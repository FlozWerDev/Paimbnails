#pragma once

#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <memory>

namespace paimon::profile_redesign {

class ProfileScrollLayer : public geode::ScrollLayer {
public:
    ProfileScrollLayer(cocos2d::CCRect const& rect, bool sw, bool vertical)
        : geode::ScrollLayer(rect, sw, vertical) {}

    static ProfileScrollLayer* create(cocos2d::CCSize const& size) {
        auto node = std::make_unique<ProfileScrollLayer>(cocos2d::CCRect{0.f, 0.f, size.width, size.height}, true, true);
        node->setAnchorPoint({0.f, 0.f});
        node->autorelease();
        return node.release();
    }

    static cocos2d::CCRect worldRect(cocos2d::CCNode* node) {
        auto const bottom = node->convertToWorldSpace({0.f, 0.f});
        auto const top = node->convertToWorldSpace(node->getContentSize());
        return {std::min(bottom.x, top.x), std::min(bottom.y, top.y),
            std::fabs(top.x - bottom.x), std::fabs(top.y - bottom.y)};
    }

    bool scrollWheel(float y, float x) override {
        if (!geode::cocos::nodeIsVisible(this) || !worldRect(this).containsPoint(geode::cocos::getMousePos())) return false;
        return geode::ScrollLayer::scrollWheel(y, x);
    }
};

} // namespace paimon::profile_redesign
