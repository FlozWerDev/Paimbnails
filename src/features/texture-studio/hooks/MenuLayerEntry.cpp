// MenuLayerEntry.cpp - Adds a "Texture Studio" button to MenuLayer's
// bottom-menu. Hook priority is set to run AFTER geode.node-ids so the
// "bottom-menu" string ID is reliably present.
//
// We're idempotent: if the button is already there (rare, but possible
// when other mods cause MenuLayer::init to fire twice), we don't add a
// second one. Visibility is gated by a mod setting so users who don't
// want the button can hide it.

#include "../ui/TextureStudioLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

namespace {

constexpr auto kButtonID = "paimbnails-texture-studio-btn";

bool textureStudioEnabled() {
    // Default true so users see the button after install. They can hide
    // it via the mod settings.
    return Mod::get()->getSettingValue<bool>("texture-studio-enabled");
}

}  // anonymous namespace

class $modify(PaimonTextureStudioMenuHook, MenuLayer) {
    static void onModify(auto& self) {
        // Run AFTER node-ids so "bottom-menu" is present.
        if (!self.setHookPriorityAfterPost("MenuLayer::init", "geode.node-ids")) {
            // node-ids not installed — fall back to Late priority. The
            // string ID may not exist; we cope below by checking null.
            (void)self.setHookPriorityPost("MenuLayer::init", Priority::Late);
        }
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!textureStudioEnabled()) return true;

        auto* menu = this->getChildByID("bottom-menu");
        if (!menu) {
            // Fallback to first CCMenu — keeps the button visible even
            // when node-ids isn't installed (degraded UX but works).
            menu = this->getChildByType<CCMenu>(0);
        }
        if (!menu) return true;

        // Idempotent guard.
        if (menu->getChildByID(kButtonID)) return true;

        // Build the button. Use a circle base sprite so it visually fits
        // alongside vanilla GD buttons (gear, robot, etc.).
        char const* iconName = "GJ_paintBtn_001.png";
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(iconName)) {
            // Fallback to the gear icon — guaranteed present in 2.2081.
            iconName = "GJ_optionsBtn_001.png";
        }
        auto* base = CircleButtonSprite::createWithSpriteFrameName(
            iconName, 1.0f, CircleBaseColor::Pink, CircleBaseSize::Medium);
        if (!base) return true;

        auto* btn = CCMenuItemExt::createSpriteExtra(base,
            [](CCMenuItemSpriteExtra*) {
                if (auto* layer = paimon::texture_studio::TextureStudioLayer::create()) {
                    layer->show();
                }
            });
        btn->setID(kButtonID);

        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
};
