#include "ImageBuffer.hpp"

#include "../../../utils/stb_image.h"
#include "../../../utils/stb_image_write.h"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cstring>

// stb_image_write requires the implementation macro to be defined exactly
// once in the entire translation unit graph. ImageConverter.cpp already does
// that for us (with STBIW_WINDOWS_UTF8). stb_image's implementation lives in
// stb_impl.cpp. So this TU only consumes the headers — no #define here.

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Helper: vector adapter for stbi_write_png_to_func.
void stbiWriteToVector(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<std::uint8_t>*>(context);
    auto* bytes = static_cast<std::uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

}  // namespace

// ── Constructors ────────────────────────────────────────────────────────

ImageBuffer::ImageBuffer(int width, int height) {
    reset(width, height);
}

ImageBuffer::ImageBuffer(int width, int height, std::uint8_t const* rgbaPixels) {
    reset(width, height);
    if (rgbaPixels && !empty()) {
        std::memcpy(m_pixels.data(), rgbaPixels, m_pixels.size());
    }
}

// ── Pixel access ────────────────────────────────────────────────────────

ImageBuffer::Pixel ImageBuffer::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
        return {0, 0, 0, 0};
    }
    auto* p = atRef(x, y);
    return {p[0], p[1], p[2], p[3]};
}

void ImageBuffer::setAt(int x, int y, Pixel p) {
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return;
    auto* dst = atRef(x, y);
    dst[0] = p.r;
    dst[1] = p.g;
    dst[2] = p.b;
    dst[3] = p.a;
}

// ── Manipulation ────────────────────────────────────────────────────────

void ImageBuffer::reset(int width, int height) {
    m_width  = std::max(0, width);
    m_height = std::max(0, height);
    m_pixels.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * kBytesPerPixel, 0);
}

void ImageBuffer::clear(Pixel color) {
    if (empty()) return;
    // memset path when all bytes are the same (transparent black is the
    // overwhelming common case).
    if (color.r == 0 && color.g == 0 && color.b == 0 && color.a == 0) {
        std::memset(m_pixels.data(), 0, m_pixels.size());
        return;
    }
    // General path: write 4 bytes at a time.
    std::uint32_t packed =
        static_cast<std::uint32_t>(color.r)
        | (static_cast<std::uint32_t>(color.g) << 8)
        | (static_cast<std::uint32_t>(color.b) << 16)
        | (static_cast<std::uint32_t>(color.a) << 24);
    auto* dst = m_pixels.data();
    auto count = pixelCount();
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(dst + i * kBytesPerPixel, &packed, sizeof(packed));
    }
}

ImageBuffer ImageBuffer::subRect(int x, int y, int w, int h) const {
    ImageBuffer out(w, h);
    if (out.empty() || empty()) return out;

    // Clip the source rectangle to the actual image bounds; rows/cols outside
    // remain transparent (already zeroed by reset()).
    int srcX0 = std::max(x, 0);
    int srcY0 = std::max(y, 0);
    int srcX1 = std::min(x + w, m_width);
    int srcY1 = std::min(y + h, m_height);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) return out;

    int dstX0 = srcX0 - x;
    int dstY0 = srcY0 - y;

    int rowBytes = (srcX1 - srcX0) * static_cast<int>(kBytesPerPixel);
    for (int sy = srcY0; sy < srcY1; ++sy) {
        auto* dst = out.atRef(dstX0, dstY0 + (sy - srcY0));
        auto* src = atRef(srcX0, sy);
        std::memcpy(dst, src, static_cast<std::size_t>(rowBytes));
    }
    return out;
}

void ImageBuffer::blitOverwrite(int dstX, int dstY, ImageBuffer const& src) {
    if (src.empty() || empty()) return;

    int srcX0 = 0;
    int srcY0 = 0;
    int srcX1 = src.width();
    int srcY1 = src.height();

    // Clip against destination bounds.
    if (dstX < 0)            { srcX0 = -dstX; dstX = 0; }
    if (dstY < 0)            { srcY0 = -dstY; dstY = 0; }
    if (dstX + (srcX1 - srcX0) > m_width)  srcX1 = srcX0 + (m_width  - dstX);
    if (dstY + (srcY1 - srcY0) > m_height) srcY1 = srcY0 + (m_height - dstY);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) return;

    int rowBytes = (srcX1 - srcX0) * static_cast<int>(kBytesPerPixel);
    for (int sy = srcY0; sy < srcY1; ++sy) {
        auto* dst = atRef(dstX, dstY + (sy - srcY0));
        auto const* srcRow = src.atRef(srcX0, sy);
        std::memcpy(dst, srcRow, static_cast<std::size_t>(rowBytes));
    }
}

void ImageBuffer::rotateCCW90() {
    if (empty()) return;
    ImageBuffer rotated(m_height, m_width);
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            // CCW: (x, y) → (y, w - 1 - x).  In our top-left-origin space,
            // this is the conventional 2D rotation.
            auto px = at(x, y);
            rotated.setAt(y, m_width - 1 - x, px);
        }
    }
    *this = std::move(rotated);
}

void ImageBuffer::rotateCW90() {
    if (empty()) return;
    ImageBuffer rotated(m_height, m_width);
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            // CW: (x, y) → (h - 1 - y, x).
            auto px = at(x, y);
            rotated.setAt(m_height - 1 - y, x, px);
        }
    }
    *this = std::move(rotated);
}

// ── IO ──────────────────────────────────────────────────────────────────

geode::Result<ImageBuffer> ImageBuffer::loadFromFile(std::filesystem::path const& path) {
    auto bytes = file::readBinary(path);
    if (!bytes) {
        return Err("ImageBuffer::loadFromFile: cannot read {}: {}",
            path.string(), bytes.unwrapErr());
    }
    return loadFromMemory(std::span<std::uint8_t const>(
        bytes.unwrap().data(), bytes.unwrap().size()));
}

geode::Result<ImageBuffer> ImageBuffer::loadFromMemory(std::span<std::uint8_t const> bytes) {
    if (bytes.empty()) return Err("ImageBuffer::loadFromMemory: empty input");

    int w = 0, h = 0, channels = 0;
    auto* px = stbi_load_from_memory(
        bytes.data(),
        static_cast<int>(bytes.size()),
        &w, &h, &channels, 4);  // force RGBA8

    if (!px) {
        return Err("ImageBuffer::loadFromMemory: stbi_load failed: {}",
            stbi_failure_reason() ? stbi_failure_reason() : "unknown");
    }
    if (w <= 0 || h <= 0) {
        stbi_image_free(px);
        return Err("ImageBuffer::loadFromMemory: invalid dimensions {}x{}", w, h);
    }

    ImageBuffer out(w, h, px);
    stbi_image_free(px);
    return Ok(std::move(out));
}

geode::Result<> ImageBuffer::saveToPng(std::filesystem::path const& path) const {
    auto encoded = encodeAsPng();
    if (!encoded) return Err(encoded.unwrapErr());

    auto res = file::writeBinary(path, encoded.unwrap());
    if (!res) {
        return Err("ImageBuffer::saveToPng: write failed: {}", res.unwrapErr());
    }
    return Ok();
}

geode::Result<std::vector<std::uint8_t>> ImageBuffer::encodeAsPng() const {
    if (empty()) return Err("ImageBuffer::encodeAsPng: empty image");

    std::vector<std::uint8_t> out;
    out.reserve(pixelCount());  // rough lower bound; stb will grow as needed

    int ok = stbi_write_png_to_func(
        &stbiWriteToVector, &out,
        m_width, m_height, 4,
        m_pixels.data(),
        static_cast<int>(stride()));
    if (!ok) return Err("ImageBuffer::encodeAsPng: stbi_write_png_to_func failed");
    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
