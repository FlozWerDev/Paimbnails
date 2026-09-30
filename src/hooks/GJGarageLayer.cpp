#include <Geode/modify/GJGarageLayer.hpp>
#include "../framework/HookConventions.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/colorful-icons/hooks/PaimonIconsGarageGlue.hpp"
#include "../features/garage-hub/GarageButtonHub.hpp"
#include "../features/icon-copy/hooks/IconCopyGarageGlue.hpp"
#include "../features/icon-maker/hooks/IconMakerGarageGlue.hpp"

using namespace geode::prelude;

class $modify(PaimonGJGarageLayer, GJGarageLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJGarageLayer::init");
    }

    void fixStatsMenuPosition(float) {
        auto* statsMenu = this->getChildByIDRecursive(
            "capeling.garage-stats-menu/stats-menu"
        );
        if (!statsMenu) return;

        statsMenu->updateLayout();
        auto const width = statsMenu->getContentSize().width;
        if (width <= 0.f) return;

        statsMenu->setPositionX(statsMenu->getPositionX() - width * 0.5f);
    }

    $override
    bool init() {
        if (!GJGarageLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "garage");
        // gear button + re-color on config change; icon maker hangs off the same popup
        paimon::icons::garage::onGarageInit(this);
        paimon::iconcopy::garage::onGarageInit(this);
        // accesses hang off the hub; this is the only visible button
        paimon::garage_hub::installHubButton(this);
        // stats display api lays out one frame late with the anchor at the edge
        this->scheduleOnce(schedule_selector(PaimonGJGarageLayer::fixStatsMenuPosition), 0.f);
        return true;
    }

    $override
    void playerColorChanged() {
        GJGarageLayer::playerColorChanged();
        paimon::icons::garage::onPlayerColorChanged(this);
        paimon::icon_maker::garage::onPlayerColorChanged(this);
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
