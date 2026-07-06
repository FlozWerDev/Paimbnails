// MenuLayerEntry.cpp - Adds a "Texture Studio" button to MenuLayer's
// bottom-menu. Hook priority runs AFTER geode.node-ids so the
// "bottom-menu" string ID is reliably present.

#include "../ui/TextureStudioLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

namespace {

constexpr auto kButtonID = "paimbnails-texture-studio-btn";

bool textureStudioEnabled() {
    return Mod::get()->getSettingValue<bool>("texture-studio-enabled");
}

}  // anonymous namespace

class $modify(PaimonTextureStudioMenuHook, MenuLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!textureStudioEnabled()) return true;

        auto* menu = this->getChildByID("bottom-menu");
        if (!menu) {
            menu = this->getChildByType<CCMenu>(0);
        }
        if (!menu) return true;

        if (menu->getChildByID(kButtonID)) return true;

        char const* iconName = "GJ_paintBtn_001.png";
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(iconName)) {
            iconName = "GJ_optionsBtn_001.png";
        }
        auto* base = CircleButtonSprite::createWithSpriteFrameName(
            iconName, 1.0f, CircleBaseColor::Pink, CircleBaseSize::Medium);
        if (!base) return true;

        auto* btn = CCMenuItemExt::createSpriteExtra(base,
            [](CCMenuItemSpriteExtra*) {
                paimon::texture_studio::TextureStudioLayer::open();
            });
        btn->setID(kButtonID);

        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
};
