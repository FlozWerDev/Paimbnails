#pragma once
// Shared pixel math for the PackGen-style tint, used by both the
// cluster-mask path (LuminanceTinter) and the overlay-asset path
// (OverlayTinter). Semantics must stay bit-compatible between the two.

#include <Geode/cocos/include/ccTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace paimon::texture_studio::tintmath {

inline std::uint8_t clampByte(int v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

// Rec.601 luminance in 0..255 space, matching PackGen which divides this by
// `brightness` directly.
inline float luminance601(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return 0.30f * static_cast<float>(r)
         + 0.59f * static_cast<float>(g)
         + 0.11f * static_cast<float>(b);
}

// tinted = userColor * (luminance / brightness), clamped per channel exactly
// like PackGen's tintImageWithLuminance. Letting each channel saturate to 255
// independently is what keeps highlights bright; a hue-preserving rescale
// would dull them and shift the color away from what PackGen produces.
// Saturation/contrast are an optional post-grade (no-op at 1.0 / 0.0), applied
// after the PackGen clamp so defaults stay bit-faithful to the web tool.
inline void tintByLuminance(std::uint8_t srcR, std::uint8_t srcG, std::uint8_t srcB,
                            cocos2d::ccColor3B tint, float brightnessF,
                            float saturation, float contrast,
                            std::uint8_t& outR, std::uint8_t& outG, std::uint8_t& outB) {
    float lum = luminance601(srcR, srcG, srcB);
    float factor = (brightnessF > 0.0f) ? (lum / brightnessF) : 0.0f;

    float fR = std::clamp(tint.r * factor, 0.0f, 255.0f);
    float fG = std::clamp(tint.g * factor, 0.0f, 255.0f);
    float fB = std::clamp(tint.b * factor, 0.0f, 255.0f);

    if (saturation != 1.0f) {
        float luma = 0.30f * fR + 0.59f * fG + 0.11f * fB;
        fR = luma + (fR - luma) * saturation;
        fG = luma + (fG - luma) * saturation;
        fB = luma + (fB - luma) * saturation;
    }
    if (contrast != 0.0f) {
        float gain = 1.0f + contrast;
        fR = (fR - 127.5f) * gain + 127.5f;
        fG = (fG - 127.5f) * gain + 127.5f;
        fB = (fB - 127.5f) * gain + 127.5f;
    }

    outR = clampByte(static_cast<int>(std::lround(fR)));
    outG = clampByte(static_cast<int>(std::lround(fG)));
    outB = clampByte(static_cast<int>(std::lround(fB)));
}

// Straight-alpha overlay; 255 = hard replace.
inline void overlayPixel(std::uint8_t& baseR, std::uint8_t& baseG, std::uint8_t& baseB, std::uint8_t& baseA,
                         std::uint8_t overlayR, std::uint8_t overlayG, std::uint8_t overlayB,
                         std::uint8_t overlayA) {
    if (overlayA == 0) return;
    if (overlayA == 255) {
        baseR = overlayR;
        baseG = overlayG;
        baseB = overlayB;
        baseA = std::max(baseA, overlayA);
        return;
    }
    float alpha = overlayA / 255.0f;
    float invA  = 1.0f - alpha;
    baseR = clampByte(static_cast<int>(std::lround(overlayR * alpha + baseR * invA)));
    baseG = clampByte(static_cast<int>(std::lround(overlayG * alpha + baseG * invA)));
    baseB = clampByte(static_cast<int>(std::lround(overlayB * alpha + baseB * invA)));
    baseA = std::max(baseA, overlayA);
}

// PackGen's "alternative glow": replace the pixel entirely where mask > 0.
inline void replacePixel(std::uint8_t& baseR, std::uint8_t& baseG, std::uint8_t& baseB, std::uint8_t& baseA,
                         std::uint8_t overlayR, std::uint8_t overlayG, std::uint8_t overlayB,
                         std::uint8_t overlayA) {
    if (overlayA == 0) return;
    baseR = overlayR;
    baseG = overlayG;
    baseB = overlayB;
    baseA = std::max(baseA, overlayA);
}

}  // namespace paimon::texture_studio::tintmath
