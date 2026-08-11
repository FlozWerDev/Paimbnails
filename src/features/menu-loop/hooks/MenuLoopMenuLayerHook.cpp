#include <Geode/modify/MenuLayer.hpp>
#include "../../../framework/HookConventions.hpp"
#include <Geode/modify/GameManager.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <chrono>
#include "../services/MenuLoopManager.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"

using namespace geode::prelude;
using namespace paimon::menuloop;

namespace {
    bool s_shownWarning = false;
}

// Hook GameManager: hook getMenuMusicFile() to return the custom path rather than
// reimplementing playMenuMusic(), so GD's channel-0 load/resume logic is reused.
class $modify(PaimonMenuLoopGameManager, GameManager) {
    $override
    gd::string getMenuMusicFile() {
        auto& sm = MenuLoopManager::get();
        const std::string& song = sm.getCurrentSong();
        if (song.empty() || song == "menuLoop.mp3") {
            return GameManager::getMenuMusicFile();
        }
        // Cache existence by path with a 5s TTL to avoid blocking the main thread on every call.
        static std::string s_cachedPath;
        static bool s_cachedValid = false;
        static std::chrono::steady_clock::time_point s_cachedAt{};

        auto now = std::chrono::steady_clock::now();
        bool stale = std::chrono::duration_cast<std::chrono::seconds>(
            now - s_cachedAt).count() >= 5;
        if (s_cachedPath == song && !stale) {
            if (!s_cachedValid) return GameManager::getMenuMusicFile();
            return song;
        }

        std::error_code ec;
        bool exists = std::filesystem::is_regular_file(song, ec);
        s_cachedPath = song;
        s_cachedValid = exists;
        s_cachedAt = now;

        if (!exists) {
            return GameManager::getMenuMusicFile();
        }
        return song;
    }

    $override
    void fadeInMenuMusic() {
        GameManager::fadeInMenuMusic();
        auto& sm = MenuLoopManager::get();
        if (sm.getShouldRestoreMenuLoopPoint()) {
            sm.restoreLastMenuLoopPosition();
        }
    }

    $override
    void encodeDataTo(DS_Dictionary* dict) {
        MenuLoopManager::get().saveLastMenuLoop();
        paimon::menumusic::MenuMusicLibrary::get().save();
        GameManager::encodeDataTo(dict);
    }
};

class $modify(PaimonMenuLoopMenuLayer, MenuLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) return false;

        auto& sm = MenuLoopManager::get();
        auto* loader = Loader::get();

        if (auto* geodify = loader->getLoadedMod("omgrod.geodify")) {
            sm.setGeodify(geodify->getSettingValue<bool>("menu-loop"));
        }
        sm.setSawbladeCustomSongsFolder(loader->isModLoaded("sawblade.custom_song_folder"));
        if (auto* colonStartTime = loader->getLoadedMod("colon.menu_loop_start_time")) {
            sm.setColonMenuLoopStartTime(colonStartTime);
        }

        // Conflict warning — queue so it survives MenuLayer init / transitions
        if (!s_shownWarning && sm.getVibecodedVentilla() && loader->isModLoaded("joseii.ventilla")) {
            auto popup = PopupManager::get().alert(
                "Uh oh!",
                "<c_>Another mod overriding the menu loop is active!</c>\n"
                "<cy>Please check your loaded mods.</c>",
                "I Understand",
                nullptr,
                420.f
            );
            popup.setPriority(true);
            popup.showQueue();
            s_shownWarning = true;
        }

        return true;
    }
};
