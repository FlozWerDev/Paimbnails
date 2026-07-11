#include <Geode/modify/GJGarageLayer.hpp>
#include "../framework/HookConventions.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/colorful-icons/hooks/PaimonIconsGarageGlue.hpp"

using namespace geode::prelude;

class $modify(PaimonGJGarageLayer, GJGarageLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJGarageLayer::init");
    }

    $override
    bool init() {
        if (!GJGarageLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "garage");
        // Inject the gear button + listen for config changes that should
        // re-color the open kit.
        paimon::icons::garage::onGarageInit(this);
        return true;
    }

    $override
    void playerColorChanged() {
        GJGarageLayer::playerColorChanged();
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    $override
    void listButtonBarSwitchedPage(ListButtonBar* bar, int page) {
        GJGarageLayer::listButtonBarSwitchedPage(bar, page);
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    void onSelectTab(cocos2d::CCObject* sender) {
        GJGarageLayer::onSelectTab(sender);
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    void setupPage(int page, IconType type) {
        GJGarageLayer::setupPage(page, type);
        paimon::icons::garage::onPlayerColorChanged(this);
    }
};

