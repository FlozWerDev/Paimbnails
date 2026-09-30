#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

namespace paimon::texture_studio {

class ImageBuffer {
public:
    static constexpr std::size_t kBytesPerPixel = 4;
    static constexpr std::size_t kMaxPixelCount = 16 * 1024 * 1024;
    static constexpr std::size_t kMaxEncodedBytes = 128 * 1024 * 1024;

    ImageBuffer() = default;
    ImageBuffer(int width, int height);
    ImageBuffer(int width, int height, std::uint8_t const* rgbaPixels);

    ImageBuffer(ImageBuffer const&) = default;
    ImageBuffer(ImageBuffer&& other) noexcept
        : m_width(std::exchange(other.m_width, 0)),
          m_height(std::exchange(other.m_height, 0)),
          m_pixels(std::move(other.m_pixels)) {}
    ImageBuffer& operator=(ImageBuffer const&) = default;
    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) {
            m_pixels = std::move(other.m_pixels);
            m_width = std::exchange(other.m_width, 0);
            m_height = std::exchange(other.m_height, 0);
        }
        return *this;
    }
    ~ImageBuffer() = default;

    int  width()  const { return m_width; }
    int  height() const { return m_height; }
    bool empty()  const { return m_width <= 0 || m_height <= 0; }

    std::size_t pixelCount() const {
        return static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    }

    std::size_t stride() const { return static_cast<std::size_t>(m_width) * kBytesPerPixel; }
    std::size_t byteSize() const { return m_pixels.size(); }

    std::uint8_t*       data()       { return m_pixels.data(); }
    std::uint8_t const* data() const { return m_pixels.data(); }

    std::span<std::uint8_t>       span()       { return std::span(m_pixels); }
    std::span<std::uint8_t const> span() const { return std::span(m_pixels); }

    struct Pixel { std::uint8_t r, g, b, a; };
    Pixel at(int x, int y) const;
    void  setAt(int x, int y, Pixel p);

    std::uint8_t* atRef(int x, int y) {
        return m_pixels.data() + (static_cast<std::size_t>(y) * stride() + static_cast<std::size_t>(x) * kBytesPerPixel);
    }
    std::uint8_t const* atRef(int x, int y) const {
        return m_pixels.data() + (static_cast<std::size_t>(y) * stride() + static_cast<std::size_t>(x) * kBytesPerPixel);
    }

    void reset(int width, int height);
    void clear(Pixel color = {0, 0, 0, 0});
    ImageBuffer subRect(int x, int y, int w, int h) const;
    void blitOverwrite(int dstX, int dstY, ImageBuffer const& src);
    void rotateCCW90();
    void rotateCW90();
    ImageBuffer resizedBilinear(int width, int height) const;

    static geode::Result<ImageBuffer> loadFromFile(std::filesystem::path const& path);
    static geode::Result<ImageBuffer> loadFromMemory(std::span<std::uint8_t const> bytes);
    geode::Result<> saveToPng(std::filesystem::path const& path) const;
    geode::Result<std::vector<std::uint8_t>> encodeAsPng() const;

private:
    int m_width  = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_pixels;
};

}  // namespace paimon::texture_studio
