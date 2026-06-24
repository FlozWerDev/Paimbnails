#pragma once

// Reusable helpers extracted from hooks/LevelSearchLayer.cpp's anonymous namespace.

#include <cocos2d.h>
#include <Geode/binding/LevelSearchLayer.hpp>
#include <algorithm>

namespace paimon::levelsearch {

// Release the search input's focus state so no IME/keyboard listener survives a
// scene change (otherwise the text input keeps eating keys in gameplay).
void releaseSearchInputFocus(LevelSearchLayer* layer);

// CCMenu that ignores touches outside a bounds node's world rect. CCClippingNode
// only clips rendering, not input, so scrolled-off rows would still capture touches.
class BoundedTouchMenu : public cocos2d::CCMenu {
public:
    static BoundedTouchMenu* create() {
        auto ret = new BoundedTouchMenu();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        if (!cocos2d::CCMenu::init()) return false;
        return true;
    }

    // The menu doesn't retain the node; the caller must keep it alive.
    void setBoundsNode(cocos2d::CCNode* bounds) { m_boundsNode = bounds; }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        if (!isTouchInsideBounds(touch)) {
            return false;
        }
        return cocos2d::CCMenu::ccTouchBegan(touch, event);
    }

private:
    cocos2d::CCNode* m_boundsNode = nullptr;

    bool isTouchInsideBounds(cocos2d::CCTouch* touch) const {
        if (!m_boundsNode || !touch) return true;

        auto location = touch->getLocation();
        auto size = m_boundsNode->getContentSize();
        cocos2d::CCRect localRect{0.f, 0.f, size.width, size.height};
        auto worldOrigin = m_boundsNode->convertToWorldSpace({localRect.origin.x, localRect.origin.y});
        auto worldOpposite = m_boundsNode->convertToWorldSpace({localRect.getMaxX(), localRect.getMaxY()});
        float minX = std::min(worldOrigin.x, worldOpposite.x);
        float minY = std::min(worldOrigin.y, worldOpposite.y);
        float maxX = std::max(worldOrigin.x, worldOpposite.x);
        float maxY = std::max(worldOrigin.y, worldOpposite.y);
        return location.x >= minX && location.x <= maxX &&
               location.y >= minY && location.y <= maxY;
    }
};

} // namespace paimon::levelsearch
