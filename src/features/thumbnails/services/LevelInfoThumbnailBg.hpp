#pragma once

// Fondo de LevelInfoLayer: RAM/disco primero, red despues (alineado con cdc.level_thumbnails).

#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <functional>

namespace paimon::thumbnails::levelinfo {

using ApplyBackgroundFn = std::function<void(cocos2d::CCTexture2D*)>;
using HasBackgroundFn = std::function<bool()>;
using LevelIdFn = std::function<int()>;

// Intenta textura sincrona (RAM) y, si hace falta, encola requestLoad con prioridad hero.
void requestHeroBackground(
    int levelID,
    geode::Ref<cocos2d::CCNode> layerAnchor,
    HasBackgroundFn hasBackground,
    LevelIdFn currentLevelId,
    ApplyBackgroundFn applyBackground
);

} // namespace paimon::thumbnails::levelinfo