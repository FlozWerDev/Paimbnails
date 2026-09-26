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

    // Interior bright details (white glyphs inside the button, apart from the glow ring); white = untouched.
    cocos2d::ccColor3B detail{255, 255, 255};
};

struct TinterOptions {
    // PackGen "brightness", 100..300, default 160. Lower = brighter.
    int brightness = 160;

    // True: glow pixels fully replaced, no bleed-through on dark glow colors.
    bool alternativeGlowOverlay = false;

    // Luminance floor (0..255) never tinted. Off by default: outline mask decides, 0 = disabled.
    int darkOutlineThreshold = 0;

    // Post-tint saturation on tinted pixels only. 1.0 = neutral.
    float saturation = 1.0f;

    // Post-tint contrast around mid-grey, tinted pixels only. 0 = neutral.
    float contrast = 0.0f;
};

class LuminanceTinter final {
public:
    // PackGen-style tint into a fresh buffer; order base → C1 → C2 → glow. Outline/alpha-0 pass through.
    static ImageBuffer apply(ImageBuffer const& source,
                             MaskSet const& masks,
                             TintColors const& colors,
                             TinterOptions options = {});

private:
    LuminanceTinter() = delete;
};

}  // namespace paimon::texture_studio
