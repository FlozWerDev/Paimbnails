#pragma once

#include <Geode/utils/cocos.hpp>
#include <string>

class FLAlertLayer;

namespace paimon::popuptheme {

struct ThemeConfig {
    std::string id = "gd";
    bool forceBlur = false;
    // -1 => keep the user's popup-blur-darkness; >= 0 overrides it for this theme.
    float blurDarknessOverride = -1.f;
    int bgOpacity = 255;
    cocos2d::ccColor3B bgColor = {255, 255, 255};
    bool hasBorders = false;
    float borderThickness = 0.f;
    cocos2d::ccColor4B borderColor = {0, 0, 0, 0};

    bool animatedBorder = false;
    bool softShadow = false;
};

void applyTheme(FLAlertLayer* popup, bool blurAlreadyApplied);
void cleanupTheme(FLAlertLayer* popup);

} // namespace paimon::popuptheme
