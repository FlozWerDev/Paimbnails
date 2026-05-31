#include <Geode/modify/GJGarageLayer.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/colorful-icons/hooks/PaimonIconsGarageGlue.hpp"

using namespace geode::prelude;

class $modify(PaimonGJGarageLayer, GJGarageLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("GJGarageLayer::init", "geode.node-ids");
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

    // Triggered every time the user changes player color/glow in the kit.
    // Drives the live re-color of the visible button bar.
    $override
    void playerColorChanged() {
        GJGarageLayer::playerColorChanged();
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    // Triggered every time the user swipes the bar to another page.
    // Geometry Dash rebuilds the visible icons here, so we must re-apply
    // our recolor right after the original work completes.
    $override
    void listButtonBarSwitchedPage(ListButtonBar* bar, int page) {
        GJGarageLayer::listButtonBarSwitchedPage(bar, page);
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    // Triggered when the user clicks one of the gamemode tab buttons
    // (Cube / Ship / Ball / ...). The bar is rebuilt from scratch, so we
    // need to re-apply our recolor right after.
    void onSelectTab(cocos2d::CCObject* sender) {
        GJGarageLayer::onSelectTab(sender);
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    // Triggered when the bar is rebuilt for a given page. Same reasoning.
    void setupPage(int page, IconType type) {
        GJGarageLayer::setupPage(page, type);
        paimon::icons::garage::onPlayerColorChanged(this);
    }
};

