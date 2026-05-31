#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <random>

using namespace geode::prelude;

namespace paimon::menuloop {

// ── Song Data ───────────────────────────────────────────────────

enum class SongType {
    Normal,
    Favorited,
    Blacklisted,
};

struct SongData {
    std::string path;
    std::string displayName;
    SongType type = SongType::Normal;
    int lengthMs = 0;
};

// ── MenuLoopManager ─────────────────────────────────────────────

class MenuLoopManager {
public:
    static MenuLoopManager& get() {
        static MenuLoopManager instance;
        return instance;
    }

    // ── Song list ──
    std::vector<std::string>& getSongs() { return m_songs; }
    void addSong(const std::string& path);
    void removeSong(const std::string& path);
    void clearSongs();
    int getSongsSize() const { return static_cast<int>(m_songs.size()); }
    bool songSizeIsBad() const { return m_songs.empty() || m_songs.size() < 2; }

    // ── Current song ──
    void pickRandomSong();
    std::string getCurrentSong() const;
    void setCurrentSong(const std::string& song);
    void setCurrentSongToSavedSong();
    void setCurrentSongToOverride();
    bool isOriginalMenuLoop() const { return m_isMenuLoop; }

    // ── Override ──
    void setOverride(const std::string& path);
    std::string getOverrideSong() const;
    bool isOverride() const { return m_isOverride; }

    // ── Blacklist / Favorites ──
    void addToBlacklist(const std::string& song);
    void addToBlacklist();
    std::vector<std::string>& getBlacklist() { return m_blacklist; }
    void addToFavorites(const std::string& song);
    void addToFavorites();
    std::vector<std::string>& getFavorites() { return m_favorites; }

    // ── Held / Previous ──
    void setHeldSong(const std::string& value);
    void resetHeldSong();
    std::string getHeldSong() const { return m_heldSong; }
    void setPreviousSong(const std::string& value);
    void resetPreviousSong();
    std::string getPreviousSong() const { return m_previousSong; }
    bool isPreviousSong() const { return m_currentSong == m_previousSong; }

    // ── Display name ──
    void setCurrentSongDisplayName(const std::string& name) { m_displayName = name; }
    std::string getCurrentSongDisplayName() const { return m_displayName; }

    // ── Shuffle mode ──
    void setConstantShuffleMode(bool value) { m_constantShuffleMode = value; }
    bool getConstantShuffleMode() const { return m_constantShuffleMode; }

    // ── Song data map ──
    std::unordered_map<std::string, SongData>& getSongToSongDataEntries() { return m_songToSongDataMap; }

    // ── State flags ──
    void setCalledOnce(bool value) { m_calledOnce = value; }
    bool getCalledOnce() const { return m_calledOnce; }
    void setFinishedCalculatingSongLengths(bool value) { m_finishedCalculatingSongLengths = value; }
    bool getFinishedCalculatingSongLengths() const { return m_finishedCalculatingSongLengths; }
    void setShouldRestoreMenuLoopPoint(bool value) { m_shouldRestoreMenuLoopPoint = value; }
    bool getShouldRestoreMenuLoopPoint() const { return m_shouldRestoreMenuLoopPoint; }
    void setLastMenuLoopPosition(int ms) { m_lastPosition = ms; }
    int getLastMenuLoopPosition() const { return m_lastPosition; }
    void restoreLastMenuLoopPosition();
    void setPauseSongPositionTracking(bool value) { m_pausedSongPositionTracking = value; }
    bool getPauseSongPositionTracking() const { return m_pausedSongPositionTracking; }

    // ── Mod compat ──
    void setGeodify(bool value) { m_geodify = value; }
    bool getGeodify() const { return m_geodify; }
    void setSawbladeCustomSongsFolder(bool value) { m_sawbladeCustomSongsFolder = value; }
    bool getSawbladeCustomSongsFolder() const { return m_sawbladeCustomSongsFolder; }
    void setAdvancedLogs(bool value) { m_advancedLogs = value; }
    bool getAdvancedLogs() const { return m_advancedLogs; }
    void setColonMenuLoopStartTime(Mod* value) { m_colonMenuLoopStartTime = value; }
    Mod* getColonMenuLoopStartTime() const { return m_colonMenuLoopStartTime; }
    void setVibecodedVentilla(bool value) { m_vibecodedVentilla = value; }
    bool getVibecodedVentilla() const { return m_vibecodedVentilla; }

    // ── Config dir helpers ──
    std::filesystem::path getConfigDir() const { return Mod::get()->getConfigDir(); }
    void saveLastMenuLoop();

    // ── Playlist file mode ──
    void setPlaylistIsEmpty(bool value) { m_playlistIsEmpty = value; }
    bool getPlaylistIsEmpty() const { return m_playlistIsEmpty; }
    void setPlaylistName(const std::string& name) { m_playlistName = name; }
    std::string getPlaylistName() const { return m_playlistName; }

    // ── Utility ──
    unsigned long getHashedCurrentSong() const { return m_hashedCurrentSong; }
    void incrementTowerRepeatCount() { m_towerRepeatCount++; }
    void resetTowerRepeatCount() { m_towerRepeatCount = 0; }
    int getTowerRepeatCount() const { return m_towerRepeatCount; }

private:
    MenuLoopManager() = default;
    ~MenuLoopManager() = default;
    MenuLoopManager(const MenuLoopManager&) = delete;
    MenuLoopManager& operator=(const MenuLoopManager&) = delete;

    std::vector<std::string> m_songs;
    std::string m_currentSong;
    std::string m_overrideSong;
    std::string m_heldSong;
    std::string m_previousSong;
    std::string m_displayName;
    std::string m_playlistName;
    std::vector<std::string> m_blacklist;
    std::vector<std::string> m_favorites;
    std::unordered_map<std::string, SongData> m_songToSongDataMap;

    bool m_isMenuLoop = true;
    bool m_isOverride = false;
    bool m_constantShuffleMode = false;
    bool m_calledOnce = false;
    bool m_finishedCalculatingSongLengths = false;
    bool m_shouldRestoreMenuLoopPoint = false;
    bool m_pausedSongPositionTracking = false;
    bool m_geodify = false;
    bool m_sawbladeCustomSongsFolder = false;
    bool m_advancedLogs = false;
    bool m_vibecodedVentilla = false;
    bool m_playlistIsEmpty = true;

    int m_lastPosition = 0;
    int m_towerRepeatCount = 0;
    unsigned long m_hashedCurrentSong = 0;

    Mod* m_colonMenuLoopStartTime = nullptr;
};

} // namespace paimon::menuloop
