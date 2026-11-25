#include "FillRenderer.hpp"

#include "GradientRasterizer.hpp"
#include "../../texture-studio/engine/TintMath.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace tmath = paimon::texture_studio::tintmath;

namespace paimon::icon_maker {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Vanilla GD icon sprites have black outlines (lum=0), midtone body (~175-190),
// and specular highlights (~230-255). Mapping ~190 -> 1.0 preserves dark lines
// and shading WITHOUT blowing saturated colors into white.
constexpr float kMidLum = 192.f;

void shadeColor(float baseR, float baseG, float baseB,
                std::uint8_t srcR, std::uint8_t srcG, std::uint8_t srcB,
                bool keepLuminance, float& outR, float& outG, float& outB) {
    if (!keepLuminance) {
        outR = baseR;
        outG = baseG;
        outB = baseB;
        return;
    }
    float lum = tmath::luminance601(srcR, srcG, srcB);
    if (lum <= kMidLum) {
        float f = lum / kMidLum;
        outR = baseR * f;
        outG = baseG * f;
        outB = baseB * f;
    } else {
        // Gentle specular lift toward white instead of channel-clipping hue shifts.
        float lift = ((lum - kMidLum) / (255.f - kMidLum)) * 0.42f;
        outR = baseR + (255.f - baseR) * lift;
        outG = baseG + (255.f - baseG) * lift;
        outB = baseB + (255.f - baseB) * lift;
    }
}

void writePixel(std::uint8_t* p, float rF, float gF, float bF, int alpha) {
    p[0] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(rF, 0.f, 255.f))));
    p[1] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(gF, 0.f, 255.f))));
    p[2] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(bF, 0.f, 255.f))));
    p[3] = tmath::clampByte(alpha);
}

bool hasColorAdjust(ColorAdjustSpec const& a) {
    return std::fabs(a.hueDeg) > 0.5f ||
           std::fabs(a.saturation - 1.f) > 0.01f ||
           std::fabs(a.brightness) > 0.01f ||
           std::fabs(a.contrast - 1.f) > 0.01f;
}

void applyColorAdjust(float& r, float& g, float& b, ColorAdjustSpec const& a) {
    // Normalize 0..1
    float rf = std::clamp(r / 255.f, 0.f, 1.f);
    float gf = std::clamp(g / 255.f, 0.f, 1.f);
    float bf = std::clamp(b / 255.f, 0.f, 1.f);

    // Hue & saturation in HSV
    if (std::fabs(a.hueDeg) > 0.5f || std::fabs(a.saturation - 1.f) > 0.01f) {
        float cmax = std::max({rf, gf, bf});
        float cmin = std::min({rf, gf, bf});
        float delta = cmax - cmin;
        float h = 0.f;
        if (delta > 1e-5f) {
            if (cmax == rf)      h = 60.f * std::fmod((gf - bf) / delta, 6.f);
            else if (cmax == gf) h = 60.f * ((bf - rf) / delta + 2.f);
            else                 h = 60.f * ((rf - gf) / delta + 4.f);
            if (h < 0.f) h += 360.f;
        }
        float s = cmax > 1e-5f ? delta / cmax : 0.f;
        float v = cmax;

        h = std::fmod(h + a.hueDeg, 360.f);
        if (h < 0.f) h += 360.f;
        s = std::clamp(s * a.saturation, 0.f, 1.f);

        float c = v * s;
        float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
        float m = v - c;
        if (h < 60.f)       { rf = c; gf = x; bf = 0.f; }
        else if (h < 120.f) { rf = x; gf = c; bf = 0.f; }
        else if (h < 180.f) { rf = 0.f; gf = c; bf = x; }
        else if (h < 240.f) { rf = 0.f; gf = x; bf = c; }
        else if (h < 300.f) { rf = x; gf = 0.f; bf = c; }
        else                { rf = c; gf = 0.f; bf = x; }
        rf += m; gf += m; bf += m;
    }

    // Contrast & brightness
    rf = (rf - 0.5f) * a.contrast + 0.5f + a.brightness;
    gf = (gf - 0.5f) * a.contrast + 0.5f + a.brightness;
    bf = (bf - 0.5f) * a.contrast + 0.5f + a.brightness;

    r = std::clamp(rf, 0.f, 1.f) * 255.f;
    g = std::clamp(gf, 0.f, 1.f) * 255.f;
    b = std::clamp(bf, 0.f, 1.f) * 255.f;
}

// Smooth anti-aliased procedural pattern mix factor (0 = secondary, 1 = primary).
float patternMaskAt(PatternSpec const& pat, float lx, float ly, float period) {
    float rad = pat.angleDeg * (kPi / 180.f);
    float cs = std::cos(rad);
    float sn = std::sin(rad);
    float u = (lx * cs + ly * sn) / std::max(period, 2.f);
    float v = (-lx * sn + ly * cs) / std::max(period, 2.f);
    float th = std::clamp(pat.thickness, 0.08f, 0.92f);

    auto frac = [](float val) {
        float f = val - std::floor(val);
        return f < 0.f ? f + 1.f : f;
    };
    auto smoothStep = [](float edge0, float edge1, float x) {
        float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-5f), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    float aa = 1.2f / std::max(period, 2.f);

    switch (pat.kind) {
        case PatternKind::Stripes: {
            float f = frac(u);
            float d = std::fabs(f - 0.5f) * 2.f;
            return 1.f - smoothStep(th - aa, th + aa, d);
        }
        case PatternKind::Diagonal: {
            float f = frac(u + v);
            float d = std::fabs(f - 0.5f) * 2.f;
            return 1.f - smoothStep(th - aa, th + aa, d);
        }
        case PatternKind::Checker: {
            int ix = static_cast<int>(std::floor(u));
            int iy = static_cast<int>(std::floor(v));
            return ((ix + iy) & 1) == 0 ? 1.f : 0.f;
        }
        case PatternKind::Dots: {
            float fx = frac(u) - 0.5f;
            float fy = frac(v) - 0.5f;
            float r = std::sqrt(fx * fx + fy * fy);
            float radDot = th * 0.48f;
            return 1.f - smoothStep(radDot - aa, radDot + aa, r);
        }
        case PatternKind::Grid: {
            float fx = std::fabs(frac(u) - 0.5f) * 2.f;
            float fy = std::fabs(frac(v) - 0.5f) * 2.f;
            float line = 1.f - th * 0.45f;
            float gx = smoothStep(line - aa, line + aa, fx);
            float gy = smoothStep(line - aa, line + aa, fy);
            return std::max(gx, gy);
        }
        case PatternKind::Chevron: {
            float tri = std::fabs(frac(u) - 0.5f) * 2.f;
            float f = frac(v + tri * 0.5f);
            float d = std::fabs(f - 0.5f) * 2.f;
            return 1.f - smoothStep(th - aa, th + aa, d);
        }
        case PatternKind::Carbon: {
            float fx = frac(u);
            float fy = frac(v);
            int cell = (static_cast<int>(std::floor(u)) + static_cast<int>(std::floor(v))) & 1;
            float weave = cell == 0
                ? std::sin(fx * kPi) * (0.65f + 0.35f * std::sin(fy * kPi))
                : std::sin(fy * kPi) * (0.45f + 0.35f * std::sin(fx * kPi));
            return std::clamp(weave, 0.f, 1.f);
        }
        case PatternKind::Concentric: {
            float r = std::sqrt(u * u + v * v);
            float f = frac(r);
            float d = std::fabs(f - 0.5f) * 2.f;
            return 1.f - smoothStep(th - aa, th + aa, d);
        }
    }
    return 1.f;
}

// bilinear sample with optional wrap (tile). returns straight-alpha rgba.
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
    }
}

std::vector<float> chamferDistance(ts::ImageBuffer const& shape, std::uint8_t seedThreshold) {
    int const w = shape.width();
    int const h = shape.height();
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
    return dist;
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
                                            int layerOpacity,
                                            float canvasScale) {
    if (shape.empty() || !outline.enabled || outline.width <= 0.f) {
        return ts::ImageBuffer();
    }

    int const w = shape.width();
    int const h = shape.height();
    float const scale = std::max(canvasScale, 0.1f);
    float const radius = std::min(outline.width * scale, static_cast<float>(std::min(w, h)) / 2.f);

    int const opacity = std::clamp(layerOpacity, 0, 255);
    if (opacity <= 0) return ts::ImageBuffer();
    auto const seedThreshold = static_cast<std::uint8_t>(std::max(8, opacity / 2));

    auto dist = chamferDistance(shape, seedThreshold);

    ts::ImageBuffer out(w, h);
    auto* dst = out.data();
    float const colorA = static_cast<float>(outline.color.a) / 255.f
                       * static_cast<float>(opacity) / 255.f;

    for (std::size_t idx = 0; idx < dist.size(); ++idx) {
        float d = dist[idx];
        if (d > radius + 0.5f) continue;
        float coverage = std::clamp(radius + 0.5f - d, 0.f, 1.f);
        int alpha = static_cast<int>(std::lround(255.f * coverage * colorA));
        if (alpha <= 0) continue;
        writePixel(dst + idx * 4, outline.color.r, outline.color.g, outline.color.b, alpha);
    }
    return out;
}

ts::ImageBuffer FillRenderer::renderShadow(ts::ImageBuffer const& shape,
                                           ShadowSpec const& shadow,
                                           int layerOpacity,
                                           float canvasScale) {
    if (shape.empty() || !shadow.enabled) return ts::ImageBuffer();
    int const opacity = std::clamp(layerOpacity, 0, 255);
    if (opacity <= 0) return ts::ImageBuffer();

    int const w = shape.width();
    int const h = shape.height();
    float const scale = std::max(canvasScale, 0.1f);
    float const radius = std::max(1.f, shadow.blur * scale);
    int const offX = static_cast<int>(std::lround(shadow.offsetX * scale));
    int const offY = static_cast<int>(std::lround(shadow.offsetY * scale));

    auto const seedThreshold = static_cast<std::uint8_t>(std::max(8, opacity / 2));
    auto dist = chamferDistance(shape, seedThreshold);

    ts::ImageBuffer out(w, h);
    auto* dst = out.data();
    float const colorA = static_cast<float>(shadow.color.a) / 255.f
                       * static_cast<float>(opacity) / 255.f;

    for (int y = 0; y < h; ++y) {
        int const sy = y - offY;
        if (sy < 0 || sy >= h) continue;
        for (int x = 0; x < w; ++x) {
            int const sx = x - offX;
            if (sx < 0 || sx >= w) continue;
            float const d = dist[static_cast<std::size_t>(sy) * w + sx];
            if (d > radius) continue;
            float const t = 1.f - d / radius;
            // Soft quadratic falloff for smooth outer glow / drop shadow.
            float const falloff = t * t;
            int const alpha = static_cast<int>(std::lround(255.f * falloff * colorA));
            if (alpha <= 0) continue;
            auto* px = dst + (static_cast<std::size_t>(y) * w + x) * 4;
            writePixel(px, shadow.color.r, shadow.color.g, shadow.color.b, alpha);
        }
    }
    return out;
}

void FillRenderer::compositeBlend(ts::ImageBuffer& base,
                                  ts::ImageBuffer const& top,
                                  BlendMode mode) {
    if (mode == BlendMode::Normal) {
        ts::SpritePreviewRenderer::compositeOver(base, top);
        return;
    }
    if (base.empty() || top.empty()) return;
    if (base.width() != top.width() || base.height() != top.height()) return;

    auto* b = base.data();
    auto const* t = top.data();
    std::size_t const n = base.pixelCount();

    for (std::size_t i = 0; i < n; ++i) {
        auto* bp = b + i * 4;
        auto const* tp = t + i * 4;
        int const ta = tp[3];
        if (ta <= 0) continue;

        float const topA = static_cast<float>(ta) / 255.f;
        float const baseA = static_cast<float>(bp[3]) / 255.f;

        if (baseA <= 0.f) {
            bp[0] = tp[0]; bp[1] = tp[1]; bp[2] = tp[2]; bp[3] = tp[3];
            continue;
        }

        float const outA = mode == BlendMode::Add
            ? std::min(1.f, baseA + topA * (1.f - baseA * 0.5f))
            : topA + baseA * (1.f - topA);
        if (outA <= 0.f) continue;

        for (int c = 0; c < 3; ++c) {
            float const bc = static_cast<float>(bp[c]) / 255.f;
            float const tc = static_cast<float>(tp[c]) / 255.f;
            float blended = tc;
            switch (mode) {
                case BlendMode::Add:
                    blended = std::min(1.f, bc + tc);
                    break;
                case BlendMode::Multiply:
                    blended = bc * tc;
                    break;
                case BlendMode::Screen:
                    blended = 1.f - (1.f - bc) * (1.f - tc);
                    break;
                default:
                    blended = tc;
                    break;
            }
            float const mixed = (blended * topA * baseA +
                                 tc * topA * (1.f - baseA) +
                                 bc * baseA * (1.f - topA)) / outA;
            bp[c] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(mixed, 0.f, 1.f) * 255.f)));
        }
        bp[3] = tmath::clampByte(static_cast<int>(std::lround(std::clamp(outA, 0.f, 1.f) * 255.f)));
    }
}

geode::Result<ts::ImageBuffer> FillRenderer::apply(ts::ImageBuffer const& shape,
                                                   FillSpec const& fill,
                                                   std::filesystem::path const& imagesDir,
                                                   float canvasScale) {
    if (shape.empty()) return Ok(ts::ImageBuffer());

    int w = shape.width();
    int h = shape.height();
    ts::ImageBuffer out(w, h);

    int bx = 0, by = 0, bw = 0, bh = 0;
    if (!alphaBounds(shape, bx, by, bw, bh)) {
        return Ok(std::move(out));
    }

    bool const keepLuma = fill.keepLuminance;
    bool const doAdjust = !fill.chroma && hasColorAdjust(fill.adjust);
    bool const doGloss = !fill.chroma && fill.gloss && fill.glossStrength > 0.01f;
    float const glossStr = std::clamp(fill.glossStrength, 0.f, 1.f);
    auto const* src = shape.data();
    auto* dst = out.data();

    auto finalizePixel = [&](int x, int y, float r, float g, float b, int alpha) {
        if (doAdjust) applyColorAdjust(r, g, b, fill.adjust);
        if (doGloss) {
            // Crisp Geometry Dash style glossy top highlight + soft bottom rim shade.
            float const ny = (static_cast<float>(y - by) + 0.5f) / static_cast<float>(std::max(bh, 1));
            float const nx = (static_cast<float>(x - bx) + 0.5f) / static_cast<float>(std::max(bw, 1));
            float const curve = 0.44f - 0.14f * std::pow((nx - 0.5f) * 2.f, 2.f);
            if (ny < curve) {
                float const fade = 1.f - (ny / std::max(curve, 0.05f)) * 0.55f;
                float const lift = fade * glossStr * 0.65f;
                r += (255.f - r) * lift;
                g += (255.f - g) * lift;
                b += (255.f - b) * lift;
            } else if (ny > 0.78f) {
                float const shade = ((ny - 0.78f) / 0.22f) * glossStr * 0.22f;
                r *= (1.f - shade);
                g *= (1.f - shade);
                b *= (1.f - shade);
            }
        }
        writePixel(dst + (static_cast<std::size_t>(y) * w + x) * 4, r, g, b, alpha);
    };

    auto paintLoop = [&](auto&& colorAt) {
        for (int y = by; y < by + bh; ++y) {
            for (int x = bx; x < bx + bw; ++x) {
                auto const* sp = src + (static_cast<std::size_t>(y) * w + x) * 4;
                if (sp[3] == 0) continue;
                auto c = colorAt(x, y);
                float r, g, b;
                shadeColor(c.r, c.g, c.b, sp[0], sp[1], sp[2], keepLuma, r, g, b);
                finalizePixel(x, y, r, g, b, sp[3] * c.a / 255);
            }
        }
    };

    // Original colors mode: preserves imported RGB colors untouched (with optional adjust/gloss).
    if (fill.type == FillType::Original && !fill.chroma) {
        for (int y = by; y < by + bh; ++y) {
            for (int x = bx; x < bx + bw; ++x) {
                auto const* sp = src + (static_cast<std::size_t>(y) * w + x) * 4;
                if (sp[3] == 0) continue;
                finalizePixel(x, y, sp[0], sp[1], sp[2], sp[3]);
            }
        }
        return Ok(std::move(out));
    }

    // chroma renders plain white; the runtime ticker cycles the hue live.
    if (fill.type == FillType::Flat || fill.chroma) {
        cocos2d::ccColor4B flat = fill.chroma
            ? cocos2d::ccColor4B{255, 255, 255, 255} : fill.flat;
        paintLoop([&](int, int) { return flat; });
        return Ok(std::move(out));
    }

    if (fill.type == FillType::Gradient) {
        paintLoop([&](int x, int y) {
            float t = GradientRasterizer::paramAt(fill.gradient,
                static_cast<float>(x - bx) + 0.5f,
                static_cast<float>(y - by) + 0.5f,
                static_cast<float>(bw), static_cast<float>(bh));
            return GradientRasterizer::sample(fill.gradient, t);
        });
        return Ok(std::move(out));
    }

    if (fill.type == FillType::Pattern) {
        float const period = 24.f * std::clamp(fill.pattern.scale, 0.2f, 4.f) * std::max(canvasScale, 0.1f);
        float const cx = static_cast<float>(bx) + static_cast<float>(bw) * 0.5f;
        float const cy = static_cast<float>(by) + static_cast<float>(bh) * 0.5f;
        auto const& p1 = fill.pattern.primary;
        auto const& p2 = fill.pattern.secondary;
        paintLoop([&](int x, int y) -> cocos2d::ccColor4B {
            float const m = patternMaskAt(fill.pattern,
                static_cast<float>(x) + 0.5f - cx,
                static_cast<float>(y) + 0.5f - cy,
                period);
            return {
                tmath::clampByte(static_cast<int>(std::lround(p2.r + (p1.r - p2.r) * m))),
                tmath::clampByte(static_cast<int>(std::lround(p2.g + (p1.g - p2.g) * m))),
                tmath::clampByte(static_cast<int>(std::lround(p2.b + (p1.b - p2.b) * m))),
                tmath::clampByte(static_cast<int>(std::lround(p2.a + (p1.a - p2.a) * m))),
            };
        });
        return Ok(std::move(out));
    }

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
            sx = sy = std::max(canvasScale, 0.1f);
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

            float r, g, b;
            shadeColor(acc[0], acc[1], acc[2], sp[0], sp[1], sp[2], keepLuma, r, g, b);
            int alpha = static_cast<int>(std::lround(
                static_cast<float>(sp[3]) * (acc[3] / 255.f) * opacityMul));
            finalizePixel(x, y, r, g, b, alpha);
        }
    }
    return Ok(std::move(out));
}

}  // namespace paimon::icon_maker
