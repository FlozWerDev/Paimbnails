#pragma once

#include "../data/ImageBuffer.hpp"
#include "MaskBuilder.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <cstdint>

namespace paimon::texture_studio {

struct TintColors {
    cocos2d::ccColor3B color1{149, 226, 3};
    cocos2d::ccColor3B color2{28, 233, 255};
    cocos2d::ccColor3B glow  {255, 255, 255};

    // Interior bright details (white glyphs/icons INSIDE the button, spatially
    // separated from the outer glow ring). Pure white = leave untouched, so
    // changing the glow no longer repaints every white pixel.
    cocos2d::ccColor3B detail{255, 255, 255};
};

struct TinterOptions {
    // PackGen's "brightness" parameter. Range 100..300; default 160.
    // Lower = brighter (factor > 1 amplifies user color); higher = darker.
    int brightness = 160;

    // When true, glow-masked pixels are REPLACED entirely (no alpha-blend),
    // avoiding base-color bleed-through for dark glow colors.
    bool alternativeGlowOverlay = false;

    // Pixels whose original Rec.601 luminance (0..255) is below this are
    // never tinted. Disabled by default: the outline mask is the source of
    // truth, and a global cutoff would make legitimate dark Color2 details
    // impossible to recolor. 0 = disabled.
    int darkOutlineThreshold = 0;

    // Post-tint saturation multiplier applied to the tinted color only
    // (untinted/outline pixels are untouched). 1.0 = neutral, 0 = grayscale,
    // 2 = doubled.
    float saturation = 1.0f;

    // Post-tint contrast around mid-grey, applied to the tinted color only.
    // 0 = neutral; -1..1 sensible range.
    float contrast = 0.0f;
};

class LuminanceTinter final {
public:
    // Apply the PackGen-style tint. Returns a fresh RGBA8 buffer (same size
    // as source); source is not modified. Order: base → C1 → C2 → glow.
    // Outline-role and alpha==0 pixels pass through unchanged.
    static ImageBuffer apply(ImageBuffer const& source,
                             MaskSet const& masks,
                             TintColors const& colors,
                             TinterOptions options = {});

private:
    LuminanceTinter() = delete;
};

}  // namespace paimon::texture_studio
