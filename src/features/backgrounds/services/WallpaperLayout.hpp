#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace paimon::wallpaper {

// shared parent-chain visibility check (used by live and scene wallpapers).
inline bool isNodeVisibleInHierarchy(cocos2d::CCNode const* node) {
    for (auto* current = const_cast<cocos2d::CCNode*>(node); current != nullptr; current = current->getParent()) {
        if (!current->isVisible()) return false;
    }
    return true;
}

struct WallpaperLayout {
    float width = 1.f;
    float height = 1.f;
    float left = 0.f;
    float bottom = 0.f;
};

inline WallpaperLayout fitWallpaper(float sourceWidth, float sourceHeight,
    float viewportWidth, float viewportHeight, std::string_view mode) {
    if (!(sourceWidth > 0.f) || !(sourceHeight > 0.f) || !(viewportWidth > 0.f) || !(viewportHeight > 0.f)) return {};
    float const scaleX = viewportWidth / sourceWidth;
    float const scaleY = viewportHeight / sourceHeight;
    float width = viewportWidth;
    float height = viewportHeight;
    if (mode != "stretch") {
        float const scale = mode == "contain" ? std::min(scaleX, scaleY) : std::max(scaleX, scaleY);
        width = sourceWidth * scale;
        height = sourceHeight * scale;
    }
    return {width, height, (viewportWidth - width) / 2.f, (viewportHeight - height) / 2.f};
}

} // namespace paimon::wallpaper
