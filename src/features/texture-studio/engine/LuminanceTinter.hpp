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

    // interior bright details (white glyphs inside the button, apart from the glow ring); white = untouched.
    cocos2d::ccColor3B detail{255, 255, 255};
};

struct TinterOptions {
    // packgen "brightness", 100..300, default 160. lower = brighter.
    int brightness = 160;

    // true: glow pixels fully replaced, no bleed-through on dark glow colors.
    bool alternativeGlowOverlay = false;

    // luminance floor (0..255) never tinted. off by default: outline mask decides, 0 = disabled.
    int darkOutlineThreshold = 0;

    // post-tint saturation on tinted pixels only. 1.0 = neutral.
    float saturation = 1.0f;

    // post-tint contrast around mid-grey, tinted pixels only. 0 = neutral.
    float contrast = 0.0f;
};

class LuminanceTinter final {
public:
    // packgen-style tint into a fresh buffer; order base → c1 → c2 → glow. outline/alpha-0 pass through.
    static ImageBuffer apply(ImageBuffer const& source,
                             MaskSet const& masks,
                             TintColors const& colors,
                             TinterOptions options = {});

private:
    LuminanceTinter() = delete;
};

}  // namespace paimon::texture_studio
