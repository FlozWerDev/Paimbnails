#include "../utils/FilesystemPath.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include "../utils/PaimonNotification.hpp"
#include "ModAuthFlow.hpp"
#include "QualityConfig.hpp"
#include <array>
#include <filesystem>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
void revealFolder(std::filesystem::path const& dir, char const* doneMsg) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto pathStr = paimon::paths::toUtf8(dir);
    if (!ec && geode::utils::file::openFolder(paimon::paths::toSdkPath(dir))) {
        PaimonNotify::show(doneMsg, NotificationIcon::Success);
    } else {
        PaimonNotify::show("Carpeta: " + pathStr, NotificationIcon::Info);
    }
}

} // namespace

$execute {
    for (char const* key : std::array<char const*, 2>{
             "maintenance-refresh-mod-code", "maintenance-copy-mod-code"}) {
        ButtonSettingPressedEventV3(Mod::get(), key).listen([](auto buttonKey) {
            if (buttonKey != "run") return;
            paimon::modauth::showPanel();
        }).leak();
    }

    ButtonSettingPressedEventV3(Mod::get(), "open-menu-music-folder").listen([](auto buttonKey) {
        if (buttonKey != "run") return;
        revealFolder(paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "menu-music",
            "Menu music folder opened (cover-debug.log is here).");
    }).leak();

    ButtonSettingPressedEventV3(Mod::get(), "open-thumbnails-folder").listen([](auto buttonKey) {
        if (buttonKey != "run") return;
        revealFolder(paimon::quality::cacheDir(), "Carpeta de thumbnails abierta.");
    }).leak();
}
