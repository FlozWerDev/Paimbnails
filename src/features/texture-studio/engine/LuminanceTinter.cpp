#include "LuminanceTinter.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

// Clamp an int to the [0, 255] byte range.
inline std::uint8_t clampByte(int v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

// Compute Rec.601 luminance from an RGB triple (returned in [0, 255] like
// PackGen — its `tintImageWithLuminance` keeps luminance in 0..255 space and
// divides by `brightness` directly, so we match that scale exactly).
inline float luminance601(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return 0.30f * static_cast<float>(r)
         + 0.59f * static_cast<float>(g)
         + 0.11f * static_cast<float>(b);
}

// Tint a single pixel using PackGen's algorithm:
//   tinted = userColor * (luminance / brightness)
//
// PackGen hard-clamps each channel to [0, 255] independently, which loses
// hue when one channel saturates before the others (e.g. tint=(255,0,0)
// against a bright source: r=510 → 255, g=0, b=0 looks identical to
// r=255 → factor doesn't matter visually). This produces flat-looking
// recolors on bright source pixels. We mitigate by detecting saturation
// and proportionally rescaling so the brightest channel exactly hits 255
// — this preserves the user's chosen *hue* even when the source is too
// bright for a literal multiply to fit.
inline void tintByLuminance(std::uint8_t srcR, std::uint8_t srcG, std::uint8_t srcB,
                            cocos2d::ccColor3B tint, float brightnessF,
                            std::uint8_t& outR, std::uint8_t& outG, std::uint8_t& outB) {
    float lum = luminance601(srcR, srcG, srcB);
    // Avoid div-by-zero if the user passes brightness=0 somehow.
    float factor = (brightnessF > 0.0f) ? (lum / brightnessF) : 0.0f;

    float fR = tint.r * factor;
    float fG = tint.g * factor;
    float fB = tint.b * factor;

    // If any channel exceeds 255, scale all three down so the brightest
    // channel lands at 255. This preserves the hue ratio between R/G/B
    // (= preserves the user's chosen color) at the cost of slightly
    // reducing brightness on saturated source pixels — the alternative
    // (independent clamp) would silently mutate hue, which looks worse.
    float maxCh = std::max({fR, fG, fB});
    if (maxCh > 255.0f) {
        float rescale = 255.0f / maxCh;
        fR *= rescale;
        fG *= rescale;
        fB *= rescale;
    }

    outR = clampByte(static_cast<int>(std::lround(fR)));
    outG = clampByte(static_cast<int>(std::lround(fG)));
    outB = clampByte(static_cast<int>(std::lround(fB)));
}

// Straight-alpha overlay of (overlayR/G/B, overlayA) onto (baseR/G/B, baseA).
// `overlayA` here is the mask byte (0..255). If overlayA == 255 we do a hard
// replace; otherwise we lerp by overlayA/255.
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

// Replace overlay (PackGen's "alternative glow" mode): replace the pixel
// entirely with the tinted color where the mask is > 0.
inline void replacePixel(std::uint8_t& baseR, std::uint8_t& baseG, std::uint8_t& baseB, std::uint8_t& baseA,
                         std::uint8_t overlayR, std::uint8_t overlayG, std::uint8_t overlayB,
                         std::uint8_t overlayA) {
    if (overlayA == 0) return;
    baseR = overlayR;
    baseG = overlayG;
    baseB = overlayB;
    baseA = std::max(baseA, overlayA);
}

}  // anonymous namespace

ImageBuffer LuminanceTinter::apply(ImageBuffer const& source,
                                   MaskSet const& masks,
                                   TintColors const& colors,
                                   TinterOptions options) {
    if (source.empty()) return ImageBuffer();

    int W = source.width();
    int H = source.height();

    // Sanity: the masks should match the source size. If not, we treat them
    // as empty (zero-alpha) to fail soft.
    auto maskMatches = [W, H](MaskBuffer const& m) {
        return m.width == W && m.height == H && !m.data.empty();
    };
    bool hasC1   = maskMatches(masks.color1);
    bool hasC2   = maskMatches(masks.color2);
    bool hasGlow = maskMatches(masks.glow);

    // Result starts as a straight copy of the source. Outline pixels (which
    // we never tint) and unmasked pixels stay correct by construction.
    ImageBuffer out(W, H, source.data());

    float brightness = static_cast<float>(std::clamp(options.brightness, 1, 1000));

    auto* dst = out.data();

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::size_t pixelOffset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x) * 4;
            std::size_t maskOffset  = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x;

            std::uint8_t srcA = dst[pixelOffset + 3];
            if (srcA == 0) continue;  // fully transparent stays transparent

            std::uint8_t srcR = dst[pixelOffset + 0];
            std::uint8_t srcG = dst[pixelOffset + 1];
            std::uint8_t srcB = dst[pixelOffset + 2];

            std::uint8_t baseR = srcR, baseG = srcG, baseB = srcB, baseA = srcA;

            // Color 1 overlay
            if (hasC1) {
                std::uint8_t mC1 = masks.color1.data[maskOffset];
                if (mC1 > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.color1, brightness, tR, tG, tB);
                    overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mC1);
                }
            }

            // Color 2 overlay
            if (hasC2) {
                std::uint8_t mC2 = masks.color2.data[maskOffset];
                if (mC2 > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.color2, brightness, tR, tG, tB);
                    overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mC2);
                }
            }

            // Glow overlay (with optional "alternative" mode)
            if (hasGlow) {
                std::uint8_t mG = masks.glow.data[maskOffset];
                if (mG > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.glow, brightness, tR, tG, tB);
                    if (options.alternativeGlowOverlay) {
                        replacePixel(baseR, baseG, baseB, baseA, tR, tG, tB, mG);
                    } else {
                        overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mG);
                    }
                }
            }

            // Outline mask is never tinted — pixels covered by it keep their
            // original RGB (already in `dst` from the initial copy).

            dst[pixelOffset + 0] = baseR;
            dst[pixelOffset + 1] = baseG;
            dst[pixelOffset + 2] = baseB;
            dst[pixelOffset + 3] = baseA;
        }
    }

    return out;
}

}  // namespace paimon::texture_studio
