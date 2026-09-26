#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include "../utils/PaimonNotification.hpp"
#include "ModAuthFlow.hpp"
#include <array>
#include <filesystem>

#ifdef GEODE_IS_WINDOWS
#include <shellapi.h>
#endif

using namespace geode::prelude;

namespace {
void openFolderNative(std::string const& pathStr) {
#ifdef GEODE_IS_WINDOWS
    ShellExecuteA(nullptr, "open", pathStr.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(GEODE_IS_MACOS)
    std::string cmd = "open \"" + pathStr + "\"";
    std::system(cmd.c_str());
#else
    (void)pathStr;
#endif
}

void revealFolder(std::filesystem::path const& dir, char const* doneMsg) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto pathStr = geode::utils::string::pathToString(dir);
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    openFolderNative(pathStr);
    PaimonNotify::create(doneMsg, NotificationIcon::Success)->show();
#else
    PaimonNotify::create("Carpeta: " + pathStr, NotificationIcon::Info)->show();
#ifndef GEODE_IS_ANDROID
    PaimonNotify::create(doneMsg, NotificationIcon::Success)->show();
#endif
#endif
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
        revealFolder(Mod::get()->getSaveDir() / "menu-music",
            "Menu music folder opened (cover-debug.log is here).");
    }).leak();

    ButtonSettingPressedEventV3(Mod::get(), "open-thumbnails-folder").listen([](auto buttonKey) {
        if (buttonKey != "run") return;
        revealFolder(paimon::quality::cacheDir(), "Carpeta de thumbnails abierta.");
    }).leak();
}
