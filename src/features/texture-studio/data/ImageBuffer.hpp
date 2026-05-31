#pragma once
//
// ImageBuffer.hpp - Owning, contiguous RGBA8 pixel buffer with a stride.
//
// The whole texture-studio pipeline (load → cluster → tint → repack → save)
// passes images through this type instead of stb's raw pointers. Benefits:
//
//   - Move semantics + RAII so we never leak a stbi_image_free.
//   - Bounds-checked access via at()/atRef() during debug, raw access in
//     release through data().
//   - Cheap sub-rect copy / paste primitives the packer and tinter need.
//   - Encapsulates "is this row stride padded?" so callers don't have to
//     special-case sub-images. We always store tightly packed RGBA (stride
//     == width * 4) but keep the stride field explicit for future flexibility.
//
// Save/load go through the engine-wide stb_impl.cpp / ImageConverter.cpp so
// we don't redefine STB_*_IMPLEMENTATION here.
//

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace paimon::texture_studio {

class ImageBuffer {
public:
    static constexpr std::size_t kBytesPerPixel = 4;  // always RGBA8

    // ── Constructors ────────────────────────────────────────────────────

    // Empty buffer (0×0). Used as a placeholder before load.
    ImageBuffer() = default;

    // Allocate a fresh buffer filled with transparent black.
    ImageBuffer(int width, int height);

    // Allocate from an existing RGBA8 raw pointer (copies). Pass `nullptr`
    // to allocate empty (transparent) and write later via atRef().
    ImageBuffer(int width, int height, std::uint8_t const* rgbaPixels);

    ImageBuffer(ImageBuffer const&) = default;
    ImageBuffer(ImageBuffer&&) noexcept = default;
    ImageBuffer& operator=(ImageBuffer const&) = default;
    ImageBuffer& operator=(ImageBuffer&&) noexcept = default;
    ~ImageBuffer() = default;

    // ── Geometry ────────────────────────────────────────────────────────

    int  width()  const { return m_width; }
    int  height() const { return m_height; }
    bool empty()  const { return m_width <= 0 || m_height <= 0; }

    // Number of pixels (width * height).
    std::size_t pixelCount() const {
        return static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    }

    // Bytes per row. Always width * 4 in our representation.
    std::size_t stride() const { return static_cast<std::size_t>(m_width) * kBytesPerPixel; }

    // Total byte size of the underlying buffer.
    std::size_t byteSize() const { return m_pixels.size(); }

    // ── Raw access ──────────────────────────────────────────────────────

    std::uint8_t*       data()       { return m_pixels.data(); }
    std::uint8_t const* data() const { return m_pixels.data(); }

    std::span<std::uint8_t>       span()       { return std::span(m_pixels); }
    std::span<std::uint8_t const> span() const { return std::span(m_pixels); }

    // ── Pixel access ────────────────────────────────────────────────────

    // Out-of-bounds reads return {0,0,0,0}. Out-of-bounds writes are no-ops.
    // These are intentionally slow paths — for hot loops use data() directly.
    struct Pixel {
        std::uint8_t r, g, b, a;
    };

    Pixel at(int x, int y) const;
    void  setAt(int x, int y, Pixel p);

    // Direct mutable reference — caller must ensure (x,y) is in-bounds.
    std::uint8_t* atRef(int x, int y) {
        return m_pixels.data() + (static_cast<std::size_t>(y) * stride() + static_cast<std::size_t>(x) * kBytesPerPixel);
    }
    std::uint8_t const* atRef(int x, int y) const {
        return m_pixels.data() + (static_cast<std::size_t>(y) * stride() + static_cast<std::size_t>(x) * kBytesPerPixel);
    }

    // ── Manipulation ────────────────────────────────────────────────────

    // Resize destructively. Allocates a fresh transparent buffer.
    void reset(int width, int height);

    // Fill the entire buffer with a single RGBA color.
    void clear(Pixel color = {0, 0, 0, 0});

    // Extract a sub-rectangle into a new buffer. Areas outside the source
    // are filled with transparent black. Useful for plist frame extraction.
    ImageBuffer subRect(int x, int y, int w, int h) const;

    // Blit `src` into this buffer at (dstX, dstY) using straight-alpha
    // overwrite (no compositing — destination pixels are replaced wholesale).
    // Used by the packer when assembling the output atlas; no compositing is
    // needed because each frame goes into a unique area of the sheet.
    void blitOverwrite(int dstX, int dstY, ImageBuffer const& src);

    // Rotate this image 90 degrees counter-clockwise. Used to undo the
    // cocos2d "textureRotated" packing convention. Width/height swap.
    void rotateCCW90();

    // Rotate this image 90 degrees clockwise (the inverse of CCW).
    void rotateCW90();

    // ── IO ──────────────────────────────────────────────────────────────

    // Load from a PNG/JPG file via stb_image. Always decoded to RGBA8.
    static geode::Result<ImageBuffer> loadFromFile(std::filesystem::path const& path);

    // Decode from an in-memory PNG/JPG buffer.
    static geode::Result<ImageBuffer> loadFromMemory(std::span<std::uint8_t const> bytes);

    // Save as PNG via stb_image_write. Returns Err on failure.
    geode::Result<> saveToPng(std::filesystem::path const& path) const;

    // Encode as PNG into a byte buffer (used for zip packaging).
    geode::Result<std::vector<std::uint8_t>> encodeAsPng() const;

private:
    int m_width  = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_pixels;  // tightly packed RGBA8
};

}  // namespace paimon::texture_studio
