#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include "../../../utils/SpriteHelper.hpp"

// Row building blocks shared by the layer editor and the asset browser lists.
namespace paimon::capture::ui {

// Clips touch testing to a parent ScrollLayer's visible rect, so rows scrolled
// out of view cannot swallow taps meant for the popup underneath.
class ClippedMenu : public cocos2d::CCMenu {
public:
    static ClippedMenu* create(cocos2d::CCNode* clipParent) {
        auto ret = new ClippedMenu();
        if (ret && ret->init()) {
            ret->m_clipParent = clipParent;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        if (m_clipParent) {
            auto worldPt = touch->getLocation();
            auto parentPos = m_clipParent->convertToWorldSpace({0.f, 0.f});
            auto parentSize = m_clipParent->getContentSize();
            auto parentScale = m_clipParent->getScale();
            float w = parentSize.width * parentScale;
            float h = parentSize.height * parentScale;
            if (worldPt.x < parentPos.x || worldPt.x > parentPos.x + w ||
                worldPt.y < parentPos.y || worldPt.y > parentPos.y + h) {
                return false;
            }
        }
        return CCMenu::ccTouchBegan(touch, event);
    }

private:
    cocos2d::CCNode* m_clipParent = nullptr;
};

inline cocos2d::CCLayerColor* makeRowFill(float width, float height, cocos2d::ccColor4B color) {
    auto* fill = cocos2d::CCLayerColor::create(color);
    if (!fill) return nullptr;
    fill->setContentSize({width, height});
    fill->ignoreAnchorPointForPosition(false);
    fill->setAnchorPoint({0.f, 0.f});
    fill->setPosition({0.f, 0.f});
    return fill;
}

// Checkbox for a list row. `tint` marks a group that is only partly visible.
inline CCMenuItemToggler* makeCheck(
    float scale, cocos2d::CCObject* target, cocos2d::SEL_MenuHandler selector,
    int tag, bool on, cocos2d::ccColor3B tint = {255, 255, 255}
) {
    auto* onSpr  = cocos2d::CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    auto* offSpr = cocos2d::CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    if (!onSpr || !offSpr) return nullptr;

    onSpr->setScale(scale);
    offSpr->setScale(scale);
    onSpr->setColor(tint);

    auto* toggler = CCMenuItemToggler::create(offSpr, onSpr, target, selector);
    if (!toggler) return nullptr;
    toggler->setTag(tag);
    toggler->toggle(on);
    return toggler;
}

// Disclosure triangle. GD's fonts have no arrow glyph, so this is a rotated
// sprite with a "+"/"-" fallback if the frame is missing.
inline cocos2d::CCNode* makeDisclosure(bool expanded, float scale) {
    if (auto* arrow = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png")) {
        arrow->setScale(scale);
        arrow->setRotation(expanded ? 270.f : 180.f);
        arrow->setOpacity(expanded ? 255 : 190);
        return arrow;
    }
    auto* label = cocos2d::CCLabelBMFont::create(expanded ? "-" : "+", "bigFont.fnt");
    if (label) label->setScale(0.4f);
    return label;
}

// Invisible full-row hit area, so a row reacts to a tap anywhere on it.
inline CCMenuItemSpriteExtra* makeRowHitArea(
    float width, float height, cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler selector, int tag
) {
    auto* hit = paimon::SpriteHelper::createColorPanel(width, height, {0, 0, 0}, 0);
    if (!hit) return nullptr;
    auto* btn = CCMenuItemSpriteExtra::create(hit, target, selector);
    if (!btn) return nullptr;
    btn->setTag(tag);
    btn->setPosition({width * 0.5f, height * 0.5f});
    return btn;
}

} // namespace paimon::capture::ui
