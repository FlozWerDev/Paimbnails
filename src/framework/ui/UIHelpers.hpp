#pragma once

// UIHelpers.hpp — Factory functions for reusable UI components.
// Cuts repeated UI-creation code across popups and layers.

#include <Geode/Geode.hpp>
#include "../../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace paimon::ui {

// Create a horizontal footer menu (bottom row of a popup).
inline cocos2d::CCMenu* makeFooterMenu(
    std::initializer_list<cocos2d::CCNode*> items,
    float gap = 10.f
) {
    auto menu = cocos2d::CCMenu::create();
    menu->setContentSize({0, 0});
    menu->setLayout(
        RowLayout::create()
            ->setGap(gap)
            ->setAxisAlignment(AxisAlignment::Center)
    );

    for (auto* item : items) {
        if (item) menu->addChild(item);
    }

    menu->updateLayout();
    return menu;
}

// Create a circular icon button (GJ style: circle with a sprite inside).
inline CCMenuItemSpriteExtra* makeCircleIconButton(
    char const* spriteName,
    cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler callback,
    float scale = 1.0f
) {
    auto spr = paimon::SpriteHelper::safeCreateWithFrameName(spriteName);
    if (!spr) spr = paimon::SpriteHelper::safeCreate(spriteName);
    if (!spr) return nullptr;

    spr->setScale(scale);

    auto btn = CCMenuItemSpriteExtra::create(spr, target, callback);
    return btn;
}

inline void addToMenuAndUpdate(cocos2d::CCMenu* menu, cocos2d::CCNode* child) {
    if (!menu || !child) return;
    menu->addChild(child);
    menu->updateLayout();
}

// Aspect-fill: scale so a sprite fills an area without distortion.
inline float aspectFillScale(cocos2d::CCSize spriteSize, cocos2d::CCSize targetSize) {
    if (spriteSize.width <= 0 || spriteSize.height <= 0) return 1.f;
    float sx = targetSize.width  / spriteSize.width;
    float sy = targetSize.height / spriteSize.height;
    return std::max(sx, sy);
}

// Aspect-fit: scale so a sprite fits an area without distortion.
inline float aspectFitScale(cocos2d::CCSize spriteSize, cocos2d::CCSize targetSize) {
    if (spriteSize.width <= 0 || spriteSize.height <= 0) return 1.f;
    float sx = targetSize.width  / spriteSize.width;
    float sy = targetSize.height / spriteSize.height;
    return std::min(sx, sy);
}

// Normalize an icon sprite to fit targetSize x targetSize.
inline void normalizeIconSprite(cocos2d::CCSprite* spr, float targetSize) {
    if (!spr) return;
    auto cs = spr->getContentSize();
    float maxDim = std::max(cs.width, cs.height);
    if (maxDim > 0.f) spr->setScale(targetSize / maxDim);
}

// Create a section title (goldFont, centered).
inline cocos2d::CCLabelBMFont* makeSectionTitle(
    char const* text, float scale = 0.45f
) {
    auto label = cocos2d::CCLabelBMFont::create(text, "goldFont.fnt");
    if (label) label->setScale(scale);
    return label;
}

// Create a dark panel via SpriteHelper.
inline cocos2d::CCNode* makeDarkPanel(float width, float height, unsigned char alpha = 80) {
    return paimon::SpriteHelper::createDarkPanel(width, height, alpha);
}

} // namespace paimon::ui
