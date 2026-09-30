#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace paimon::texture_studio::packgen {

class FrameImage {
public:
    static constexpr std::size_t kBpp = 4;

    FrameImage() = default;
    FrameImage(int w, int h) { reset(w, h); }
    FrameImage(int w, int h, std::uint8_t const* rgba) {
        reset(w, h);
        if (rgba && !empty()) {
            std::memcpy(m_px.data(), rgba, m_px.size());
        }
    }

    int width() const { return m_w; }
    int height() const { return m_h; }
    bool empty() const { return m_w <= 0 || m_h <= 0 || m_px.empty(); }
    std::size_t pixelCount() const {
        return static_cast<std::size_t>(m_w) * static_cast<std::size_t>(m_h);
    }
    std::size_t byteSize() const { return m_px.size(); }
    std::size_t stride() const {
        return static_cast<std::size_t>(m_w) * kBpp;
    }

    std::uint8_t* data() { return m_px.data(); }
    std::uint8_t const* data() const { return m_px.data(); }

    std::uint8_t* row(int y) { return m_px.data() + static_cast<std::size_t>(y) * stride(); }
    std::uint8_t const* row(int y) const {
        return m_px.data() + static_cast<std::size_t>(y) * stride();
    }

    struct Pixel { std::uint8_t r, g, b, a; };

    Pixel at(int x, int y) const {
        if (empty() || x < 0 || y < 0 || x >= m_w || y >= m_h) return {0, 0, 0, 0};
        auto const* p = row(y) + static_cast<std::size_t>(x) * kBpp;
        return {p[0], p[1], p[2], p[3]};
    }
    void setAt(int x, int y, Pixel p) {
        if (empty() || x < 0 || y < 0 || x >= m_w || y >= m_h) return;
        auto* d = row(y) + static_cast<std::size_t>(x) * kBpp;
        d[0] = p.r; d[1] = p.g; d[2] = p.b; d[3] = p.a;
    }

    void reset(int w, int h) {
        auto width = static_cast<std::size_t>(std::max(0, w));
        auto height = static_cast<std::size_t>(std::max(0, h));
        if (width && height > m_px.max_size() / kBpp / width) {
            throw std::length_error("frame image dimensions exceed buffer capacity");
        }
        m_px.assign(width * height * kBpp, 0);
        m_w = static_cast<int>(width);
        m_h = static_cast<int>(height);
    }

    void clear(Pixel c = {0, 0, 0, 0}) {
        if (empty()) return;
        if (c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0) {
            std::memset(m_px.data(), 0, m_px.size());
            return;
        }
        std::uint8_t const packed[] = {c.r, c.g, c.b, c.a};
        auto* d = m_px.data();
        for (std::size_t i = 0, n = pixelCount(); i < n; ++i) {
            std::memcpy(d + i * kBpp, packed, sizeof(packed));
        }
    }

    // copy of the rect; out-of-bounds source stays transparent.
    FrameImage subRect(int x, int y, int w, int h) const {
        FrameImage out(w, h);
        if (out.empty() || empty()) return out;
        int sx0 = std::max(x, 0), sy0 = std::max(y, 0);
        int sx1 = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(x) + w, 0, m_w));
        int sy1 = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(y) + h, 0, m_h));
        if (sx0 >= sx1 || sy0 >= sy1) return out;
        std::size_t rowBytes = static_cast<std::size_t>(sx1 - sx0) * kBpp;
        for (int sy = sy0; sy < sy1; ++sy) {
            std::memcpy(out.row(sy0 - y + (sy - sy0)) + static_cast<std::size_t>(sx0 - x) * kBpp,
                        row(sy) + static_cast<std::size_t>(sx0) * kBpp, rowBytes);
        }
        return out;
    }

    void blitOverwrite(int dx, int dy, FrameImage const& src) {
        if (src.empty() || empty()) return;
        if (&src == this) {
            auto copy = src;
            blitOverwrite(dx, dy, copy);
            return;
        }
        int sx0 = static_cast<int>(std::clamp<std::int64_t>(-static_cast<std::int64_t>(dx), 0, src.m_w));
        int sy0 = static_cast<int>(std::clamp<std::int64_t>(-static_cast<std::int64_t>(dy), 0, src.m_h));
        int sx1 = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(m_w) - dx, 0, src.m_w));
        int sy1 = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(m_h) - dy, 0, src.m_h));
        if (sx0 >= sx1 || sy0 >= sy1) return;
        dx += sx0;
        dy += sy0;
        std::size_t rowBytes = static_cast<std::size_t>(sx1 - sx0) * kBpp;
        for (int sy = sy0; sy < sy1; ++sy) {
            std::memcpy(row(dy + (sy - sy0)) + static_cast<std::size_t>(dx) * kBpp,
                        src.row(sy) + static_cast<std::size_t>(sx0) * kBpp, rowBytes);
        }
    }

    void rotateCW90() {
        if (empty()) return;
        FrameImage rot(m_h, m_w);
        for (int y = 0; y < m_h; ++y) {
            for (int x = 0; x < m_w; ++x) {
                std::memcpy(rot.row(x) + static_cast<std::size_t>(m_h - 1 - y) * kBpp,
                            row(y) + static_cast<std::size_t>(x) * kBpp, kBpp);
            }
        }
        *this = std::move(rot);
    }

    // 2x2 box-average halve (matches sheettinter's 0.5 path).
    FrameImage boxHalf() const {
        if (empty()) return {};
        int nw = std::max(1, m_w / 2), nh = std::max(1, m_h / 2);
        FrameImage out(nw, nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                int r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = x * 2 + dx, sy = y * 2 + dy;
                        if (sx < m_w && sy < m_h) {
                            auto const* p = row(sy) + static_cast<std::size_t>(sx) * kBpp;
                            r += p[0]; g += p[1]; b += p[2]; a += p[3];
                            ++n;
                        }
                    }
                }
                if (n > 0) {
                    out.setAt(x, y, {static_cast<std::uint8_t>(r / n),
                                     static_cast<std::uint8_t>(g / n),
                                     static_cast<std::uint8_t>(b / n),
                                     static_cast<std::uint8_t>(a / n)});
                }
            }
        }
        return out;
    }

    bool hasInk() const {
        for (std::size_t i = 0, n = pixelCount(); i < n; ++i) {
            if (m_px[i * kBpp + 3] != 0) return true;
        }
        return false;
    }

    bool operator==(FrameImage const& o) const {
        return m_w == o.m_w && m_h == o.m_h && m_px == o.m_px;
    }
    bool operator!=(FrameImage const& o) const { return !(*this == o); }

private:
    int m_w = 0;
    int m_h = 0;
    std::vector<std::uint8_t> m_px;
};

}  // namespace paimon::texture_studio::packgen
