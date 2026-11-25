#include "GifCompute.hpp"

#if defined(_WIN32) && defined(PAIM_GIF_COMPUTE)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include "GifCoverageShader.hpp"
#include "GifQuantizeShader.hpp"
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>

#if defined(PAIM_USE_GEODE)
#include <Geode/loader/Log.hpp>
#endif

namespace paimon::gifimport {
namespace compute {

using Microsoft::WRL::ComPtr;

constexpr int kTileSize = 8;
constexpr std::size_t kMinimumCells = 128 * 128;
constexpr std::size_t kMaximumTileEntries = 4 * 1024 * 1024;

struct GpuShape {
    float x, y, width, height;
    float cosine, sine, skewTangent, skewCosine;
    std::uint32_t kind;
};
static_assert(sizeof(GpuShape) == 36);
static_assert(static_cast<int>(PrimitiveKind::Circle) == 2);
static_assert(static_cast<int>(PrimitiveKind::Triangle) == 3);
static_assert(static_cast<int>(PrimitiveKind::WideTriangle) == 4);

struct TileRange { std::uint32_t begin = 0, end = 0; };
using Parameters = std::array<std::uint32_t, 8>;

// the importer owns this device/context. The mutex protects concurrent Render
// passes without involving the game's OpenGL state or its render thread.
class ComputeDevice {
public:
    bool measure(std::span<GpuShape const> shapes, std::span<TileRange const> tiles,
        std::span<std::uint32_t const> indices, Parameters const& parameters,
        std::vector<std::uint32_t>& result) {
        std::lock_guard lock(m_mutex);
        if (!initialize()) return false;
        std::array<ComPtr<ID3D11ShaderResourceView>, 3> views;
        if (!upload(shapes, views[0]) || !upload(tiles, views[1]) || !upload(indices, views[2])) return false;
        return dispatch(views, parameters, m_coverageShader.Get(), result.data());
    }

    bool quantize(SourceFrame const& frame, std::span<std::uint8_t const> removed,
        std::span<std::int16_t const> lookup, Parameters const& parameters,
        std::vector<std::int32_t>& cells) {
        std::lock_guard lock(m_mutex);
        if (!initialize()) return false;
        std::array<ComPtr<ID3D11ShaderResourceView>, 3> views;
        std::vector<std::uint8_t> padded;
        if (removed.size() % 4 != 0) {
            padded.assign(removed.begin(), removed.end());
            padded.resize((padded.size() + 3) & ~std::size_t{3}, 0);
            removed = padded;
        }
        if (!uploadRaw(std::as_bytes(std::span(frame.rgba)), views[0]) ||
            !uploadRaw(std::as_bytes(removed), views[1]) || !uploadRaw(std::as_bytes(lookup), views[2])) return false;
        return dispatch(views, parameters, m_quantizeShader.Get(), cells.data());
    }

    void shutdown() {
        std::lock_guard lock(m_mutex);
        m_failed = true;
        m_capacity = 0;
        m_outputView.Reset();
        m_output.Reset();
        m_readback.Reset();
        m_parameters.Reset();
        m_coverageShader.Reset();
        m_quantizeShader.Reset();
        m_context.Reset();
        m_device.Reset();
    }

private:
    bool dispatch(std::array<ComPtr<ID3D11ShaderResourceView>, 3> const& views,
        Parameters const& parameters, ID3D11ComputeShader* shader, void* destination) {
        auto const count = static_cast<std::size_t>(parameters[0]) * parameters[1];
        if (!resizeOutput(count)) return false;
        m_context->UpdateSubresource(m_parameters.Get(), 0, nullptr, parameters.data(), 0, 0);
        std::array<ID3D11ShaderResourceView*, 3> resources{views[0].Get(), views[1].Get(), views[2].Get()};
        auto* constants = m_parameters.Get();
        auto* output = m_outputView.Get();
        m_context->CSSetShader(shader, nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, &constants);
        m_context->CSSetShaderResources(0, static_cast<UINT>(resources.size()), resources.data());
        m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        m_context->Dispatch((parameters[0] + 7) / 8, (parameters[1] + 7) / 8, 1);
        // unbind before copying and before the local SRVs go out of scope.
        std::array<ID3D11ShaderResourceView*, 3> empty{};
        ID3D11UnorderedAccessView* noOutput = nullptr;
        m_context->CSSetShaderResources(0, static_cast<UINT>(empty.size()), empty.data());
        m_context->CSSetUnorderedAccessViews(0, 1, &noOutput, nullptr);
        m_context->CopyResource(m_readback.Get(), m_output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // map blocks the worker until this small readback is ready; it does not
        // busy-poll or block the editor/render thread.
        if (FAILED(m_context->Map(m_readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return fail();
        }
        std::memcpy(destination, mapped.pData, count * sizeof(std::uint32_t));
        m_context->Unmap(m_readback.Get(), 0);
        return true;
    }

    bool initialize() {
        if (m_failed) return false;
        if (m_parameters) return true;
        // hardware only: WARP would move this work straight back to the CPU.
        D3D_FEATURE_LEVEL const level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            &level, 1, D3D11_SDK_VERSION, &m_device, nullptr, &m_context))) return fail();
        if (FAILED(m_device->CreateComputeShader(kCoverageShaderBytecode, sizeof(kCoverageShaderBytecode),
            nullptr, &m_coverageShader)) ||
            FAILED(m_device->CreateComputeShader(kQuantizeShaderBytecode, sizeof(kQuantizeShaderBytecode),
            nullptr, &m_quantizeShader))) return fail();
        D3D11_BUFFER_DESC constants{};
        constants.ByteWidth = sizeof(Parameters);
        constants.Usage = D3D11_USAGE_DEFAULT;
        constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_device->CreateBuffer(&constants, nullptr, &m_parameters))) return fail();
#if defined(PAIM_USE_GEODE)
        geode::log::info("[GifImport] DirectCompute GPU color voting and surface coverage enabled");
#endif
        return true;
    }

    bool fail() {
        m_failed = true;
#if defined(PAIM_USE_GEODE)
        geode::log::warn("[GifImport] DirectCompute unavailable; using CPU conversion");
#endif
        return false;
    }

    template <typename T>
    bool upload(std::span<T const> values, ComPtr<ID3D11ShaderResourceView>& view) {
        if (values.empty() || values.size_bytes() > std::numeric_limits<UINT>::max()) return false;
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(values.size_bytes());
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        description.StructureByteStride = sizeof(T);
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = values.data();
        ComPtr<ID3D11Buffer> buffer;
        return SUCCEEDED(m_device->CreateBuffer(&description, &initial, &buffer)) &&
            SUCCEEDED(m_device->CreateShaderResourceView(buffer.Get(), nullptr, &view));
    }

    bool uploadRaw(std::span<std::byte const> bytes, ComPtr<ID3D11ShaderResourceView>& view) {
        if (bytes.empty() || bytes.size() % 4 || bytes.size() > std::numeric_limits<UINT>::max()) return false;
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(bytes.size());
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = bytes.data();
        ComPtr<ID3D11Buffer> buffer;
        if (FAILED(m_device->CreateBuffer(&description, &initial, &buffer))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC descriptionView{};
        descriptionView.Format = DXGI_FORMAT_R32_TYPELESS;
        descriptionView.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        descriptionView.BufferEx.NumElements = description.ByteWidth / 4;
        descriptionView.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        return SUCCEEDED(m_device->CreateShaderResourceView(buffer.Get(), &descriptionView, &view));
    }

    bool resizeOutput(std::size_t count) {
        if (m_capacity >= count) return true;
        m_outputView.Reset(); m_output.Reset(); m_readback.Reset();
        m_capacity = 0;
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(count * sizeof(std::uint32_t));
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        description.StructureByteStride = sizeof(std::uint32_t);
        if (FAILED(m_device->CreateBuffer(&description, nullptr, &m_output)) ||
            FAILED(m_device->CreateUnorderedAccessView(m_output.Get(), nullptr, &m_outputView))) return false;
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.MiscFlags = 0;
        description.StructureByteStride = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_device->CreateBuffer(&description, nullptr, &m_readback))) return false;
        m_capacity = count;
        return true;
    }

    std::mutex m_mutex;
    bool m_failed = false;
    std::size_t m_capacity = 0;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11ComputeShader> m_coverageShader, m_quantizeShader;
    ComPtr<ID3D11Buffer> m_parameters, m_output, m_readback;
    ComPtr<ID3D11UnorderedAccessView> m_outputView;
};

std::atomic<ComputeDevice*> s_computeDevice{nullptr};

ComputeDevice& device() {
    // Heap-leaked: ID3D11Device/Context COM objects must never release under DllMain Loader Lock.
    static auto* instance = []() {
        auto* p = new ComputeDevice();
        s_computeDevice.store(p, std::memory_order_release);
        return p;
    }();
    return *instance;
}

} // namespace compute

void shutdownComputeDevice() {
    if (auto* instance = compute::s_computeDevice.load(std::memory_order_acquire)) {
        instance->shutdown();
    }
}

bool computeSurfaceCoverage(std::span<ShapeXform const> shapes, int width, int height,
    bool protectTransparency, std::vector<std::uint32_t>& coverage) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || shapes.empty()) return false;
    auto const cells = static_cast<std::size_t>(width) * height;
    if (cells < compute::kMinimumCells) return false;
    // native alpha masks are deliberately handled by the existing CPU sampler.
    for (auto const& shape : shapes) {
        if (shape.kind == PrimitiveKind::Stamp ||
            (shape.kind == PrimitiveKind::Circle && shape.skewTangent != 0.f) ||
            !std::isfinite(shape.x) || !std::isfinite(shape.y) ||
            !std::isfinite(shape.width) || !std::isfinite(shape.height) ||
            !std::isfinite(shape.extentX) || !std::isfinite(shape.extentY)) return false;
    }
    int const tilesWide = (width + compute::kTileSize - 1) / compute::kTileSize;
    int const tilesHigh = (height + compute::kTileSize - 1) / compute::kTileSize;
    auto const tileCount = static_cast<std::size_t>(tilesWide) * tilesHigh;
    std::vector<compute::TileRange> ranges(tileCount);
    std::vector<compute::GpuShape> packed;
    packed.reserve(shapes.size());
    auto visitTiles = [&](ShapeXform const& shape, auto&& visit) {
        auto const box = xformBox(shape, width, height);
        if (box[2] < box[0] || box[3] < box[1]) return;
        for (int y = box[1] / compute::kTileSize; y <= box[3] / compute::kTileSize; ++y)
            for (int x = box[0] / compute::kTileSize; x <= box[2] / compute::kTileSize; ++x)
                visit(static_cast<std::size_t>(y) * tilesWide + x);
    };
    std::size_t entries = 0;
    for (auto const& shape : shapes) {
        packed.push_back({shape.x, shape.y, shape.width, shape.height, shape.cosine, shape.sine,
            shape.skewTangent, shape.skewCosine, static_cast<std::uint32_t>(shape.kind)});
        visitTiles(shape, [&](std::size_t tile) { ++ranges[tile].end; ++entries; });
        if (entries > compute::kMaximumTileEntries) return false;
    }
    if (!entries) { coverage.assign(cells, 0); return true; }
    std::uint32_t offset = 0;
    for (auto& range : ranges) {
        auto const count = range.end;
        range.begin = range.end = offset;
        offset += count;
    }
    std::vector<std::uint32_t> indices(entries);
    for (std::size_t index = 0; index < shapes.size(); ++index)
        visitTiles(shapes[index], [&](std::size_t tile) {
            indices[ranges[tile].end++] = static_cast<std::uint32_t>(index);
        });
    coverage.resize(cells);
    return compute::device().measure(packed, ranges, indices,
        {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            static_cast<std::uint32_t>(tilesWide), protectTransparency ? 1u : 0u}, coverage);
}

bool computeQuantizedFrame(SourceFrame const& frame, std::span<std::uint8_t const> removed,
    std::span<std::int16_t const> lookup, int sourceWidth, int sourceHeight,
    int width, int height, int stride, int alphaThreshold, int colors,
    std::vector<std::int32_t>& cells) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > 4096 || sourceHeight > 4096 ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        colors < 1 || colors > 64 || stride < 1 || alphaThreshold < 1 || alphaThreshold > 255) return false;
    auto const count = static_cast<std::size_t>(width) * height;
    auto const sourcePixels = static_cast<std::size_t>(sourceWidth) * sourceHeight;
    if (count < compute::kMinimumCells || sourcePixels < count || frame.rgba.size() != sourcePixels * 4 ||
        removed.size() != sourcePixels || lookup.size() != 32768) return false;
    cells.resize(count);
    return compute::device().quantize(frame, removed, lookup,
        {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            static_cast<std::uint32_t>(sourceWidth), static_cast<std::uint32_t>(sourceHeight),
            static_cast<std::uint32_t>(stride), static_cast<std::uint32_t>(alphaThreshold),
            static_cast<std::uint32_t>(colors), 0}, cells);
}

} // namespace paimon::gifimport
#endif
