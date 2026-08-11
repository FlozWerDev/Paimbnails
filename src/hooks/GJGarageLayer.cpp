#include <Geode/modify/GJGarageLayer.hpp>
#include "../framework/HookConventions.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/colorful-icons/hooks/PaimonIconsGarageGlue.hpp"
#include "../features/garage-hub/GarageButtonHub.hpp"
#include "../features/icon-copy/hooks/IconCopyGarageGlue.hpp"
#include "../features/icon-gallery/hooks/IconStoreGarageGlue.hpp"
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
        // Inject the gear button + listen for config changes that should
        // re-color the open kit. El Creador de Iconos cuelga de ese mismo
        // popup, asi que no necesita boton propio aqui.
        paimon::icons::garage::onGarageInit(this);
        paimon::iconcopy::garage::onGarageInit(this);
        paimon::icon_gallery::garage::onGarageInit(this);
        // Los accesos de arriba ya no se apilan en la columna: cuelgan del hub,
        // y este es el unico boton que se ve.
        paimon::garage_hub::installHubButton(this);
        // Stats Display API lays out its children one frame after garage init,
        // but leaves the menu anchor at the right edge instead of its center.
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
