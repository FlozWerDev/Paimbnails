
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include "services/AutoPreviewStore.hpp"
#include "../../utils/PaimonNotification.hpp"

using namespace geode::prelude;

$execute {
    ButtonSettingPressedEventV3(Mod::get(), "auto-preview-clear").listen([](auto buttonKey) {
        if (buttonKey != "run") return;
        paimon::autopreview::AutoPreviewStore::get().clearAll();
        PaimonNotify::create("Generated previews cleared.", NotificationIcon::Success)->show();
    }).leak();
}
