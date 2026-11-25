#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <memory>

namespace paimon::compat {

// these helpers use the public state/init API while the 2.2091 Windows
// bindings lack implementations for the corresponding convenience methods.
inline LevelInfoLayer* createLevelInfoLayer(GJGameLevel* level, bool challenge) {
    if (!level) return nullptr;
    auto layer = std::make_unique<LevelInfoLayer>();
    if (!layer->init(level, challenge)) return nullptr;
    layer->autorelease();
    return layer.release();
}

inline int averageDifficulty(GJGameLevel const* level) {
    if (!level || level->m_ratings <= 0) return 0;
    return level->m_ratingsSum / level->m_ratings;
}

inline void setPageIndicatorsVisible(BoomScrollLayer* scroll, bool visible) {
    if (!scroll) return;
    scroll->m_dotsVisible = visible;
    if (!scroll->m_dots) return;
    for (auto* dot : geode::cocos::CCArrayExt<cocos2d::CCNode*>(scroll->m_dots)) {
        if (dot) dot->setVisible(visible);
    }
}

} // namespace paimon::compat
