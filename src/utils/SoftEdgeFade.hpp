#pragma once

#include <Geode/Geode.hpp>

namespace paimon {

struct SoftEdgeFade {
    float amount = 0.f;
    float skew = 0.f;
};

bool drawSoftEdgeFade(cocos2d::CCSprite* sprite, SoftEdgeFade const& fade);

} // namespace paimon
