#include "DecoderMF.hpp"

#if defined(USE_MEDIA_FOUNDATION)

#include <Geode/loader/Log.hpp>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <objbase.h>
#include "../../utils/TimedJoin.hpp"
#include "../../core/Settings.hpp"
#include <libyuv/planar_functions.h>
#include <libyuv/scale.h>

namespace paimon {

namespace {
// One shared D3D11 device avoids concurrent creation and per-player startup cost.
std::mutex g_d3d11Mutex;
ID3D11Device*        g_sharedD3DDevice = nullptr;
ID3D11DeviceContext* g_sharedD3DCtx    = nullptr;
int                  g_sharedD3DRefs   = 0;     // Active decoder references.
bool                 g_sharedD3DBroken = false; // Device creation/loss is sticky.

// Fail closed after a device-creation error; software decode is the fallback.
bool acquireSharedD3D11(ID3D11Device*& outDevice, ID3D11DeviceContext*& outCtx) {
    std::lock_guard lk(g_d3d11Mutex);
    if (g_sharedD3DBroken) return false;

    if (g_sharedD3DDevice && g_sharedD3DCtx) {
        outDevice = g_sharedD3DDevice;
        outCtx    = g_sharedD3DCtx;
        ++g_sharedD3DRefs;
        return true;
    }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL outLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,  // Required for DXVA.
        levels, 2, D3D11_SDK_VERSION,
        &g_sharedD3DDevice, &outLevel, &g_sharedD3DCtx);

    if (FAILED(hr) || !g_sharedD3DDevice) {
        geode::log::warn("DecoderMF: shared D3D11CreateDevice failed (hr={:08X})", static_cast<unsigned>(hr));
        g_sharedD3DDevice = nullptr;
        g_sharedD3DCtx    = nullptr;
        g_sharedD3DBroken = true;
        return false;
    }

    {
        ID3D10Multithread* mt = nullptr;
        hr = g_sharedD3DCtx->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&mt));
        if (SUCCEEDED(hr) && mt) {
            mt->SetMultithreadProtected(TRUE);
            mt->Release();
            geode::log::info("DecoderMF: shared D3D11 multithread protection enabled");
        } else {
            geode::log::warn("DecoderMF: failed to enable shared D3D11 multithread protection");
        }
    }

    outDevice = g_sharedD3DDevice;
    outCtx    = g_sharedD3DCtx;
    ++g_sharedD3DRefs;
    geode::log::info("DecoderMF: created shared D3D11 device (process-wide, DXVA-capable)");
    return true;
}

void releaseSharedD3D11() {
    std::lock_guard lk(g_d3d11Mutex);
    if (g_sharedD3DRefs > 0) --g_sharedD3DRefs;
}

void releaseMfObjectsSafely(ID3D11Texture2D*& stagingTex, IMFSourceReader*& reader) {
    __try {
        if (stagingTex) {
            stagingTex->Release();
            stagingTex = nullptr;
        }
        if (reader) {
            reader->Release();
            reader = nullptr;
        }
    } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
               ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        stagingTex = nullptr;
        reader = nullptr;
    }
}

void releaseD3D11Safely(ID3D11Device*& dev, ID3D11DeviceContext*& ctx, IMFDXGIDeviceManager*& mgr) {
    __try {
        if (mgr) { mgr->Release(); mgr = nullptr; }
        if (ctx) { ctx->Release(); ctx = nullptr; }
        if (dev) { dev->Release(); dev = nullptr; }
    } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
               ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        dev = nullptr;
        ctx = nullptr;
        mgr = nullptr;
    }
}
}

bool DecoderMF::open(const std::string& path) {
    closeInternal();
    m_decodeThreadDetached.store(false, std::memory_order_release);
    m_videoPath = path;

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: MFStartup failed (hr={})", hr);
        return false;
    }

    if (!setupD3D11()) {
        geode::log::warn("DecoderMF: D3D11 setup failed, continuing without HW accel");
    }

    if (!setupReader(path)) {
        closeInternal();
        return false;
    }

    if (!m_ring.init(m_outWidth, m_outHeight)) {
        closeInternal();
        return false;
    }

    if (m_downscaleFactor > 1) {
        int uvW = (m_width + 1) / 2;
        int uvH = (m_height + 1) / 2;
        m_scratch.planeY  = Frame::allocAligned(Frame::alignedSize(m_width, m_height));
        m_scratch.planeCb = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
        m_scratch.planeCr = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
        m_scratch.strideY  = Frame::alignedStride(m_width);
        m_scratch.strideCb = Frame::alignedStride(uvW);
        m_scratch.strideCr = Frame::alignedStride(uvW);
        m_scratch.width  = m_width;
        m_scratch.height = m_height;
        if (!m_scratch.planeY || !m_scratch.planeCb || !m_scratch.planeCr) {
            geode::log::warn("DecoderMF: scratch alloc failed, disabling downscale");
            closeInternal();
            return false;
        }
    }

    m_finished.store(false, std::memory_order_relaxed);
    m_decoding.store(false, std::memory_order_relaxed);
    return true;
}

bool DecoderMF::setupD3D11() {
    if (!acquireSharedD3D11(m_d3dDevice, m_d3dCtx)) {
        return false;
    }

    HRESULT hr = MFCreateDXGIDeviceManager(&m_resetToken, &m_dxgiMgr);
    if (FAILED(hr) || !m_dxgiMgr) {
        geode::log::warn("DecoderMF: MFCreateDXGIDeviceManager failed (hr={:08X})", static_cast<unsigned>(hr));
        releaseSharedD3D11();
        m_d3dDevice = nullptr;
        m_d3dCtx    = nullptr;
        return false;
    }

    hr = m_dxgiMgr->ResetDevice(m_d3dDevice, m_resetToken);
    m_dxvaEnabled = SUCCEEDED(hr);
    if (!m_dxvaEnabled) {
        geode::log::warn("DecoderMF: DXGI manager ResetDevice failed, DXVA unavailable");
    }

    m_sharedD3D = true;
    return true;
}

bool DecoderMF::setupReader(const std::string& path) {
// Use DXVA when available; copy frames through a staging texture.
    IMFAttributes* attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 3);
    if (FAILED(hr)) return false;

    hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: failed to set MF_LOW_LATENCY");
    }

// The decode loop accepts both D3D11 surfaces and system memory.
    if (m_dxvaEnabled && m_dxgiMgr) {
        hr = attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_dxgiMgr);
        if (FAILED(hr)) {
            geode::log::warn("DecoderMF: failed to set D3D manager, DXVA disabled");
            m_dxvaEnabled = false;
        } else {
            geode::log::info("DecoderMF: DXVA hardware acceleration enabled");
        }
    }

    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '/', '\\');

    int wLen = MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, nullptr, 0);
    if (wLen <= 0) { attrs->Release(); return false; }
    auto* wPath = new (std::nothrow) wchar_t[wLen];
    if (!wPath) { attrs->Release(); return false; }
    MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, wPath, wLen);

    hr = MFCreateSourceReaderFromURL(wPath, attrs, &m_reader);
    delete[] wPath;
    attrs->Release();

    if (FAILED(hr) || !m_reader) {
        geode::log::warn("DecoderMF: MFCreateSourceReaderFromURL failed (hr={})", hr);
        return false;
    }

    if (!setOutputFormat()) {
        geode::log::warn("DecoderMF: failed to set any output format");
        return false;
    }

    IMFMediaType* currentType = nullptr;
    hr = m_reader->GetCurrentMediaType(
        static_cast<UINT32>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentType);
    if (FAILED(hr)) return false;

    UINT32 w = 0, h = 0;
    hr = MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &w, &h);
    if (FAILED(hr)) { currentType->Release(); return false; }

    GUID actualSubtype = GUID_NULL;
    if (SUCCEEDED(currentType->GetGUID(MF_MT_SUBTYPE, &actualSubtype))) {
        if (actualSubtype != m_pixelFormat) {
            const char* actualName =
                actualSubtype == MFVideoFormat_NV12 ? "NV12" :
                actualSubtype == MFVideoFormat_I420 ? "I420" :
                actualSubtype == MFVideoFormat_YV12 ? "YV12" : "unknown";
            geode::log::warn("DecoderMF: actual output subtype ({}) differs from requested", actualName);
            m_pixelFormat = actualSubtype;
        }
    }
    currentType->Release();

    m_width  = static_cast<int>(w);
    m_height = static_cast<int>(h);

    // Apply the quality cap to reduce ring-buffer, texture, PBO, and FBO memory.
    // Integer scaling preserves aspect ratio and even 4:2:0 dimensions.
    m_outWidth  = m_width;
    m_outHeight = m_height;
    m_downscaleFactor = 1;
    int cap = paimon::settings::video::videoMaxDecodeDimension();
    if (cap > 0) {
        int maxDim = std::max(m_width, m_height);
        if (maxDim > cap) {
            int f = std::min((maxDim + cap - 1) / cap, 4);
            if (f >= 2) {
                m_downscaleFactor = f;
                m_outWidth  = std::max(2, (m_width  / f) & ~1);
                m_outHeight = std::max(2, (m_height / f) & ~1);
                geode::log::info("DecoderMF: downscaling {}x{} -> {}x{} (factor {}, quality cap {})",
                    m_width, m_height, m_outWidth, m_outHeight, f, cap);
            }
        }
    }

    PROPVARIANT var;
    hr = m_reader->GetPresentationAttribute(
        static_cast<UINT32>(MF_SOURCE_READER_MEDIASOURCE),
        MF_PD_DURATION, &var);
    if (SUCCEEDED(hr)) {
        if (var.vt == VT_UI8) {
            m_duration = static_cast<double>(var.uhVal.QuadPart) / 10000000.0;
        }
        PropVariantClear(&var);
    }

    return true;
}

bool DecoderMF::setOutputFormat() {
    const GUID formatsToTry[] = {
        MFVideoFormat_NV12,  // Native MF format, interleaved CbCr.
        MFVideoFormat_I420,  // Y→Cb→Cr.
        MFVideoFormat_YV12,  // Y→Cr→Cb; swap required.
    };

    for (const auto& fmt : formatsToTry) {
        IMFMediaType* type = nullptr;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) continue;

        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, fmt);

        hr = m_reader->SetCurrentMediaType(
            static_cast<UINT32>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr, type);
        type->Release();

        if (SUCCEEDED(hr)) {
            m_subType = fmt;
            m_pixelFormat = fmt;
            const char* name =
                fmt == MFVideoFormat_NV12 ? "NV12" :
                fmt == MFVideoFormat_I420 ? "I420" : "YV12";
            geode::log::info("DecoderMF: output format set to {}", name);
            return true;
        }
    }
    return false;
}

// Derive padded Y rows from the buffer size instead of codec alignment guesses.
static int deriveYPlaneRows(size_t bufferSize, int stride, int visibleHeight) {
    int heuristic = (visibleHeight + 15) & ~15;
    if (bufferSize == 0 || stride <= 0 || visibleHeight <= 0) return heuristic;

    long long derived = static_cast<long long>(bufferSize) * 2 /
                        (static_cast<long long>(stride) * 3);

    if (derived >= visibleHeight && derived <= static_cast<long long>(visibleHeight) + 256) {
        return static_cast<int>(derived);
    }
    return heuristic;
}

void DecoderMF::copyPlanesToSlot2D(BYTE* scanline0, LONG lStride, Frame& slot, size_t bufferSize) {
    // Normalize bottom-up frames.
    if (lStride < 0) {
        scanline0 = scanline0 + static_cast<ptrdiff_t>(lStride) * (m_height - 1);
        lStride = -lStride;
    }

    int uvW = (m_width + 1) / 2;
    int uvH = (m_height + 1) / 2;
    int uvSrcStride = (lStride + 1) / 2;  // Planar chroma stride.

    int alignedH = deriveYPlaneRows(bufferSize, static_cast<int>(lStride), m_height);
    int alignedUvH = (alignedH + 1) / 2;

    int yCopyBytes = std::min(slot.strideY, static_cast<int>(lStride));
    if (slot.strideY == lStride && slot.strideY >= m_width) {
        std::memcpy(slot.planeY, scanline0, static_cast<size_t>(yCopyBytes) * m_height);
    } else {
        for (int r = 0; r < m_height; ++r) {
            std::memcpy(slot.planeY + r * slot.strideY,
                        scanline0 + r * lStride, yCopyBytes);
        }
    }

    if (m_pixelFormat == MFVideoFormat_I420) {
        BYTE* cbStart = scanline0 + lStride * alignedH;
        BYTE* crStart = cbStart + uvSrcStride * alignedUvH;
        if (slot.strideCb == uvSrcStride && slot.strideCr == uvSrcStride) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvSrcStride) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvSrcStride) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb,
                            cbStart + r * uvSrcStride,
                            std::min(slot.strideCb, uvW));
                std::memcpy(slot.planeCr + r * slot.strideCr,
                            crStart + r * uvSrcStride,
                            std::min(slot.strideCr, uvW));
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_YV12) {
        BYTE* crStart = scanline0 + lStride * alignedH;
        BYTE* cbStart = crStart + uvSrcStride * alignedUvH;
        if (slot.strideCb == uvSrcStride && slot.strideCr == uvSrcStride) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvSrcStride) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvSrcStride) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb,
                            cbStart + r * uvSrcStride,
                            std::min(slot.strideCb, uvW));
                std::memcpy(slot.planeCr + r * slot.strideCr,
                            crStart + r * uvSrcStride,
                            std::min(slot.strideCr, uvW));
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_NV12) {
        // NV12 stores interleaved Cb/Cr.
        BYTE* uvStart = scanline0 + lStride * alignedH;
        libyuv::SplitUVPlane(uvStart, lStride,
                             slot.planeCb, slot.strideCb,
                             slot.planeCr, slot.strideCr,
                             uvW, uvH);
    } else {
        static bool s_warnedFmt = false;
        if (!s_warnedFmt) {
            s_warnedFmt = true;
            geode::log::warn("DecoderMF: unhandled pixel format (not NV12/I420/YV12) - "
                             "chroma not extracted, video may render green");
        }
    }
}

void DecoderMF::copyPlanesToSlotLinear(BYTE* data, DWORD bufLen, Frame& slot) {
    int uvW    = (m_width + 1) / 2;
    int uvH    = (m_height + 1) / 2;

    int alignedH = deriveYPlaneRows(static_cast<size_t>(bufLen), m_width, m_height);
    int alignedUvH = (alignedH + 1) / 2;
    int ySize  = m_width * alignedH;
    int uvSize = uvW * alignedUvH;

    if (slot.strideY == m_width) {
        std::memcpy(slot.planeY, data, static_cast<size_t>(m_width) * m_height);
    } else {
        for (int r = 0; r < m_height; ++r) {
            std::memcpy(slot.planeY + r * slot.strideY,
                        data + r * m_width, m_width);
        }
    }

    if (m_pixelFormat == MFVideoFormat_I420) {
        BYTE* cbStart = data + ySize;
        BYTE* crStart = cbStart + uvSize;
        if (slot.strideCb == uvW && slot.strideCr == uvW) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvW) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvW) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb, cbStart + r * uvW, uvW);
                std::memcpy(slot.planeCr + r * slot.strideCr, crStart + r * uvW, uvW);
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_YV12) {
        BYTE* crStart = data + ySize;
        BYTE* cbStart = crStart + uvSize;
        if (slot.strideCb == uvW && slot.strideCr == uvW) {
            std::memcpy(slot.planeCb, cbStart, static_cast<size_t>(uvW) * uvH);
            std::memcpy(slot.planeCr, crStart, static_cast<size_t>(uvW) * uvH);
        } else {
            for (int r = 0; r < uvH; ++r) {
                std::memcpy(slot.planeCb + r * slot.strideCb, cbStart + r * uvW, uvW);
                std::memcpy(slot.planeCr + r * slot.strideCr, crStart + r * uvW, uvW);
            }
        }
    } else if (m_pixelFormat == MFVideoFormat_NV12) {
        // NV12 stores interleaved Cb/Cr.
        BYTE* uvStart = data + ySize;
        libyuv::SplitUVPlane(uvStart, m_width,
                             slot.planeCb, slot.strideCb,
                             slot.planeCr, slot.strideCr,
                             uvW, uvH);
    }
}

bool DecoderMF::createStagingTexture() {
    if (!m_d3dDevice || m_width <= 0 || m_height <= 0) return false;

    if (m_stagingTex) {
        m_stagingTex->Release();
        m_stagingTex = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(m_width);
    desc.Height = static_cast<UINT>(m_height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTex);
    if (FAILED(hr) || !m_stagingTex) {
        geode::log::warn("DecoderMF: failed to create staging texture ({}x{})", m_width, m_height);
        return false;
    }
    geode::log::info("DecoderMF: staging texture created ({}x{}, NV12)", m_width, m_height);
    return true;
}

bool DecoderMF::copyPlanesFromD3D11(ID3D11Texture2D* srcTexture, UINT subresource, Frame& slot) {
    if (!m_d3dCtx || !srcTexture) return false;

    D3D11_TEXTURE2D_DESC srcDesc = {};
    srcTexture->GetDesc(&srcDesc);

    if (srcDesc.Format == DXGI_FORMAT_420_OPAQUE ||
        srcDesc.Format == DXGI_FORMAT_AI44 ||
        srcDesc.Format == DXGI_FORMAT_IA44 ||
        srcDesc.Format == DXGI_FORMAT_P8 ||
        srcDesc.Format == DXGI_FORMAT_A8P8 ||
        srcDesc.Format == DXGI_FORMAT_UNKNOWN) {
        geode::log::warn("DecoderMF: DXVA output format {} is not CPU-readable, falling back",
            static_cast<int>(srcDesc.Format));
        return false;
    }

    if (!m_stagingTex || m_stagingFormat != srcDesc.Format ||
        m_stagingWidth != srcDesc.Width || m_stagingHeight != srcDesc.Height) {
        if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = srcDesc.Width;
        desc.Height = srcDesc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = srcDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTex);
        if (FAILED(hr) || !m_stagingTex) {
            geode::log::warn("DecoderMF: failed to create staging texture ({}x{}, format={})",
                srcDesc.Width, srcDesc.Height, static_cast<int>(srcDesc.Format));
            return false;
        }
        m_stagingFormat = srcDesc.Format;
        m_stagingWidth = srcDesc.Width;
        m_stagingHeight = srcDesc.Height;
        geode::log::info("DecoderMF: staging texture created ({}x{}, format={})",
            srcDesc.Width, srcDesc.Height, static_cast<int>(srcDesc.Format));
    }

    // Serialize DXVA and copy paths for driver safety.
    {
        std::lock_guard<std::mutex> ctxLk(m_d3dCtxMutex);

        m_d3dCtx->CopySubresourceRegion(m_stagingTex, 0, 0, 0, 0, srcTexture, subresource, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_d3dCtx->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            geode::log::warn("DecoderMF: failed to map staging texture (hr={:08X})", static_cast<unsigned>(hr));
            return false;
        }

        BYTE* scanline0 = static_cast<BYTE*>(mapped.pData);
        LONG lStride = static_cast<LONG>(mapped.RowPitch);

        size_t mappedSize = static_cast<size_t>(mapped.RowPitch) * m_stagingHeight * 3 / 2;
        copyPlanesToSlot2D(scanline0, lStride, slot, mappedSize);

        m_d3dCtx->Unmap(m_stagingTex, 0);
    }
    return true;
}

void DecoderMF::startDecoding() {
    // A timed-out worker is terminal; never start a second producer.
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
    if (m_decoding.load(std::memory_order_relaxed)) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    m_thread = std::thread(&DecoderMF::decodeLoop, this);
}

void DecoderMF::stopDecoding() {
    // Bound the join so ReadSample() cannot freeze the main thread.
    constexpr auto kJoinTimeout = std::chrono::milliseconds(1000);

    bool wasDecoding = m_decoding.exchange(false, std::memory_order_acq_rel);
    m_ring.wakeAll();
    if (!wasDecoding) {
        if (m_thread.joinable()) {
            if (!paimon::timedJoin(m_thread, kJoinTimeout)) {
                if (!m_decodeThreadDetached.exchange(true, std::memory_order_acq_rel))
                    noteDetachedDecoder("MediaFoundation");
            }
        }
        return;
    }

    // Flush wakes ReadSample(); SEH covers force-close after MF unload.
    if (m_reader) {
        __try {
            m_reader->Flush(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM));
        } __except(EXCEPTION_ACCESS_VIOLATION == GetExceptionCode()
                   ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        }
    }

    if (m_thread.joinable()) {
        if (!paimon::timedJoin(m_thread, kJoinTimeout)) {
            if (!m_decodeThreadDetached.exchange(true, std::memory_order_acq_rel))
                noteDetachedDecoder("MediaFoundation");
        }
    }
}

void DecoderMF::decodeLoop() {
    // COM must be initialized on the decode thread.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    int frameCount = 0;
    while (m_decoding.load(std::memory_order_relaxed)) {
        if (m_ring.isFull()) {
            m_ring.waitForWritable(50, &m_decoding);
            continue;
        }

        DWORD streamIdx = 0, flags = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = m_reader->ReadSample(
            static_cast<UINT32>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, &streamIdx, &flags, nullptr, &sample);

        if (FAILED(hr) || !m_decoding.load(std::memory_order_relaxed)) {
            if (sample) sample->Release();
            if (FAILED(hr) && m_decoding.load(std::memory_order_relaxed)) {
                geode::log::warn("DecoderMF: ReadSample failed (hr={})", hr);
            }
            m_finished.store(true, std::memory_order_release);
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();
            if (m_looping.load(std::memory_order_relaxed)) {
// Rewind in-thread: PTS restarts at 0 and the ring stays fed across the loop.
                PROPVARIANT pos;
                PropVariantInit(&pos);
                pos.vt = VT_I8;
                pos.hVal.QuadPart = 0;
                HRESULT seekHr = m_reader->SetCurrentPosition(GUID_NULL, pos);
                PropVariantClear(&pos);
                if (SUCCEEDED(seekHr)) continue;
                geode::log::warn("DecoderMF: loop rewind failed (hr={:08X})",
                                 static_cast<unsigned>(seekHr));
            }
            geode::log::info("DecoderMF: end of stream after {} frames", frameCount);
            m_finished.store(true, std::memory_order_release);
            break;
        }

        constexpr DWORD kFlushFlag = 0x200;
        if (flags & kFlushFlag) {
            if (sample) sample->Release();
            continue;
        }

        if (!sample) continue;

        auto* slot = m_ring.nextWrite();
        if (!slot) {
            sample->Release();
            m_ring.waitForWritable(50, &m_decoding);
            continue;
        }

        LONGLONG pts100ns = 0;
        sample->GetSampleTime(&pts100ns);
        slot->pts = static_cast<double>(pts100ns) / 10000000.0;

        IMFMediaBuffer* buf = nullptr;
        hr = sample->GetBufferByIndex(0, &buf);
        if (FAILED(hr) || !buf) {
            sample->Release();
            continue;
        }

        DWORD bufLen = 0;
        buf->GetCurrentLength(&bufLen);

        bool copied = false;

        Frame* dst = (m_downscaleFactor > 1) ? &m_scratch : slot;

        if (m_dxvaEnabled && m_d3dCtx) {
            IMFDXGIBuffer* dxgiBuf = nullptr;
            hr = buf->QueryInterface(IID_PPV_ARGS(&dxgiBuf));
            if (SUCCEEDED(hr) && dxgiBuf) {
                ID3D11Texture2D* tex = nullptr;
                UINT subresource = 0;
                hr = dxgiBuf->GetResource(IID_PPV_ARGS(&tex));
                if (SUCCEEDED(hr) && tex) {
                    dxgiBuf->GetSubresourceIndex(&subresource);

                    copied = copyPlanesFromD3D11(tex, subresource, *dst);

                    tex->Release();
                }
                dxgiBuf->Release();
            }
        }

        if (!copied) {
            IMF2DBuffer2* buf2d = nullptr;
            hr = buf->QueryInterface(IID_PPV_ARGS(&buf2d));
            if (SUCCEEDED(hr) && buf2d) {
                BYTE* scanline0 = nullptr;
                LONG lStride = 0;
                DWORD cbBuffer = 0;
                hr = buf2d->Lock2DSize(MF2DBuffer_LockFlags_Read,
                                       &scanline0, &lStride,
                                       nullptr, &cbBuffer);
                if (SUCCEEDED(hr)) {
                    copyPlanesToSlot2D(scanline0, lStride, *dst, static_cast<size_t>(cbBuffer));
                    copied = true;
                    buf2d->Unlock2D();
                }
                buf2d->Release();
            }
        }

        if (!copied) {
            BYTE* data = nullptr;
            hr = buf->Lock(&data, nullptr, &bufLen);
            if (SUCCEEDED(hr) && data) {
                copyPlanesToSlotLinear(data, bufLen, *dst);
                copied = true;
                buf->Unlock();
            }
        }

        buf->Release();
        sample->Release();

        if (copied) {
            if (m_downscaleFactor > 1) {
                downscalePlanes(m_scratch, *slot, m_downscaleFactor);
            }
            m_dxvaReadbackFailures = 0;
            m_ring.commitWrite();
            ++frameCount;
            if (frameCount == 1) {
                geode::log::info("DecoderMF: first frame decoded ({}x{}, format={}, dxva={})", m_width, m_height,
                    m_pixelFormat == MFVideoFormat_I420 ? "I420" :
                    m_pixelFormat == MFVideoFormat_YV12 ? "YV12" : "NV12",
                    m_dxvaEnabled ? "yes" : "no");
            }
        } else if (m_dxvaEnabled) {
            ++m_dxvaReadbackFailures;
            if (m_dxvaReadbackFailures >= 3) {
                geode::log::warn("DecoderMF: DXVA readback failed {} times, falling back to software decode",
                    m_dxvaReadbackFailures);
                if (fallbackToSoftwareDecode(m_videoPath)) {
                    geode::log::info("DecoderMF: switched to software decode, continuing");
                } else {
                    geode::log::warn("DecoderMF: software decode fallback failed, stopping");
                    m_finished.store(true, std::memory_order_release);
                    break;
                }
            }
        }
    }

    CoUninitialize();
}


// Recreate the reader without DXVA after repeated readback failures.
bool DecoderMF::fallbackToSoftwareDecode(const std::string& path) {
    m_dxvaEnabled = false;
    m_dxvaReadbackFailures = 0;

    {
        std::lock_guard<std::mutex> ctxLk(m_d3dCtxMutex);
        if (m_stagingTex) {
            m_stagingTex->Release();
            m_stagingTex = nullptr;
        }
    }
    m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    m_stagingWidth = 0;
    m_stagingHeight = 0;

    if (m_reader) {
        m_reader->Release();
        m_reader = nullptr;
    }

    IMFAttributes* attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 3);
    if (FAILED(hr)) return false;

    hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: fallback - failed to set MF_LOW_LATENCY");
    }

    hr = attrs->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    if (FAILED(hr)) {
        geode::log::warn("DecoderMF: fallback - failed to disable DXVA");
    }

    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '/', '\\');

    int wLen = MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, nullptr, 0);
    if (wLen <= 0) { attrs->Release(); return false; }
    auto* wPath = new (std::nothrow) wchar_t[wLen];
    if (!wPath) { attrs->Release(); return false; }
    MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, wPath, wLen);

    hr = MFCreateSourceReaderFromURL(wPath, attrs, &m_reader);
    delete[] wPath;
    attrs->Release();

    if (FAILED(hr) || !m_reader) {
        geode::log::warn("DecoderMF: fallback - MFCreateSourceReaderFromURL failed (hr={})", hr);
        return false;
    }

    if (!setOutputFormat()) {
        geode::log::warn("DecoderMF: fallback - failed to set output format");
        return false;
    }

    geode::log::info("DecoderMF: successfully switched to software decode");
    return true;
}

void DecoderMF::seekTo(double seconds) {
    if (!m_reader) return;
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;

    while (m_ring.nextRead()) m_ring.commitRead();

    PROPVARIANT var;
    var.vt = VT_I8;
    var.hVal.QuadPart = static_cast<LONGLONG>(seconds * 10000000.0);
    m_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    m_finished.store(false, std::memory_order_relaxed);
    if (wasDecoding) startDecoding();
}

// Box-average one 8-bit plane; clamp edge blocks to source bounds.
void DecoderMF::downscalePlanes(const Frame& src, Frame& dst, int) {
    libyuv::ScalePlane(src.planeY, src.strideY, m_width, m_height,
                       dst.planeY, dst.strideY, m_outWidth, m_outHeight,
                       libyuv::kFilterBox);
    int srcUvW = (m_width + 1) / 2,  srcUvH = (m_height + 1) / 2;
    int dstUvW = (m_outWidth + 1) / 2, dstUvH = (m_outHeight + 1) / 2;
    libyuv::ScalePlane(src.planeCb, src.strideCb, srcUvW, srcUvH,
                       dst.planeCb, dst.strideCb, dstUvW, dstUvH,
                       libyuv::kFilterBox);
    libyuv::ScalePlane(src.planeCr, src.strideCr, srcUvW, srcUvH,
                       dst.planeCr, dst.strideCr, dstUvW, dstUvH,
                       libyuv::kFilterBox);
}

bool DecoderMF::skipFrame() {
    return m_ring.skipRead();
}

double DecoderMF::getDuration() const { return m_duration; }
int DecoderMF::getWidth()  const { return m_outWidth  > 0 ? m_outWidth  : m_width; }
int DecoderMF::getHeight() const { return m_outHeight > 0 ? m_outHeight : m_height; }
bool DecoderMF::isFinished() const {
    return m_finished.load(std::memory_order_acquire);
}

double DecoderMF::peekNextPTS() const {
    return m_ring.peekNextPTS();
}

double DecoderMF::peekSecondPTS() const {
    return m_ring.peekSecondPTS();
}

const VideoFrame* DecoderMF::peekFrame() {
    return m_ring.peekRead();
}

void DecoderMF::releaseFrame() {
    if (m_ring.peekRead()) m_ring.commitRead();
}

void DecoderMF::closeInternal() {
    stopDecoding();

    // A detached worker may still hold MF/D3D references; release under SEH.
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) {
        geode::log::warn("[DecoderMF] closeInternal: decode thread was detached; "
                         "forcing COM/D3D release under SEH.");
        {
            std::lock_guard lk(g_d3d11Mutex);
            if (m_sharedD3D) {
                m_d3dDevice = nullptr;
                m_d3dCtx    = nullptr;
                if (m_dxgiMgr) { m_dxgiMgr->Release(); m_dxgiMgr = nullptr; }
                releaseSharedD3D11();
                m_sharedD3D = false;
            } else {
                releaseD3D11Safely(m_d3dDevice, m_d3dCtx, m_dxgiMgr);
            }
        }
        releaseMfObjectsSafely(m_stagingTex, m_reader);
        m_dxvaEnabled = false;
        m_dxvaReadbackFailures = 0;
        m_stagingFormat = DXGI_FORMAT_UNKNOWN;
        m_stagingWidth  = 0;
        m_stagingHeight = 0;
        m_videoPath.clear();
        return;
    }

    // Keep shared device/context ownership in the process-wide cache.
    {
        std::lock_guard lk(g_d3d11Mutex);
        if (m_dxgiMgr) {
            m_dxgiMgr->Release();
            m_dxgiMgr = nullptr;
        }
        if (m_sharedD3D) {
            m_d3dCtx    = nullptr;
            m_d3dDevice = nullptr;
        } else {
            if (m_d3dCtx) {
                m_d3dCtx->Release();
                m_d3dCtx = nullptr;
            }
            if (m_d3dDevice) {
                m_d3dDevice->Release();
                m_d3dDevice = nullptr;
            }
        }
    }
    if (m_sharedD3D) {
        releaseSharedD3D11();
        m_sharedD3D = false;
    }

    // MF may already be unloaded during force close; release pointers under SEH.
    releaseMfObjectsSafely(m_stagingTex, m_reader);
    // Do not call MFShutdown here; it is process-wide.
    m_dxvaEnabled = false;
    m_dxvaReadbackFailures = 0;
    m_stagingFormat = DXGI_FORMAT_UNKNOWN;
    m_stagingWidth = 0;
    m_stagingHeight = 0;
    m_videoPath.clear();

    Frame::freeAligned(m_scratch.planeY);
    Frame::freeAligned(m_scratch.planeCb);
    Frame::freeAligned(m_scratch.planeCr);
    m_scratch.planeY = m_scratch.planeCb = m_scratch.planeCr = nullptr;
    m_downscaleFactor = 1;
}

}

#endif
