#include "TwitchRequestManager.hpp"
#include "services/StreamOverlayServer.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

using paimon::twitch::Platform;
using paimon::twitch::TwitchRequestManager;

$on_mod(Loaded) {
    listenForSettingChanges<bool>("twitch-requests-enabled", +[](bool) {
        TwitchRequestManager::get().restart();
        paimon::twitch::StreamOverlayServer::get().restart();
    });
    listenForSettingChanges<std::string>("twitch-requests-channel", +[](std::string) {
        TwitchRequestManager::get().restart(Platform::Twitch);
    });
    listenForSettingChanges<std::string>("twitch-requests-youtube-channel", +[](std::string) {
        TwitchRequestManager::get().restart(Platform::YouTube);
    });
    listenForSettingChanges<std::string>("twitch-requests-kick-channel", +[](std::string) {
        TwitchRequestManager::get().restart(Platform::Kick);
    });
    listenForSettingChanges<std::string>("twitch-requests-tiktok-channel", +[](std::string) {
        TwitchRequestManager::get().restart(Platform::TikTok);
    });
    listenForSettingChanges<bool>("twitch-requests-web-enabled", +[](bool) {
        TwitchRequestManager::get().restartWebRequests();
    });
    listenForSettingChanges<bool>("twitch-requests-obs-enabled", +[](bool) {
        paimon::twitch::StreamOverlayServer::get().restart();
    });
}

$on_game(Loaded) {
    TwitchRequestManager::get().init();
    paimon::twitch::StreamOverlayServer::get().init();
}

$on_game(Exiting) {
    paimon::twitch::StreamOverlayServer::get().shutdown();
    TwitchRequestManager::get().shutdown();
}
