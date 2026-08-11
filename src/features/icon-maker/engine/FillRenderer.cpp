#include "FillRenderer.hpp"

#include "GradientRasterizer.hpp"
#include "../../texture-studio/engine/TintMath.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace tmath = paimon::texture_studio::tintmath;

namespace paimon::icon_maker {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Matches texture-studio's default tint brightness so shading response feels
// identical across both features.
constexpr float kBrightness = 160.f;

float lumFactor(std::uint8_t r, std::uint8_t g, std::uint8_t b, bool keepLuminance) {
    if (!keepLuminance) return 1.f;
    return tmath::luminance601(r, g, b) / kBrightness;
}

void writePixel(std::uint8_t* p, float rF, float gF, float bF, int alpha) {
    p[0] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(rF, 0.f, 255.f))));
    p[1] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(gF, 0.f, 255.f))));
    p[2] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(bF, 0.f, 255.f))));
    p[3] = tmath::clampByte(alpha);
}

// Bilinear sample with optional wrap (Tile). Returns straight-alpha RGBA.
void sampleImage(ts::ImageBuffer const& img, float sxf, float syf, bool wrap,
                 float out[4]) {
    int iw = img.width();
    int ih = img.height();
    float fx = sxf - 0.5f;
    float fy = syf - 0.5f;
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    float tx = fx - static_cast<float>(x0);
    float ty = fy - static_cast<float>(y0);

    auto const* src = img.data();
    auto tap = [&](int px, int py, float weight) {
        if (weight <= 0.f) return;
        if (wrap) {
            px = ((px % iw) + iw) % iw;
            py = ((py % ih) + ih) % ih;
        } else if (px < 0 || py < 0 || px >= iw || py >= ih) {
            return;
        }
        auto const* p = src + (static_cast<std::size_t>(py) * iw + px) * 4;
        float a = static_cast<float>(p[3]) * weight;
        out[0] += static_cast<float>(p[0]) * a;
        out[1] += static_cast<float>(p[1]) * a;
        out[2] += static_cast<float>(p[2]) * a;
        out[3] += a;
    };

    tap(x0,     y0,     (1.f - tx) * (1.f - ty));
    tap(x0 + 1, y0,     tx * (1.f - ty));
    tap(x0,     y0 + 1, (1.f - tx) * ty);
    tap(x0 + 1, y0 + 1, tx * ty);

    if (out[3] > 0.f) {
        out[0] /= out[3];
        out[1] /= out[3];
        out[2] /= out[3];
        out[3] /= 1.f;  // out[3] stays as accumulated alpha (0..255)
    }
}

}  // anonymous namespace

bool FillRenderer::alphaBounds(ts::ImageBuffer const& buffer,
                               int& outX, int& outY, int& outW, int& outH) {
    int w = buffer.width();
    int h = buffer.height();
    int minX = w, minY = h, maxX = -1, maxY = -1;
    auto const* data = buffer.data();
    for (int y = 0; y < h; ++y) {
        auto const* row = data + static_cast<std::size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            if (row[x * 4 + 3] > 0) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < minX) return false;
    outX = minX;
    outY = minY;
    outW = maxX - minX + 1;
    outH = maxY - minY + 1;
    return true;
}

ts::ImageBuffer FillRenderer::renderOutline(ts::ImageBuffer const& shape,
                                            OutlineSpec const& outline,
                                            int layerOpacity) {
    if (shape.empty() || !outline.enabled || outline.width <= 0.f) {
        return ts::ImageBuffer();
    }

    int const w = shape.width();
    int const h = shape.height();
    float const radius = std::min(outline.width, static_cast<float>(std::min(w, h)) / 2.f);

    // The piece opacity is already multiplied into `shape`, so the silhouette
    // threshold has to scale with it or a faded layer would grow no outline.
    int const opacity = std::clamp(layerOpacity, 0, 255);
    if (opacity <= 0) return ts::ImageBuffer();
    auto const seedThreshold = static_cast<std::uint8_t>(
        std::max(8, opacity / 2));

    // Chamfer distance transform: two sweeps, O(w*h), close enough to a true
    // euclidean distance that the contour reads as round at icon sizes.
    constexpr float kOrtho = 1.f;
    constexpr float kDiag = 1.41421356f;
    float const unreached = static_cast<float>(w + h) * 2.f;

    std::vector<float> dist(static_cast<std::size_t>(w) * h, unreached);
    auto const* src = shape.data();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto idx = static_cast<std::size_t>(y) * w + x;
            if (src[idx * 4 + 3] >= seedThreshold) dist[idx] = 0.f;
        }
    }

    auto relax = [&](std::size_t target, std::size_t from, float cost) {
        float candidate = dist[from] + cost;
        if (candidate < dist[target]) dist[target] = candidate;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto idx = static_cast<std::size_t>(y) * w + x;
            if (dist[idx] == 0.f) continue;
            if (x > 0)                 relax(idx, idx - 1, kOrtho);
            if (y > 0)                 relax(idx, idx - w, kOrtho);
            if (x > 0 && y > 0)        relax(idx, idx - w - 1, kDiag);
            if (x + 1 < w && y > 0)    relax(idx, idx - w + 1, kDiag);
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            auto idx = static_cast<std::size_t>(y) * w + x;
            if (dist[idx] == 0.f) continue;
            if (x + 1 < w)                  relax(idx, idx + 1, kOrtho);
            if (y + 1 < h)                  relax(idx, idx + w, kOrtho);
            if (x + 1 < w && y + 1 < h)     relax(idx, idx + w + 1, kDiag);
            if (x > 0 && y + 1 < h)         relax(idx, idx + w - 1, kDiag);
        }
    }

    ts::ImageBuffer out(w, h);
    auto* dst = out.data();
    float const colorA = static_cast<float>(outline.color.a) / 255.f
                       * static_cast<float>(opacity) / 255.f;

    for (std::size_t idx = 0; idx < dist.size(); ++idx) {
        float d = dist[idx];
        if (d > radius + 0.5f) continue;
        // One-pixel feather at the outer edge so the contour is not stair-stepped.
        float coverage = std::clamp(radius + 0.5f - d, 0.f, 1.f);
        int alpha = static_cast<int>(std::lround(255.f * coverage * colorA));
        if (alpha <= 0) continue;
        writePixel(dst + idx * 4, outline.color.r, outline.color.g, outline.color.b, alpha);
    }
    return out;
}

geode::Result<ts::ImageBuffer> FillRenderer::apply(ts::ImageBuffer const& shape,
                                                   FillSpec const& fill,
                                                   std::filesystem::path const& imagesDir) {
    if (shape.empty()) return Ok(ts::ImageBuffer());

    int w = shape.width();
    int h = shape.height();
    ts::ImageBuffer out(w, h);

    int bx = 0, by = 0, bw = 0, bh = 0;
    if (!alphaBounds(shape, bx, by, bw, bh)) {
        return Ok(std::move(out));  // fully transparent shape: nothing to paint
    }

    bool const keepLuma = fill.keepLuminance;
    auto const* src = shape.data();
    auto* dst = out.data();

    // Chroma renders plain white; the runtime ticker cycles the hue live.
    if (fill.type == FillType::Flat || fill.chroma) {
        cocos2d::ccColor4B flat = fill.chroma
            ? cocos2d::ccColor4B{255, 255, 255, 255} : fill.flat;
        for (int y = by; y < by + bh; ++y) {
            for (int x = bx; x < bx + bw; ++x) {
                auto const* sp = src + (static_cast<std::size_t>(y) * w + x) * 4;
                if (sp[3] == 0) continue;
                float f = lumFactor(sp[0], sp[1], sp[2], keepLuma);
                writePixel(dst + (static_cast<std::size_t>(y) * w + x) * 4,
                    flat.r * f, flat.g * f, flat.b * f,
                    sp[3] * flat.a / 255);
            }
        }
        return Ok(std::move(out));
    }

    if (fill.type == FillType::Gradient) {
        for (int y = by; y < by + bh; ++y) {
            for (int x = bx; x < bx + bw; ++x) {
                auto const* sp = src + (static_cast<std::size_t>(y) * w + x) * 4;
                if (sp[3] == 0) continue;
                float t = GradientRasterizer::paramAt(fill.gradient,
                    static_cast<float>(x - bx) + 0.5f,
                    static_cast<float>(y - by) + 0.5f,
                    static_cast<float>(bw), static_cast<float>(bh));
                auto c = GradientRasterizer::sample(fill.gradient, t);
                float f = lumFactor(sp[0], sp[1], sp[2], keepLuma);
                writePixel(dst + (static_cast<std::size_t>(y) * w + x) * 4,
                    c.r * f, c.g * f, c.b * f,
                    sp[3] * c.a / 255);
            }
        }
        return Ok(std::move(out));
    }

    // Image fill: inverse-map every shape pixel into the fill image, honoring
    // fit mode (incl. Tile = wrap), scale, offset and rotation over the
    // shape's bounding box.
    if (fill.image.file.empty()) {
        return Err("El relleno de imagen no tiene archivo");
    }
    auto loaded = ts::ImageBuffer::loadFromFile(imagesDir / fill.image.file);
    if (!loaded) {
        return Err("No se pudo cargar '{}': {}", fill.image.file, loaded.unwrapErr());
    }
    auto img = loaded.unwrap();
    if (img.empty()) return Err("Imagen de relleno vacia");

    float imgW = static_cast<float>(img.width());
    float imgH = static_cast<float>(img.height());
    float fw = static_cast<float>(bw);
    float fh = static_cast<float>(bh);

    float sx = 1.f, sy = 1.f;
    switch (fill.image.fit) {
        case FillFitMode::Fill:
            sx = sy = std::max(fw / imgW, fh / imgH);
            break;
        case FillFitMode::Stretch:
            sx = fw / imgW;
            sy = fh / imgH;
            break;
        case FillFitMode::Tile:
            sx = sy = 1.f;  // native pixel size; user scale drives tiling density
            break;
        case FillFitMode::Fit:
        default:
            sx = sy = std::min(fw / imgW, fh / imgH);
            break;
    }
    float userScale = std::clamp(fill.image.scale, 0.05f, 20.f);
    sx *= userScale;
    sy *= userScale;
    if (sx <= 0.f || sy <= 0.f) return Ok(std::move(out));

    bool const wrap = fill.image.fit == FillFitMode::Tile;

    float cx = fw * 0.5f + std::clamp(fill.image.offsetX, -1.f, 1.f) * fw * 0.5f;
    float cy = fh * 0.5f - std::clamp(fill.image.offsetY, -1.f, 1.f) * fh * 0.5f;

    float rad = fill.image.rotationDeg * (kPi / 180.f);
    float cosR = std::cos(rad);
    float sinR = std::sin(rad);
    float opacityMul = std::clamp(fill.image.opacity, 0, 255) / 255.f;

    for (int y = by; y < by + bh; ++y) {
        for (int x = bx; x < bx + bw; ++x) {
            auto const* sp = src + (static_cast<std::size_t>(y) * w + x) * 4;
            if (sp[3] == 0) continue;

            float dx = (static_cast<float>(x - bx) + 0.5f) - cx;
            float dy = (static_cast<float>(y - by) + 0.5f) - cy;

            float ux = dx * cosR + dy * sinR;
            float uy = -dx * sinR + dy * cosR;

            float sxf = ux / sx + imgW * 0.5f;
            float syf = uy / sy + imgH * 0.5f;

            float acc[4] = {0.f, 0.f, 0.f, 0.f};
            sampleImage(img, sxf, syf, wrap, acc);
            if (acc[3] <= 0.f) continue;

            float f = lumFactor(sp[0], sp[1], sp[2], keepLuma);
            int alpha = static_cast<int>(std::lround(
                static_cast<float>(sp[3]) * (acc[3] / 255.f) * opacityMul));
            writePixel(dst + (static_cast<std::size_t>(y) * w + x) * 4,
                acc[0] * f, acc[1] * f, acc[2] * f, alpha);
        }
    }
    return Ok(std::move(out));
}

}  // namespace paimon::icon_maker
