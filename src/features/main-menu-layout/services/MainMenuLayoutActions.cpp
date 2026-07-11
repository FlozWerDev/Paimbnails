#include "MainMenuLayoutManager.hpp"

#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

$execute {
    ButtonSettingPressedEventV3(Mod::get(), "main-menu-layout-reset").listen([](auto buttonKey) {
        if (buttonKey != "run") return;

        auto& manager = paimon::menu_layout::MainMenuLayoutManager::get();
        manager.load();
        manager.resetAll();

        // Re-apply defaults only in supported scenes (MenuLayer and PauseLayer).
        if (auto* scene = CCDirector::get()->getRunningScene()) {
            if (auto* menuLayer = scene->getChildByType<MenuLayer>(0)) {
                manager.applyDefaults(menuLayer);
            }
            if (auto* pauseLayer = scene->getChildByType<PauseLayer>(0)) {
                manager.applyDefaults(pauseLayer);
            }
        }

        PaimonNotify::show(Localization::get().getString("menu_layout.reset_saved"), NotificationIcon::Success);
    }).leak();
}
