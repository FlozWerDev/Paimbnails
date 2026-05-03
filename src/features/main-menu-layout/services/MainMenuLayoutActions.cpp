#include "MainMenuLayoutManager.hpp"

#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

$execute {
    ButtonSettingPressedEventV3(Mod::get(), "main-menu-layout-reset").listen([](auto buttonKey) {
        if (buttonKey != "run") return;

        auto& manager = paimon::menu_layout::MainMenuLayoutManager::get();
        manager.load();
        manager.resetAll();

        if (auto* scene = CCDirector::sharedDirector()->getRunningScene()) {
            if (auto* menuLayer = scene->getChildByType<MenuLayer>(0)) {
                manager.applyDefaults(menuLayer);
            }
        }

        PaimonNotify::show(Localization::get().getString("menu_layout.reset_saved"), NotificationIcon::Success);
    }).leak();
}
