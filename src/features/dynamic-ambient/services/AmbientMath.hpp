#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace paimon::ambient {

struct AmbientSettings {
    bool enabled = false;
    float intensity = 0.85f;       // 0..1.5
    float rimStrength = 0.85f;     // 0..1.5
    float shadowStrength = 0.65f;  // 0..1.0
    float tintStrength = 0.70f;    // 0..1.0
    float paletteStrength = 0.78f; // 0..1.0 (real-time background-adaptive palette)
    bool affectButtons = true;
    bool affectPanels = true;
    bool affectAssets = true;
};

struct AmbientRgb {
    float r = 1.f;
    float g = 1.f;
    float b = 1.f;
};

struct AmbientLightState {
    static constexpr int kZoneW = 4;
    static constexpr int kZoneH = 3;

    // Normalized screen UV [0..1] of the dominant light source (0,0 bottom-left, 1,1 top-right).
    float lightX = 0.18f;
    float lightY = 0.84f;
    // Normalized fallback global direction toward the light.
    float globalDirX = -0.55f;
    float globalDirY = 0.83f;
    // Key light color (warm sun / neon / moon) and ambient shadow color.
    AmbientRgb keyColor{1.0f, 0.93f, 0.76f};
    AmbientRgb shadowColor{0.28f, 0.24f, 0.44f};
    // Extracted multi-role scene palette for real-time adaptive button & asset recoloring.
    AmbientRgb palettePrimary{0.48f, 0.88f, 0.30f};
    AmbientRgb paletteSecondary{0.26f, 0.82f, 0.96f};
    AmbientRgb paletteSurface{0.34f, 0.24f, 0.18f};
    AmbientRgb paletteAccent{1.0f, 0.80f, 0.32f};
    // Coarse 4x3 spatial color field for local environmental bounce lighting per UI node.
    std::array<AmbientRgb, kZoneW * kZoneH> zoneColors{};
    // 0 = flat overcast, 1 = strong directional light source.
    float directionality = 0.55f;
    float averageLuma = 0.45f;
    bool fromCapture = false;
    bool hasZones = false;
};

struct AmbientNodeLight {
    float dirX = -0.55f;
    float dirY = 0.83f;
    float proximityBoost = 1.0f;
};

inline float clampAmbientFloat(float value, float fallback, float lo, float hi) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, lo, hi);
}

inline AmbientSettings sanitizeAmbientSettings(AmbientSettings s) {
    AmbientSettings const d;
    s.intensity = clampAmbientFloat(s.intensity, d.intensity, 0.f, 1.5f);
    s.rimStrength = clampAmbientFloat(s.rimStrength, d.rimStrength, 0.f, 1.5f);
    s.shadowStrength = clampAmbientFloat(s.shadowStrength, d.shadowStrength, 0.f, 1.0f);
    s.tintStrength = clampAmbientFloat(s.tintStrength, d.tintStrength, 0.f, 1.0f);
    s.paletteStrength = clampAmbientFloat(s.paletteStrength, d.paletteStrength, 0.f, 1.0f);
    return s;
}

inline float rgbLuminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

struct AmbientHsl {
    float h = 0.f; // degrees [0..360)
    float s = 0.f; // [0..1]
    float l = 0.f; // [0..1]
};

inline AmbientHsl rgbToHsl(float r, float g, float b) {
    float const maxC = std::max({r, g, b});
    float const minC = std::min({r, g, b});
    AmbientHsl out;
    out.l = 0.5f * (maxC + minC);
    float const d = maxC - minC;
    if (d <= 1e-5f) return out;
    out.s = (out.l > 0.5f) ? (d / std::max(1e-5f, 2.f - maxC - minC))
                           : (d / std::max(1e-5f, maxC + minC));
    float h = 0.f;
    if (maxC == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (maxC == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    out.h = h * 60.f;
    return out;
}

inline float hueChannel(float p, float q, float t) {
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

inline AmbientRgb hslToRgb(float hDeg, float s, float l) {
    float h = std::fmod(hDeg, 360.f);
    if (h < 0.f) h += 360.f;
    h /= 360.f;
    s = std::clamp(s, 0.f, 1.f);
    l = std::clamp(l, 0.f, 1.f);
    if (s <= 1e-4f) return {l, l, l};
    float const q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    float const p = 2.f * l - q;
    return {
        std::clamp(hueChannel(p, q, h + 1.f / 3.f), 0.f, 1.f),
        std::clamp(hueChannel(p, q, h), 0.f, 1.f),
        std::clamp(hueChannel(p, q, h - 1.f / 3.f), 0.f, 1.f)
    };
}

inline float circularHueDistanceDeg(float a, float b) {
    float diff = std::abs(std::fmod(a - b + 540.f, 360.f) - 180.f);
    return std::clamp(diff, 0.f, 180.f);
}

inline AmbientRgb shapeUiTone(AmbientRgb raw, float satMin, float satMax, float lumMin, float lumMax, float chromaPresence) {
    auto hsl = rgbToHsl(raw.r, raw.g, raw.b);
    float const targetSatMin = satMin * chromaPresence;
    float const targetSatMax = std::max(targetSatMin, satMax * std::max(0.28f, chromaPresence));
    hsl.s = std::clamp(std::max(hsl.s * 1.32f, targetSatMin), targetSatMin, targetSatMax);
    hsl.l = std::clamp(hsl.l, lumMin, lumMax);
    return hslToRgb(hsl.h, hsl.s, hsl.l);
}

inline AmbientRgb normalizeLightColor(float r, float g, float b, AmbientRgb fallback) {
    float const mx = std::max({r, g, b});
    if (!(mx > 0.02f) || !std::isfinite(mx)) return fallback;
    // Preserve hue vibrancy cleanly instead of washing bright hues out to flat white.
    float const inv = 1.f / mx;
    float nr = r * inv;
    float ng = g * inv;
    float nb = b * inv;
    float const avg = (nr + ng + nb) / 3.f;
    float const satBoost = 1.28f;
    nr = std::clamp(avg + (nr - avg) * satBoost, 0.f, 1.f);
    ng = std::clamp(avg + (ng - avg) * satBoost, 0.f, 1.f);
    nb = std::clamp(avg + (nb - avg) * satBoost, 0.f, 1.f);
    return {
        std::clamp(0.10f + 0.90f * nr, 0.f, 1.f),
        std::clamp(0.10f + 0.90f * ng, 0.f, 1.f),
        std::clamp(0.10f + 0.90f * nb, 0.f, 1.f)
    };
}

inline AmbientRgb normalizeShadowColor(float r, float g, float b, AmbientRgb fallback) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) return fallback;
    float const avg = (r + g + b) / 3.f;
    // Deepen and preserve chromatic saturation so shadows stay rich rather than muddy grey.
    float const satBoost = 1.35f;
    float const sr = std::clamp((avg + (r - avg) * satBoost) * 0.85f + 0.05f, 0.04f, 0.88f);
    float const sg = std::clamp((avg + (g - avg) * satBoost) * 0.85f + 0.05f, 0.04f, 0.88f);
    float const sb = std::clamp((avg + (b - avg) * satBoost) * 0.85f + 0.07f, 0.06f, 0.92f);
    return {sr, sg, sb};
}

inline AmbientRgb normalizeZoneColor(float r, float g, float b, AmbientRgb fallback) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) return fallback;
    float const mx = std::max({r, g, b});
    if (mx < 0.03f) {
        return {
            std::clamp(fallback.r * 0.25f + 0.04f, 0.03f, 0.45f),
            std::clamp(fallback.g * 0.25f + 0.04f, 0.03f, 0.45f),
            std::clamp(fallback.b * 0.25f + 0.05f, 0.04f, 0.48f)
        };
    }
    // Preserve true bright-vs-dark local contrast while giving dark corners rich chroma.
    float const lift = mx < 0.28f ? std::min(1.30f, 0.28f / std::max(mx, 0.08f)) : 1.0f;
    float const nr = std::clamp(r * lift, 0.f, 1.f);
    float const ng = std::clamp(g * lift, 0.f, 1.f);
    float const nb = std::clamp(b * lift, 0.f, 1.f);
    float const avg = (nr + ng + nb) / 3.f;
    float const satBoost = 1.28f;
    return {
        std::clamp(avg + (nr - avg) * satBoost, 0.02f, 1.f),
        std::clamp(avg + (ng - avg) * satBoost, 0.02f, 1.f),
        std::clamp(avg + (nb - avg) * satBoost, 0.02f, 1.f)
    };
}

inline void populateAdaptivePalette(
    std::uint8_t const* rgba,
    std::size_t count,
    float avgR,
    float avgG,
    float avgB,
    AmbientLightState& out
) {
    struct HueBin {
        float w = 0.f;
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
    };
    constexpr int kBins = 24;
    constexpr float kBinDeg = 360.f / static_cast<float>(kBins);
    std::array<HueBin, kBins> bins{};
    float totalHueWeight = 0.f;

    for (std::size_t i = 0; i < count; ++i) {
        float const r = static_cast<float>(rgba[i * 4 + 0]) / 255.f;
        float const g = static_cast<float>(rgba[i * 4 + 1]) / 255.f;
        float const b = static_cast<float>(rgba[i * 4 + 2]) / 255.f;
        float const maxC = std::max({r, g, b});
        float const minC = std::min({r, g, b});
        float const chroma = maxC - minC;
        if (maxC < 0.07f || chroma < 0.035f) continue;

        auto const hsl = rgbToHsl(r, g, b);
        float const hsvSat = chroma / maxC;
        if (hsvSat < 0.14f) continue;

        // Emphasize clean, vivid chromatic areas over muddy greys/blacks.
        float const satPow = hsvSat * std::sqrt(hsvSat);
        float const weight = chroma * satPow * (0.20f + 0.80f * maxC);
        int const binIdx = std::clamp(static_cast<int>(hsl.h / kBinDeg), 0, kBins - 1);
        bins[binIdx].w += weight;
        bins[binIdx].r += r * weight;
        bins[binIdx].g += g * weight;
        bins[binIdx].b += b * weight;
        totalHueWeight += weight;
    }

    float const chromaPresence = std::clamp(totalHueWeight / std::max(1.f, static_cast<float>(count) * 0.036f), 0.f, 1.f);

    if (totalHueWeight <= 1e-4f) {
        // Near-monochrome backdrop: build tasteful tonal contrast from scene luminance.
        float const lum = rgbLuminance(avgR, avgG, avgB);
        float const baseL = std::clamp(lum + (lum < 0.45f ? 0.22f : -0.05f), 0.38f, 0.72f);
        out.palettePrimary = {baseL, baseL, std::clamp(baseL + 0.04f, 0.f, 1.f)};
        out.paletteSecondary = {
            std::clamp(baseL * 0.78f, 0.25f, 0.78f),
            std::clamp(baseL * 0.82f, 0.25f, 0.80f),
            std::clamp(baseL * 0.90f, 0.30f, 0.85f)
        };
        out.paletteAccent = {
            std::clamp(out.keyColor.r * 0.92f + 0.08f, 0.45f, 1.f),
            std::clamp(out.keyColor.g * 0.92f + 0.08f, 0.45f, 1.f),
            std::clamp(out.keyColor.b * 0.92f + 0.08f, 0.45f, 1.f)
        };
        out.paletteSurface = {
            std::clamp(avgR * 0.65f + 0.12f, 0.14f, 0.42f),
            std::clamp(avgG * 0.65f + 0.12f, 0.14f, 0.42f),
            std::clamp(avgB * 0.70f + 0.14f, 0.16f, 0.46f)
        };
        return;
    }

    int bestBin = 0;
    for (int i = 1; i < kBins; ++i) {
        if (bins[i].w > bins[bestBin].w) bestBin = i;
    }

    auto blendBinCluster = [&](int center) -> AmbientRgb {
        float wSum = bins[center].w;
        float rSum = bins[center].r;
        float gSum = bins[center].g;
        float bSum = bins[center].b;
        // Smooth 5-bin cluster (+/- 30 deg) prevents harsh palette snaps on animated scenes.
        static constexpr std::array<std::pair<int, float>, 4> kOffsets{{
            {-1, 0.64f}, {1, 0.64f}, {-2, 0.22f}, {2, 0.22f}
        }};
        for (auto const& [off, kernelW] : kOffsets) {
            int const idx = (center + off + kBins) % kBins;
            if (bins[idx].w > bins[center].w * 0.14f) {
                wSum += bins[idx].w * kernelW;
                rSum += bins[idx].r * kernelW;
                gSum += bins[idx].g * kernelW;
                bSum += bins[idx].b * kernelW;
            }
        }
        if (wSum <= 1e-5f) return {avgR, avgG, avgB};
        return {rSum / wSum, gSum / wSum, bSum / wSum};
    };

    AmbientRgb const rawPrimary = blendBinCluster(bestBin);
    out.palettePrimary = shapeUiTone(rawPrimary, 0.60f, 0.96f, 0.46f, 0.64f, chromaPresence);
    auto const priHsl = rgbToHsl(out.palettePrimary.r, out.palettePrimary.g, out.palettePrimary.b);

    // Search for a distinct secondary chromatic cluster with strong angular separation from Primary.
    int secondBin = -1;
    float bestSecondScore = 0.f;
    for (int i = 0; i < kBins; ++i) {
        if (i == bestBin || bins[i].w <= 1e-5f) continue;
        float const centerH = (static_cast<float>(i) + 0.5f) * kBinDeg;
        float const hueDist = circularHueDistanceDeg(priHsl.h, centerH);
        if (hueDist < 36.f) continue;
        float const sepFactor = 0.30f + 0.70f * std::clamp((hueDist - 36.f) / 96.f, 0.f, 1.f);
        float const score = bins[i].w * sepFactor;
        if (score > bestSecondScore) {
            bestSecondScore = score;
            secondBin = i;
        }
    }

    float const hueShift = (priHsl.h >= 60.f && priHsl.h <= 210.f) ? 42.f : -42.f;
    float const secLum = std::clamp(priHsl.l + (priHsl.l < 0.54f ? 0.10f : -0.10f), 0.44f, 0.66f);
    AmbientRgb const analogousSec = hslToRgb(priHsl.h + hueShift, std::clamp(priHsl.s * 0.94f, 0.48f, 0.90f), secLum);

    if (secondBin >= 0 && bins[secondBin].w >= bins[bestBin].w * 0.010f) {
        AmbientRgb const rawSec = blendBinCluster(secondBin);
        AmbientRgb const clusterSec = shapeUiTone(rawSec, 0.58f, 0.95f, 0.48f, 0.66f, chromaPresence);
        float const ratio = bins[secondBin].w / std::max(1e-5f, bins[bestBin].w);
        float const t = std::clamp((ratio - 0.010f) / 0.055f, 0.f, 1.f);
        out.paletteSecondary = {
            analogousSec.r + (clusterSec.r - analogousSec.r) * t,
            analogousSec.g + (clusterSec.g - analogousSec.g) * t,
            analogousSec.b + (clusterSec.b - analogousSec.b) * t
        };
    } else {
        out.paletteSecondary = analogousSec;
    }

    // Accent tone harmonizes bright key light with a touch of the dominant theme hue.
    AmbientRgb const rawAccent{
        out.palettePrimary.r * 0.25f + out.keyColor.r * 0.75f,
        out.palettePrimary.g * 0.25f + out.keyColor.g * 0.75f,
        out.palettePrimary.b * 0.25f + out.keyColor.b * 0.75f
    };
    out.paletteAccent = shapeUiTone(rawAccent, 0.58f, 0.96f, 0.55f, 0.72f, std::max(0.42f, chromaPresence));

    // Surface tone paints brown/panel UI at a clean mid-dark value so buttons & text stay crisp.
    AmbientRgb const rawSurface{
        out.shadowColor.r * 0.48f + out.paletteSecondary.r * 0.24f + out.palettePrimary.r * 0.28f,
        out.shadowColor.g * 0.48f + out.paletteSecondary.g * 0.24f + out.palettePrimary.g * 0.28f,
        out.shadowColor.b * 0.48f + out.paletteSecondary.b * 0.24f + out.palettePrimary.b * 0.28f
    };
    out.paletteSurface = shapeUiTone(rawSurface, 0.26f, 0.56f, 0.22f, 0.38f, chromaPresence);
}

inline void populateAmbientZones(std::uint8_t const* rgba, int w, int h, AmbientLightState& out) {
    constexpr int kZW = AmbientLightState::kZoneW;
    constexpr int kZH = AmbientLightState::kZoneH;
    for (int zy = 0; zy < kZH; ++zy) {
        int const y0 = (zy * h) / kZH;
        int const y1 = std::max(y0 + 1, ((zy + 1) * h) / kZH);
        for (int zx = 0; zx < kZW; ++zx) {
            int const x0 = (zx * w) / kZW;
            int const x1 = std::max(x0 + 1, ((zx + 1) * w) / kZW);
            float wSum = 0.f;
            float rSum = 0.f, gSum = 0.f, bSum = 0.f;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    std::size_t const idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                    float const r = static_cast<float>(rgba[idx * 4 + 0]) / 255.f;
                    float const g = static_cast<float>(rgba[idx * 4 + 1]) / 255.f;
                    float const b = static_cast<float>(rgba[idx * 4 + 2]) / 255.f;
                    float const maxC = std::max({r, g, b});
                    float const minC = std::min({r, g, b});
                    float const chroma = maxC - minC;
                    float const lum = rgbLuminance(r, g, b);
                    float const wgt = 0.30f + 0.55f * lum + 0.75f * chroma;
                    wSum += wgt;
                    rSum += r * wgt;
                    gSum += g * wgt;
                    bSum += b * wgt;
                }
            }
            std::size_t const zIdx = static_cast<std::size_t>(zy) * kZW + static_cast<std::size_t>(zx);
            if (wSum > 1e-4f) {
                out.zoneColors[zIdx] = normalizeZoneColor(rSum / wSum, gSum / wSum, bSum / wSum, out.keyColor);
            } else {
                out.zoneColors[zIdx] = out.keyColor;
            }
        }
    }
    out.hasZones = true;
}

// Analyze an RGBA8 bottom-up (OpenGL glReadPixels order) grid of dimensions w x h.
inline AmbientLightState analyzeAmbientGrid(std::uint8_t const* rgba, int w, int h) {
    AmbientLightState out;
    if (!rgba || w <= 0 || h <= 0) return out;

    std::size_t const count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<float> luma(count, 0.f);
    float sumL = 0.f;
    float maxL = 0.f;
    float avgR = 0.f, avgG = 0.f, avgB = 0.f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::size_t const idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            float const r = static_cast<float>(rgba[idx * 4 + 0]) / 255.f;
            float const g = static_cast<float>(rgba[idx * 4 + 1]) / 255.f;
            float const b = static_cast<float>(rgba[idx * 4 + 2]) / 255.f;
            avgR += r;
            avgG += g;
            avgB += b;
            float const l = rgbLuminance(r, g, b);
            luma[idx] = l;
            sumL += l;
            if (l > maxL) maxL = l;
        }
    }

    float const invCount = 1.f / static_cast<float>(count);
    avgR *= invCount;
    avgG *= invCount;
    avgB *= invCount;
    float const avgL = sumL * invCount;
    out.averageLuma = avgL;
    float const contrast = std::max(0.f, maxL - avgL);
    out.fromCapture = true;

    // Flat or near-black background: keep gentle upper-left light and ambient tint from scene.
    if (contrast < 0.035f) {
        out.keyColor = normalizeLightColor(avgR * 1.15f + 0.12f, avgG * 1.15f + 0.12f, avgB * 1.15f + 0.12f, out.keyColor);
        out.shadowColor = normalizeShadowColor(avgR, avgG, avgB, out.shadowColor);
        out.directionality = 0.25f;
        populateAdaptivePalette(rgba, count, avgR, avgG, avgB, out);
        populateAmbientZones(rgba, w, h, out);
        return out;
    }

    float const threshold = avgL + contrast * 0.45f;
    float const softStart = avgL + contrast * 0.25f;
    float const softSpan = std::max(0.03f, threshold - softStart);
    float const denom = std::max(0.05f, maxL - softStart);

    float wSum = 0.f;
    float lx = 0.f, ly = 0.f;
    float keyR = 0.f, keyG = 0.f, keyB = 0.f;

    float shWeightSum = 0.f;
    float shR = 0.f, shG = 0.f, shB = 0.f;

    auto brightRamp = [&](float l) {
        float const t = std::clamp((l - softStart) / softSpan, 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    auto brightWeight = [&](float l, float ramp) {
        float const norm = std::max(0.f, (l - softStart) / denom);
        return ramp * norm * norm * (0.4f + 0.6f * l);
    };

    for (int y = 0; y < h; ++y) {
        float const v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            std::size_t const idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            float const r = static_cast<float>(rgba[idx * 4 + 0]) / 255.f;
            float const g = static_cast<float>(rgba[idx * 4 + 1]) / 255.f;
            float const b = static_cast<float>(rgba[idx * 4 + 2]) / 255.f;
            float const l = luma[idx];

            float const ramp = brightRamp(l);
            if (ramp > 1e-4f) {
                float const weight = brightWeight(l, ramp);
                wSum += weight;
                lx += u * weight;
                ly += v * weight;
                keyR += r * weight;
                keyG += g * weight;
                keyB += b * weight;
            }
            float const shadeMask = 1.f - ramp * 0.85f;
            if (shadeMask > 1e-4f) {
                float const darkWeight = shadeMask * (0.35f + 0.65f * (1.f - std::min(1.f, l / std::max(avgL, 0.08f))));
                shWeightSum += darkWeight;
                shR += r * darkWeight;
                shG += g * darkWeight;
                shB += b * darkWeight;
            }
        }
    }

    if (wSum > 1e-4f) {
        out.lightX = std::clamp(lx / wSum, 0.02f, 0.98f);
        out.lightY = std::clamp(ly / wSum, 0.02f, 0.98f);
        out.keyColor = normalizeLightColor(keyR / wSum, keyG / wSum, keyB / wSum, out.keyColor);
    }

    if (shWeightSum > 1e-4f) {
        out.shadowColor = normalizeShadowColor(shR / shWeightSum, shG / shWeightSum, shB / shWeightSum, out.shadowColor);
    }

    // Measure directional spread around centroid: concentrated bright region => sharper light.
    float spreadSq = 0.f;
    if (wSum > 1e-4f) {
        for (int y = 0; y < h; ++y) {
            float const v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
            for (int x = 0; x < w; ++x) {
                float const u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
                std::size_t const idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                float const l = luma[idx];
                float const ramp = brightRamp(l);
                if (ramp > 1e-4f) {
                    float const weight = brightWeight(l, ramp);
                    float const dx = u - out.lightX;
                    float const dy = v - out.lightY;
                    spreadSq += (dx * dx + dy * dy) * weight;
                }
            }
        }
        spreadSq /= wSum;
    }
    float const concentration = std::clamp(1.f - std::sqrt(spreadSq) * 1.4f, 0.2f, 1.f);
    out.directionality = std::clamp(contrast * 1.65f * (0.45f + 0.55f * concentration), 0.25f, 1.0f);

    float const gx = out.lightX - 0.5f;
    float const gy = out.lightY - 0.5f;
    float const gLen = std::hypot(gx, gy);
    if (gLen > 0.03f) {
        out.globalDirX = gx / gLen;
        out.globalDirY = gy / gLen;
    }

    populateAdaptivePalette(rgba, count, avgR, avgG, avgB, out);
    populateAmbientZones(rgba, w, h, out);
    return out;
}

inline float lerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

inline AmbientRgb lerpRgb(AmbientRgb const& a, AmbientRgb const& b, float t) {
    return {
        lerpFloat(a.r, b.r, t),
        lerpFloat(a.g, b.g, t),
        lerpFloat(a.b, b.b, t)
    };
}

inline AmbientLightState smoothAmbientState(AmbientLightState const& cur, AmbientLightState const& target, float dt) {
    if (!cur.fromCapture && target.fromCapture) return target;
    float const safeDt = std::clamp(dt, 0.f, 0.045f);
    float const alphaColor = 1.f - std::exp(-safeDt * 1.35f);
    float const alphaLight = 1.f - std::exp(-safeDt * 2.15f);
    float const alphaZone = 1.f - std::exp(-safeDt * 1.55f);
    AmbientLightState out = cur;
    out.lightX = lerpFloat(cur.lightX, target.lightX, alphaLight);
    out.lightY = lerpFloat(cur.lightY, target.lightY, alphaLight);
    out.keyColor = lerpRgb(cur.keyColor, target.keyColor, alphaColor);
    out.shadowColor = lerpRgb(cur.shadowColor, target.shadowColor, alphaColor);
    out.palettePrimary = lerpRgb(cur.palettePrimary, target.palettePrimary, alphaColor);
    out.paletteSecondary = lerpRgb(cur.paletteSecondary, target.paletteSecondary, alphaColor);
    out.paletteSurface = lerpRgb(cur.paletteSurface, target.paletteSurface, alphaColor);
    out.paletteAccent = lerpRgb(cur.paletteAccent, target.paletteAccent, alphaColor);
    if (!cur.hasZones && target.hasZones) {
        out.zoneColors = target.zoneColors;
    } else if (target.hasZones) {
        for (std::size_t i = 0; i < out.zoneColors.size(); ++i) {
            out.zoneColors[i] = lerpRgb(cur.zoneColors[i], target.zoneColors[i], alphaZone);
        }
    }
    out.hasZones = target.hasZones;
    out.directionality = lerpFloat(cur.directionality, target.directionality, alphaLight);
    out.averageLuma = lerpFloat(cur.averageLuma, target.averageLuma, alphaColor);
    out.fromCapture = target.fromCapture;

    float const gx = out.lightX - 0.5f;
    float const gy = out.lightY - 0.5f;
    float const gLen = std::hypot(gx, gy);
    if (gLen > 0.03f) {
        out.globalDirX = gx / gLen;
        out.globalDirY = gy / gLen;
    } else {
        out.globalDirX = lerpFloat(cur.globalDirX, target.globalDirX, alphaLight);
        out.globalDirY = lerpFloat(cur.globalDirY, target.globalDirY, alphaLight);
        float const len = std::max(1e-3f, std::hypot(out.globalDirX, out.globalDirY));
        out.globalDirX /= len;
        out.globalDirY /= len;
    }
    return out;
}

inline AmbientRgb sampleLocalAmbientColor(AmbientLightState const& state, float nodeU, float nodeV) {
    if (!state.hasZones) return state.keyColor;
    constexpr int kZW = AmbientLightState::kZoneW;
    constexpr int kZH = AmbientLightState::kZoneH;
    float const fx = std::clamp(nodeU * static_cast<float>(kZW) - 0.5f, 0.f, static_cast<float>(kZW - 1));
    float const fy = std::clamp(nodeV * static_cast<float>(kZH) - 0.5f, 0.f, static_cast<float>(kZH - 1));
    int const x0 = static_cast<int>(fx);
    int const y0 = static_cast<int>(fy);
    int const x1 = std::min(x0 + 1, kZW - 1);
    int const y1 = std::min(y0 + 1, kZH - 1);
    float const tx = fx - static_cast<float>(x0);
    float const ty = fy - static_cast<float>(y0);
    auto at = [&](int x, int y) -> AmbientRgb const& {
        return state.zoneColors[static_cast<std::size_t>(y) * kZW + static_cast<std::size_t>(x)];
    };
    AmbientRgb const bot = lerpRgb(at(x0, y0), at(x1, y0), tx);
    AmbientRgb const top = lerpRgb(at(x0, y1), at(x1, y1), tx);
    return lerpRgb(bot, top, ty);
}

// Compute light direction vector (toward light) and proximity boost for a UI node
// whose center sits at normalized screen UV (nodeU, nodeV).
inline AmbientNodeLight computeNodeLightDir(
    AmbientLightState const& state,
    float nodeU,
    float nodeV,
    float aspectRatio = 16.f / 9.f
) {
    float const dx = (state.lightX - nodeU) * std::max(0.5f, aspectRatio);
    float const dy = (state.lightY - nodeV);
    float const dist = std::hypot(dx, dy);

    AmbientNodeLight res;
    if (dist < 0.08f) {
        // Node sits right over the light source: blend toward global light vector to avoid jitter.
        float const blend = dist / 0.08f;
        float const lx = dist > 1e-4f ? (dx / dist) : state.globalDirX;
        float const ly = dist > 1e-4f ? (dy / dist) : state.globalDirY;
        float bx = lerpFloat(state.globalDirX, lx, blend);
        float by = lerpFloat(state.globalDirY, ly, blend);
        float const bLen = std::max(1e-3f, std::hypot(bx, by));
        res.dirX = bx / bLen;
        res.dirY = by / bLen;
    } else {
        res.dirX = dx / dist;
        res.dirY = dy / dist;
    }

    // Closer to the sun/lamp -> slightly stronger glow; further -> softer.
    res.proximityBoost = std::clamp(1.25f - dist * 0.35f, 0.78f, 1.32f);
    return res;
}

} // namespace paimon::ambient
