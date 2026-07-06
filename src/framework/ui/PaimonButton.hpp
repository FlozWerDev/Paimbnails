#pragma once

// PaimonButton.hpp — Mod button factory with auto-registration.
// Saves each hook from repeating the create + register + store-scale pattern.

#include <Geode/Geode.hpp>
#include "../../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace paimon::ui {

class PaimonButton {
public:
    // Create a button registered as a mod button. Original scale is tracked via an
    // ID prefix (CCMenuItemSpriteExtra has no user field for it).
    static CCMenuItemSpriteExtra* create(
        cocos2d::CCNode* sprite,
        cocos2d::CCObject* target,
        cocos2d::SEL_MenuHandler callback
    ) {
        auto btn = CCMenuItemSpriteExtra::create(sprite, target, callback);
        if (!btn) return nullptr;

        registerAsPaimon(btn);
        return btn;
    }

    static CCMenuItemSpriteExtra* createFromSprite(
        char const* spriteName,
        cocos2d::CCObject* target,
        cocos2d::SEL_MenuHandler callback,
        float scale = 1.0f
    ) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName(spriteName);
        if (!spr) {
            spr = paimon::SpriteHelper::safeCreate(spriteName);
        }
        if (!spr) return nullptr;

        spr->setScale(scale);
        return create(spr, target, callback);
    }

    // Register an existing button as a Paimon button (back-compat).
    static void registerAsPaimon(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        std::string currentID = btn->getID();
        if (!currentID.starts_with("paimon-mod-btn")) {
            btn->setID(currentID.empty()
                ? "paimon-mod-btn"
                : ("paimon-mod-btn-" + currentID));
        }
    }

    // Check whether a button is a Paimon button.
    static bool isPaimonButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        return btn->getID().starts_with("paimon-mod-btn");
    }
};

} // namespace paimon::ui
