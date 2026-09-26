#include "LuminanceTinter.hpp"

#include "../packgen/TintEngine.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

ImageBuffer LuminanceTinter::apply(ImageBuffer const& source,
                                   MaskSet const& masks,
                                   TintColors const& colors,
                                   TinterOptions options) {
    if (source.empty()) return ImageBuffer();

    int W = source.width();
    int H = source.height();

    // Masks must match source size; else fail soft as empty.
    auto maskMatches = [W, H](MaskBuffer const& m) {
        return m.width == W && m.height == H && !m.data.empty();
    };
    bool hasC1   = maskMatches(masks.color1);
    bool hasC2   = maskMatches(masks.color2);
    bool hasGlow = maskMatches(masks.glow);
    // White detail = neutral: interiors keep vanilla unless the user picks a color.
    bool hasDetail = maskMatches(masks.detail) &&
                     !(colors.detail.r == 255 && colors.detail.g == 255 &&
                       colors.detail.b == 255);

    // Initial copy keeps outline/unmasked correct.
    ImageBuffer out(W, H, source.data());

    float brightness = static_cast<float>(std::clamp(options.brightness, 1, 1000));
    float saturation = std::clamp(options.saturation, 0.0f, 3.0f);
    float contrast   = std::clamp(options.contrast, -1.0f, 1.0f);

    auto const* src = source.data();
    auto* dst = out.data();
    // Fail soft on pixel-less buffers; the old loop dereferenced null here.
    if (!src || !dst) return out;

    auto maskPtr = [](MaskBuffer const& m, bool has) -> std::uint8_t const* {
        return has ? m.data.data() : nullptr;
    };

    using packgen::PrecomputedTint;
    PrecomputedTint c1 = PrecomputedTint::make(
        colors.color1.r, colors.color1.g, colors.color1.b,
        brightness, saturation, contrast);
    PrecomputedTint c2 = PrecomputedTint::make(
        colors.color2.r, colors.color2.g, colors.color2.b,
        brightness, saturation, contrast);
    PrecomputedTint detail = PrecomputedTint::make(
        colors.detail.r, colors.detail.g, colors.detail.b,
        brightness, saturation, contrast);
    PrecomputedTint glow = PrecomputedTint::make(
        colors.glow.r, colors.glow.g, colors.glow.b,
        brightness, saturation, contrast);

    // Shared read-only table; magic statics are thread-safe.
    static const packgen::AlphaLut kLut = packgen::AlphaLut::make();

    // Bit-exact with the old loop (same op order/clamps); kernel touches masked pixels only.
    packgen::tintStackImage(src, dst, W, H,
                            maskPtr(masks.color1, hasC1),
                            maskPtr(masks.color2, hasC2),
                            maskPtr(masks.detail, hasDetail),
                            maskPtr(masks.glow, hasGlow),
                            c1, c2, detail, glow,
                            hasDetail, options.alternativeGlowOverlay,
                            options.darkOutlineThreshold, kLut);

    return out;
}

}  // namespace paimon::texture_studio
