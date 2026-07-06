#pragma once

#include "MenuLoopManager.hpp"
#include "../../menu-music/services/MenuMusicCopy.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/GeodeUI.hpp>

namespace paimon::menuloop {

// VANILLA_GD_MENU_LOOP_DISABLED: true if user disabled menu music in GD settings
inline bool isVanillaMenuLoopDisabled() {
    auto* gm = GameManager::get();
    return gm && gm->getGameVariable("0122");
}

namespace MenuLoopControl {

    inline void woahThereBuddy(const std::string& reason) {
        geode::createQuickPopup(
            "Menu Loop", reason,
            "Never Mind", "Open Mod Settings",
            [](FLAlertLayer*, bool openConfig) {
                if (!openConfig) return;
                geode::openSettingsPopup(Mod::get(), false);
            }
        );
    }

    // Shuffle
    inline void shuffleSong() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (!sm.getFinishedCalculatingSongLengths()) {
            Notification::create("Menu Loop is still busy. Try again in a bit!", NotificationIcon::Warning)->show();
            return;
        }
        if (sm.isOverride()) {
            woahThereBuddy("You're currently playing a menu loop <cy>override</c>. Check your settings.");
            return;
        }
        if (sm.songSizeIsBad()) {
            woahThereBuddy("You don't have enough songs. Visit the config directory through mod settings.");
            return;
        }

        FMODAudioEngine::get()->m_backgroundMusicChannel->stop();
        const std::string& songToBeStored = sm.getCurrentSong();
        if (!songToBeStored.empty()) {
            if (sm.getSongToSongDataEntries().contains(songToBeStored) &&
                sm.getSongToSongDataEntries()[songToBeStored].type != SongType::Blacklisted) {
                sm.setPreviousSong(songToBeStored);
            }
        }
        sm.pickRandomSong();
        if (!sm.isOverride()) Mod::get()->setSavedValue<std::string>("lastMenuLoop", sm.getCurrentSong());
        GameManager::sharedState()->playMenuMusic();
    }

    // Constant Shuffle (auto-advance when song ends)
    inline void constantShuffleModeNewSong() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (!sm.getConstantShuffleMode()) return shuffleSong();

        auto* fmod = FMODAudioEngine::get();
        float vol = 0.f;
        auto res = fmod->m_backgroundMusicChannel->getVolume(&vol);
        if (fmod->m_musicVolume <= 0.0f || fmod->getBackgroundMusicVolume() <= 0.0f || vol <= 0.0f) {
            if (sm.getAdvancedLogs()) log::info("MISSION ABORT — volume is 0");
            return;
        }

        fmod->m_backgroundMusicChannel->stop();
        const std::string& songToBeStored = sm.getCurrentSong();
        if (!songToBeStored.empty()) {
            if (sm.getSongToSongDataEntries().contains(songToBeStored) &&
                sm.getSongToSongDataEntries()[songToBeStored].type != SongType::Blacklisted) {
                sm.setPreviousSong(songToBeStored);
            }
        }
        sm.pickRandomSong();
        if (sm.getCalledOnce() || !Mod::get()->getSettingValue<bool>("menuLoopSaveSongOnGameClose")) {
            if (sm.getAdvancedLogs()) log::info("playing song as normal");
            if (!sm.isOverride()) Mod::get()->setSavedValue<std::string>("lastMenuLoop", sm.getCurrentSong());
        } else if (sm.isOverride()) {
            if (sm.getAdvancedLogs()) log::info("playing song from override");
            sm.setCurrentSong(sm.getOverrideSong());
        }
        GameManager::sharedState()->playMenuMusic();
        sm.setCalledOnce(true);
    }

    // Previous
    inline void previousSong() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (!sm.getFinishedCalculatingSongLengths()) {
            Notification::create("Menu Loop is still busy. Try again in a bit!", NotificationIcon::Warning)->show();
            return;
        }
        if (sm.isOverride()) {
            woahThereBuddy("You're currently playing a menu loop <cy>override</c>. Check your settings.");
            return;
        }
        if (sm.songSizeIsBad()) {
            woahThereBuddy("You don't have enough songs. Visit the config directory through mod settings.");
            return;
        }
        if (sm.isPreviousSong()) {
            Notification::create("You're already playing the previous song! :)", NotificationIcon::Info)->show();
            return;
        }
        const std::string& prev = sm.getPreviousSong();
        if (prev.empty()) {
            Notification::create("There's no previous song to go back to! :(", NotificationIcon::Info)->show();
            return;
        }
        FMODAudioEngine::get()->m_backgroundMusicChannel->stop();
        sm.setCurrentSong(prev);
        GameManager::sharedState()->playMenuMusic();
    }

    // Hold
    inline void holdSong() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (!sm.getFinishedCalculatingSongLengths()) {
            Notification::create("Menu Loop is still busy. Try again in a bit!", NotificationIcon::Warning)->show();
            return;
        }
        if (sm.isOverride()) {
            woahThereBuddy("You're currently playing a menu loop <cy>override</c>. Check your settings.");
            return;
        }
        if (sm.songSizeIsBad()) {
            woahThereBuddy("You don't have enough songs. Visit the config directory through mod settings.");
            return;
        }
        const std::string& formerHeld = sm.getHeldSong();
        const std::string& current = sm.getCurrentSong();
        if (current == formerHeld) {
            Notification::create("You're already holding that song! :D", NotificationIcon::Info)->show();
            return;
        }
        sm.setHeldSong(current);
        if (!formerHeld.empty()) {
            FMODAudioEngine::get()->m_backgroundMusicChannel->stop();
            sm.setCurrentSong(formerHeld);
            GameManager::sharedState()->playMenuMusic();
            return;
        }
        if (!sm.getConstantShuffleMode()) shuffleSong();
        else constantShuffleModeNewSong();
    }

    // Favorite
    inline void favoriteSong() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (!sm.getFinishedCalculatingSongLengths()) {
            Notification::create("Menu Loop is still busy. Try again in a bit!", NotificationIcon::Warning)->show();
            return;
        }
        if (sm.isOriginalMenuLoop()) {
            woahThereBuddy("There's nothing to favorite! Check your config folder.");
            return;
        }
        if (sm.isOverride()) {
            woahThereBuddy("You're trying to favorite your own <cy>override</c>. Check your settings.");
            return;
        }
        const std::string& current = sm.getCurrentSong();
        if (std::ranges::find(sm.getFavorites(), current) != sm.getFavorites().end()) {
            Notification::create("You've already favorited this song! :D", NotificationIcon::Info)->show();
            return;
        }
        if (std::ranges::find(sm.getBlacklist(), current) != sm.getBlacklist().end()) {
            woahThereBuddy("You've already blacklisted this song. Check your blacklist again.");
            return;
        }
        log::info("favoriting: {}", current);
        sm.addToFavorites();

        // Write to favorites.txt
        auto favPath = sm.getConfigDir() / "favorites.txt";
        {
            auto existing = geode::utils::file::readString(favPath).unwrapOr("");
            (void)geode::utils::file::writeString(favPath, existing + fmt::format("{}\n", current));
        }

        Notification::create(fmt::format("Favorited {}!", sm.getCurrentSongDisplayName()), NotificationIcon::Success)->show();
    }

    // Blacklist
    inline void blacklistSong() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (!sm.getFinishedCalculatingSongLengths()) {
            Notification::create("Menu Loop is still busy. Try again in a bit!", NotificationIcon::Warning)->show();
            return;
        }
        if (sm.isOriginalMenuLoop()) {
            woahThereBuddy("There's nothing to blacklist! Check your config folder.");
            return;
        }
        if (sm.isOverride()) {
            woahThereBuddy("You're trying to blacklist your own <cy>override</c>. Check your settings.");
            return;
        }
        const std::string& current = sm.getCurrentSong();
        if (std::ranges::find(sm.getBlacklist(), current) != sm.getBlacklist().end()) {
            woahThereBuddy("You've already blacklisted this song. Check your blacklist again.");
            return;
        }
        if (std::ranges::find(sm.getFavorites(), current) != sm.getFavorites().end()) {
            woahThereBuddy("You've already favorited this song! Check your favorites again.");
            return;
        }
        log::info("blacklisting: {}", current);
        sm.addToBlacklist();
        sm.removeSong(current);

        // Write to blacklist.txt
        auto blPath = sm.getConfigDir() / "blacklist.txt";
        {
            auto existing = geode::utils::file::readString(blPath).unwrapOr("");
            (void)geode::utils::file::writeString(blPath, existing + fmt::format("{}\n", current));
        }

        // Pick new song
        if (!sm.getConstantShuffleMode()) shuffleSong();
        else constantShuffleModeNewSong();

        Notification::create(fmt::format("Blacklisted {}. New song picked!", sm.getCurrentSongDisplayName()), NotificationIcon::Success)->show();
    }

    // Copy Song
    inline void copySong() {
        auto toCopy = paimon::menumusic::resolveActiveMenuMusicCopyValue();
        geode::utils::clipboard::write(toCopy);
        Notification::create(fmt::format("Copied: {}", toCopy), NotificationIcon::Success)->show();
    }

    // Playback progress (seek)
    //
    // Jump amount in milliseconds. If SHIFT is held, doubles;
    // CTRL+SHIFT triples (mimicking the reference mod).
    inline int getJumpAmountMs() {
        int base = static_cast<int>(Mod::get()->getSettingValue<int64_t>("menuLoopSeekAmountMs"));
        if (base < 100) base = 100;
        if (base > 30000) base = 30000;
#ifdef GEODE_IS_DESKTOP
        auto* kd = cocos2d::CCKeyboardDispatcher::get();
        if (kd && kd->getShiftKeyPressed()) {
            bool boost = false;
#ifdef GEODE_IS_MACOS
            boost = kd->getControlKeyPressed();
#else
            boost = kd->getAltKeyPressed();
#endif
            return base * (boost ? 3 : 2);
        }
#endif
        return base;
    }

    // Jump active music channel to percentage (0..100).
    inline void setSongPercentage(int percentage) {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        const std::string& currSong = sm.getCurrentSong();
        if (fmod->getActiveMusic(0) != currSong) return;

        const int fullLength = fmod->getMusicLengthMS(0);
        if (fullLength <= 0) return;

        sm.setPauseSongPositionTracking(true);
        auto* ch = fmod->getActiveMusicChannel(0);
        if (ch) {
            int target = static_cast<int>(
                static_cast<float>(fullLength) * (std::clamp(percentage, 0, 100) / 100.f));
            ch->setPosition(target, FMOD_TIMEUNIT_MS);
            sm.setLastMenuLoopPosition(target);
        }
        sm.setPauseSongPositionTracking(false);
    }

    // Skip backward by the configured amount. Wraps or clamps
    // depending on constant shuffle mode.
    inline void skipBackward() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        const std::string& currSong = sm.getCurrentSong();
        if (fmod->getActiveMusic(0) != currSong) return;

        const int fullLength = fmod->getMusicLengthMS(0);
        const int lastPos = sm.getLastMenuLoopPosition();
        const int jump = getJumpAmountMs();

        sm.setPauseSongPositionTracking(true);
        int newPos = 0;
        auto* ch = fmod->getActiveMusicChannel(0);
        if (!ch) { sm.setPauseSongPositionTracking(false); return; }

        if ((lastPos - jump) < 0) {
            if (sm.getConstantShuffleMode() || fullLength <= 0) {
                newPos = 0;
                ch->setPosition(newPos, FMOD_TIMEUNIT_MS);
            } else {
                // Wrap modulo fullLength to go back from the end.
                newPos = (((lastPos - jump) % fullLength) + fullLength) % fullLength;
                ch->setPosition(newPos, FMOD_TIMEUNIT_MS);
            }
        } else {
            newPos = lastPos - jump;
            ch->setPosition(newPos, FMOD_TIMEUNIT_MS);
        }
        sm.setLastMenuLoopPosition(newPos);
        sm.setPauseSongPositionTracking(false);
    }

    // Skip forward by the configured amount. Shuffles if past end
    // in constant shuffle mode, wraps otherwise.
    inline void skipForward() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        const std::string& currSong = sm.getCurrentSong();
        if (fmod->getActiveMusic(0) != currSong) return;

        const int fullLength = fmod->getMusicLengthMS(0);
        const int lastPos = sm.getLastMenuLoopPosition();
        const int jump = getJumpAmountMs();

        sm.setPauseSongPositionTracking(true);
        auto* ch = fmod->getActiveMusicChannel(0);
        if (!ch) { sm.setPauseSongPositionTracking(false); return; }

        int newPos = 0;
        if (fullLength > 0 && (lastPos + jump) > fullLength) {
            if (sm.getConstantShuffleMode()) {
                sm.setPauseSongPositionTracking(false);
                shuffleSong();
                return;
            }
            newPos = (lastPos + jump) % fullLength;
            ch->setPosition(newPos, FMOD_TIMEUNIT_MS);
        } else {
            newPos = lastPos + jump;
            ch->setPosition(newPos, FMOD_TIMEUNIT_MS);
        }
        sm.setLastMenuLoopPosition(newPos);
        sm.setPauseSongPositionTracking(false);
    }

    // Add current song to the configured playlist file
    inline void addCurrentSongToPlaylistFile() {
        if (isVanillaMenuLoopDisabled()) return;
        auto& sm = MenuLoopManager::get();
        if (sm.isOriginalMenuLoop()) {
            woahThereBuddy("There's nothing to add to your playlist!");
            return;
        }
        if (sm.isOverride()) {
            woahThereBuddy("You're trying to add your own <cy>override</c> to your playlist.");
            return;
        }

        auto playlistFile = Mod::get()->getSavedValue<std::string>("menuLoopPlaylistFile", "");
        std::filesystem::path plPath;
        if (playlistFile.empty()) {
            plPath = sm.getConfigDir() / "playlistOne.txt";
        } else {
            plPath = std::filesystem::path(playlistFile);
        }

        std::error_code ec;
        if (plPath.extension() != ".txt") {
            Notification::create("Playlist file must be a .txt file.", NotificationIcon::Error)->show();
            return;
        }
        auto existing = geode::utils::file::readString(plPath).unwrapOr("");
        if (existing.find(sm.getCurrentSong()) != std::string::npos) {
            Notification::create("Already in playlist.", NotificationIcon::Info)->show();
            return;
        }
        (void)geode::utils::file::writeString(
            plPath, existing + fmt::format("{}\n", sm.getCurrentSong()));
        Notification::create(
            fmt::format("Added to {}", plPath.filename().string()),
            NotificationIcon::Success)->show();
    }

} // namespace MenuLoopControl

} // namespace paimon::menuloop
