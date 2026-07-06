#pragma once

#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <functional>

namespace paimon::thumbnails::levelinfo {

using ApplyBackgroundFn = std::function<void(cocos2d::CCTexture2D*)>;
using HasBackgroundFn = std::function<bool()>;
using LevelIdFn = std::function<int()>;

void requestHeroBackground(
    int levelID,
    geode::Ref<cocos2d::CCNode> layerAnchor,
    HasBackgroundFn hasBackground,
    LevelIdFn currentLevelId,
    ApplyBackgroundFn applyBackground
);

} // namespace paimon::thumbnails::levelinfo