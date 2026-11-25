#pragma once

#include "LiveMaskAnalyzer.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <array>
#include <cstddef>

namespace paimon::texture_studio {

using LiveTonePalette = std::array<cocos2d::ccColor3B, static_cast<std::size_t>(LiveTone::Count)>;

struct LiveToneInputs {
    cocos2d::ccColor3B primary{149, 226, 3};
    cocos2d::ccColor3B secondary{28, 233, 255};
    bool autoSurface = true;
    cocos2d::ccColor3B surface{60, 120, 130};
    bool autoBackground = true;
    cocos2d::ccColor3B background{70, 110, 20};
    // 0 = every family uses primary/secondary, 1 = strongest shade split.
    float spread = .6f;
};

// one palette color per vanilla color family. Families that have no color
// of their own get shades of the palette, so differently colored GD buttons
// stay distinguishable without leaving the chosen colors.
LiveTonePalette buildLiveTones(LiveToneInputs const& inputs);

cocos2d::ccColor3B autoSurfaceTone(cocos2d::ccColor3B primary, cocos2d::ccColor3B secondary);
cocos2d::ccColor3B autoBackgroundTone(cocos2d::ccColor3B primary, cocos2d::ccColor3B secondary);

char const* liveToneName(LiveTone tone);
// what vanilla UI a family covers, for the editor legend.
char const* liveToneSource(LiveTone tone);

// CPU twin of the shader's luminance tint, used for layer fills and swatches.
cocos2d::ccColor3B liveTintColor(cocos2d::ccColor3B source, cocos2d::ccColor3B tone,
                                 int brightness, float saturation, float contrast);

}  // namespace paimon::texture_studio
