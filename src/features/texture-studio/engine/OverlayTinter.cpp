#include "OverlayTinter.hpp"

#include "../packgen/TintEngine.hpp"

#include <algorithm>

namespace paimon::texture_studio {

namespace {

// packgen drawimage rule: mismatch still paints the top-left overlap (rejecting stranded whole files).
void applyOne(ImageBuffer& dst, ImageBuffer const& overlay,
              cocos2d::ccColor3B color, float brightness,
              float saturation, float contrast, bool replace) {
    if (overlay.empty() || dst.empty()) return;

    int W = std::min(dst.width(), overlay.width());
    int H = std::min(dst.height(), overlay.height());
    if (W <= 0 || H <= 0) return;

    auto* d = dst.data();
    auto const* o = overlay.data();
    if (!d || !o) return;

    auto spec = packgen::PrecomputedTint::make(
        color.r, color.g, color.b, brightness, saturation, contrast);
    static const packgen::AlphaLut kLut = packgen::AlphaLut::make();

    // same overlap rule and op order as the old loop; per-pixel clamps hoisted into spec.
    packgen::applyOverlayBand(d, dst.width(), o, overlay.width(),
                              0, H, W, spec, kLut, replace);
}

}  // namespace

bool OverlayImages::anyUsable(int width, int height) const {
    // any non-empty overlay contributes at least its top-left overlap.
    (void)width;
    (void)height;
    return !overlay1.empty() || !overlay2.empty() || !gold.empty()
        || !demon1.empty() || !demon2.empty() || !glow.empty();
}

ImageBuffer OverlayTinter::apply(ImageBuffer const& base,
                                 OverlayImages const& overlays,
                                 TintColors const& colors,
                                 TinterOptions options) {
    if (base.empty()) return ImageBuffer();

    ImageBuffer out(base.width(), base.height(), base.data());

    float brightness = static_cast<float>(std::clamp(options.brightness, 1, 1000));
    float saturation = std::clamp(options.saturation, 0.0f, 3.0f);
    float contrast   = std::clamp(options.contrast, -1.0f, 1.0f);

    applyOne(out, overlays.overlay1, colors.color1, brightness, saturation, contrast, false);
    applyOne(out, overlays.overlay2, colors.color2, brightness, saturation, contrast, false);
    applyOne(out, overlays.gold,     colors.color2, brightness, saturation, contrast, false);
    applyOne(out, overlays.demon1,   colors.color1, brightness, saturation, contrast, false);
    applyOne(out, overlays.demon2,   colors.color2, brightness, saturation, contrast, false);
    applyOne(out, overlays.glow,     colors.glow,   brightness, saturation, contrast,
             options.alternativeGlowOverlay);

    return out;
}

}  // namespace paimon::texture_studio
