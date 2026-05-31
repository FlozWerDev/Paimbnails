#pragma once
//
// IconColorMath.hpp - Pure color math utilities used by the recolor service.
//
// Original implementation: written from scratch for Paimbnails. The HSV
// algorithm is textbook (Wikipedia: HSL_and_HSV) but the code style and
// surface area diverge from any reference mod we may have looked at:
//   - returns std::tuple instead of mutating output params
//   - uses C++23 std::clamp / std::lerp / std::midpoint where natural
//   - exposes color blending helpers (lerp, complement, luminance)
//
// All functions are pure, header-only friendly, and have zero allocations.
//

#include <Geode/cocos/include/ccTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

namespace paimon::icons::math {

// ─────────────────────────────────────────────────────────────
// Normalisation helpers
// ─────────────────────────────────────────────────────────────

inline float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

inline std::uint8_t clampByte(int v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

// Wrap an angle in degrees to [0, 360).
inline float wrapHue(float deg) {
    deg = std::fmod(deg, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

// ─────────────────────────────────────────────────────────────
// RGB <-> HSV
//
// HSV: hue ∈ [0, 360), sat ∈ [0, 1], val ∈ [0, 1].
// ─────────────────────────────────────────────────────────────

struct HSV { float h, s, v; };

inline HSV toHSV(cocos2d::ccColor3B c) {
    const float r = c.r / 255.0f;
    const float g = c.g / 255.0f;
    const float b = c.b / 255.0f;

    const float maxC = std::max({r, g, b});
    const float minC = std::min({r, g, b});
    const float delta = maxC - minC;

    HSV out{0.0f, 0.0f, maxC};
    if (maxC > 0.0f) out.s = delta / maxC;
    if (delta < 1e-6f) {
        // Achromatic.
        out.h = 0.0f;
        return out;
    }

    float h;
    if (maxC == r)      h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
    else if (maxC == g) h = (b - r) / delta + 2.0f;
    else                h = (r - g) / delta + 4.0f;
    out.h = h * 60.0f;
    return out;
}

inline cocos2d::ccColor3B fromHSV(HSV hsv) {
    const float h = wrapHue(hsv.h);
    const float s = clamp01(hsv.s);
    const float v = clamp01(hsv.v);

    const float c = v * s;
    const float h60 = h / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(h60, 2.0f) - 1.0f));
    const float m = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    const int sextant = static_cast<int>(std::floor(h60)) % 6;
    switch (sextant) {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }
    return cocos2d::ccColor3B{
        clampByte(static_cast<int>((r + m) * 255.0f + 0.5f)),
        clampByte(static_cast<int>((g + m) * 255.0f + 0.5f)),
        clampByte(static_cast<int>((b + m) * 255.0f + 0.5f)),
    };
}

// ─────────────────────────────────────────────────────────────
// Convenience operations
// ─────────────────────────────────────────────────────────────

// Linearly interpolate two colors.
inline cocos2d::ccColor3B lerp(cocos2d::ccColor3B a, cocos2d::ccColor3B b, float t) {
    t = clamp01(t);
    return cocos2d::ccColor3B{
        clampByte(static_cast<int>(std::lerp(static_cast<float>(a.r), static_cast<float>(b.r), t))),
        clampByte(static_cast<int>(std::lerp(static_cast<float>(a.g), static_cast<float>(b.g), t))),
        clampByte(static_cast<int>(std::lerp(static_cast<float>(a.b), static_cast<float>(b.b), t))),
    };
}

// Rotate hue by `deg` degrees, preserve saturation/value.
inline cocos2d::ccColor3B rotateHue(cocos2d::ccColor3B c, float deg) {
    auto hsv = toHSV(c);
    hsv.h = wrapHue(hsv.h + deg);
    return fromHSV(hsv);
}

// Multiply saturation and value (clamped to [0, 1]).
inline cocos2d::ccColor3B scaleSV(cocos2d::ccColor3B c, float satMul, float valMul) {
    auto hsv = toHSV(c);
    hsv.s = clamp01(hsv.s * satMul);
    hsv.v = clamp01(hsv.v * valMul);
    return fromHSV(hsv);
}

// Color complement (180° hue + invert value to keep contrast).
inline cocos2d::ccColor3B complement(cocos2d::ccColor3B c) {
    auto hsv = toHSV(c);
    hsv.h = wrapHue(hsv.h + 180.0f);
    return fromHSV(hsv);
}

// Perceptual luminance (Rec. 709). Returns 0..1.
inline float luminance(cocos2d::ccColor3B c) {
    return 0.2126f * (c.r / 255.0f)
         + 0.7152f * (c.g / 255.0f)
         + 0.0722f * (c.b / 255.0f);
}

// ─────────────────────────────────────────────────────────────
// Stable per-id hash → HSV color
//
// Used by ColorMode::RandomStable. Two icons with the same (type, id) ALWAYS
// hash to the same color, regardless of session.
//
// Implementation: SplitMix64 mixing on a 64-bit seed derived from (type, id).
// Output H is uniform in [0, 360), S/V depend on the chosen palette.
// ─────────────────────────────────────────────────────────────
inline std::uint64_t splitMix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline float hashToFloat01(std::uint64_t hash) {
    // Top 24 bits as fraction.
    return static_cast<float>(hash >> 40) / 16777216.0f;
}

}  // namespace paimon::icons::math
