#include "LuminanceTinter.hpp"

#include "TintMath.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

using tintmath::luminance601;
using tintmath::overlayPixel;
using tintmath::replacePixel;
using tintmath::tintByLuminance;

ImageBuffer LuminanceTinter::apply(ImageBuffer const& source,
                                   MaskSet const& masks,
                                   TintColors const& colors,
                                   TinterOptions options) {
    if (source.empty()) return ImageBuffer();

    int W = source.width();
    int H = source.height();

    // Masks must match the source size; otherwise treat as empty (fail soft).
    auto maskMatches = [W, H](MaskBuffer const& m) {
        return m.width == W && m.height == H && !m.data.empty();
    };
    bool hasC1   = maskMatches(masks.color1);
    bool hasC2   = maskMatches(masks.color2);
    bool hasGlow = maskMatches(masks.glow);
    // Pure white = neutral: interior bright details keep their vanilla look
    // unless the user explicitly picks a detail color.
    bool hasDetail = maskMatches(masks.detail) &&
                     !(colors.detail.r == 255 && colors.detail.g == 255 &&
                       colors.detail.b == 255);

    // Outline and unmasked pixels stay correct via this initial copy.
    ImageBuffer out(W, H, source.data());

    float brightness = static_cast<float>(std::clamp(options.brightness, 1, 1000));
    float saturation = std::clamp(options.saturation, 0.0f, 3.0f);
    float contrast   = std::clamp(options.contrast, -1.0f, 1.0f);

    auto* dst = out.data();

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::size_t pixelOffset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x) * 4;
            std::size_t maskOffset  = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x;

            std::uint8_t srcA = dst[pixelOffset + 3];
            if (srcA == 0) continue;

            std::uint8_t srcR = dst[pixelOffset + 0];
            std::uint8_t srcG = dst[pixelOffset + 1];
            std::uint8_t srcB = dst[pixelOffset + 2];

            if (options.darkOutlineThreshold > 0 &&
                luminance601(srcR, srcG, srcB) <
                    static_cast<float>(options.darkOutlineThreshold)) {
                continue;
            }

            std::uint8_t baseR = srcR, baseG = srcG, baseB = srcB, baseA = srcA;

            if (hasC1) {
                std::uint8_t mC1 = masks.color1.data[maskOffset];
                if (mC1 > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.color1, brightness,
                                    saturation, contrast, tR, tG, tB);
                    overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mC1);
                }
            }

            if (hasC2) {
                std::uint8_t mC2 = masks.color2.data[maskOffset];
                if (mC2 > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.color2, brightness,
                                    saturation, contrast, tR, tG, tB);
                    overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mC2);
                }
            }

            if (hasDetail) {
                std::uint8_t mD = masks.detail.data[maskOffset];
                if (mD > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.detail, brightness,
                                    saturation, contrast, tR, tG, tB);
                    overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mD);
                }
            }

            if (hasGlow) {
                std::uint8_t mG = masks.glow.data[maskOffset];
                if (mG > 0) {
                    std::uint8_t tR, tG, tB;
                    tintByLuminance(srcR, srcG, srcB, colors.glow, brightness,
                                    saturation, contrast, tR, tG, tB);
                    if (options.alternativeGlowOverlay) {
                        replacePixel(baseR, baseG, baseB, baseA, tR, tG, tB, mG);
                    } else {
                        overlayPixel(baseR, baseG, baseB, baseA, tR, tG, tB, mG);
                    }
                }
            }

            dst[pixelOffset + 0] = baseR;
            dst[pixelOffset + 1] = baseG;
            dst[pixelOffset + 2] = baseB;
            dst[pixelOffset + 3] = baseA;
        }
    }

    return out;
}

}  // namespace paimon::texture_studio
