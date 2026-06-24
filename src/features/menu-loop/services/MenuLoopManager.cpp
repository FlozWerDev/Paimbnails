#include "MenuLoopManager.hpp"
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>
#include <ranges>

using namespace geode::prelude;
using namespace paimon::menuloop;

// Helpers

static bool isSupportedFile(const std::string_view path) {
    if (path.empty()) return false;
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(std::filesystem::path(path).extension()));
    // Formats FMOD in GD can play directly.
    // AAC (m4a) and Opus/WebM are excluded: GD's FMOD Core doesn't support AAC outside iOS
    // and doesn't support Opus in the normal path. YtDlpDownloader uses ffmpeg to re-encode
    // to mp3 before returning the path, so we always receive a valid .mp3.
    static const std::array<std::string, 5> ok = {
        ".mp3", ".wav", ".ogg", ".oga", ".flac"
    };
    return std::ranges::find(ok, ext) != ok.end();
}

static std::filesystem::path toProblematicString(const std::string& s) {
    return std::filesystem::path(s);
}

static std::string toNormalizedString(const std::filesystem::path& p) {
    return geode::utils::string::pathToString(p);
}

static int randomIndex(int size) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, size - 1);
    return dist(gen);
}

// Song list

void MenuLoopManager::addSong(const std::string& path) {
    if (std::ranges::find(m_songs, path) == m_songs.end()) {
        m_songs.push_back(path);
    }
}

void MenuLoopManager::removeSong(const std::string& path) {
    m_songs.erase(std::remove(m_songs.begin(), m_songs.end(), path), m_songs.end());
}

void MenuLoopManager::clearSongs() {
    m_songs.clear();
}

// Current song

void MenuLoopManager::pickRandomSong() {
    if (m_isOverride) {
        m_isMenuLoop = false;
        m_currentSong = m_overrideSong;
    } else if (!m_songs.empty()) {
        m_isMenuLoop = false;
        if (m_songs.size() != 1) {
            int randomIdx = randomIndex(static_cast<int>(m_songs.size()));
            if (getAdvancedLogs()) log::info("entering a while loop maybe");
            std::error_code existsEc;
            if (!std::filesystem::exists(toProblematicString(m_songs[randomIdx]), existsEc) || existsEc) {
                m_isMenuLoop = true;
                m_currentSong = "menuLoop.mp3";
                Notification::create("Unable to find song at index! Check logs.", NotificationIcon::Error, 10.f)->show();
                return;
            }
            // Avoid same song unless it's a favorite
            // Guard against pathological cases (e.g. only 2 songs where the
            // other is a favorite) that would otherwise loop forever.
            int avoidAttempts = 50;
            while (m_songs[randomIdx] == m_currentSong &&
                   std::ranges::find(m_favorites, m_songs[randomIdx]) == m_favorites.end() &&
                   --avoidAttempts > 0) {
                if (getAdvancedLogs()) log::info("avoiding shuffling into the same song");
                randomIdx = randomIndex(static_cast<int>(m_songs.size()));
            }
            m_currentSong = m_songs[randomIdx];
            if (getAdvancedLogs()) log::info("new song: {}", m_currentSong);
        } else {
            m_currentSong = m_songs[0];
        }
    } else {
        m_isMenuLoop = true;
        m_currentSong = "menuLoop.mp3";
    }
    m_hashedCurrentSong = std::hash<std::string>{}(m_currentSong);
}

std::string MenuLoopManager::getCurrentSong() const {
    if (!getOverrideSong().empty()) return getOverrideSong();
    return m_currentSong;
}

void MenuLoopManager::setCurrentSong(const std::string& song) {
    if (!getOverrideSong().empty()) m_currentSong = getOverrideSong();
    else m_currentSong = song;
    if (m_currentSong != "menuLoop.mp3" && !m_currentSong.empty()) {
        m_isMenuLoop = false;
    }
    m_hashedCurrentSong = std::hash<std::string>{}(m_currentSong);
}

void MenuLoopManager::setCurrentSongToSavedSong() {
    if (m_isMenuLoop || !getOverrideSong().empty()) return;
    const auto lastMenuLoop = Mod::get()->getSavedValue<std::string>("lastMenuLoop");
    const auto lastMenuLoopPath = Mod::get()->getSavedValue<std::filesystem::path>("lastMenuLoopPath");
    std::error_code existsEc1, existsEc2;
    if (std::filesystem::exists(toProblematicString(lastMenuLoop), existsEc1) && !existsEc1) {
        m_currentSong = lastMenuLoop;
    } else if (std::filesystem::exists(lastMenuLoopPath, existsEc2) && !existsEc2) {
        m_currentSong = toNormalizedString(lastMenuLoopPath);
    }
    m_hashedCurrentSong = std::hash<std::string>{}(m_currentSong);
}

// Override

void MenuLoopManager::setOverride(const std::string& path) {
    if (!isSupportedFile(path) && !path.empty()) {
        if (getAdvancedLogs()) log::info("invalid file offered for override song: {}", path);
        return;
    }
    if (isSupportedFile(path)) {
        m_overrideSong = path;
        m_isOverride = true;
        m_isMenuLoop = false;
        if (getAdvancedLogs()) log::info("set override to true: {}", path);
        return;
    }
    m_overrideSong = "";
    m_isOverride = false;
    if (getAdvancedLogs()) log::info("set override to false");
}

std::string MenuLoopManager::getOverrideSong() const {
    if (!isSupportedFile(m_overrideSong)) return "";
    return m_overrideSong;
}

void MenuLoopManager::setCurrentSongToOverride() {
    if (getAdvancedLogs()) log::info("setting current song to override");
    const std::string& override = getOverrideSong();
    if (override.empty() || !isSupportedFile(override)) {
        if (getAdvancedLogs()) log::info("override is not valid");
        return;
    }
    m_currentSong = override;
    m_hashedCurrentSong = std::hash<std::string>{}(m_currentSong);
}

// Blacklist

void MenuLoopManager::addToBlacklist(const std::string& song) {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_favorites, song) != m_favorites.end()) {
        if (getAdvancedLogs()) log::info("tried to blacklist a favorited song: {}", song);
        return;
    }
    m_blacklist.push_back(song);
}

void MenuLoopManager::addToBlacklist() {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_favorites, m_currentSong) != m_favorites.end()) {
        if (getAdvancedLogs()) log::info("tried to blacklist a favorited song: {}", m_currentSong);
        return;
    }
    m_blacklist.push_back(m_currentSong);
}

// Favorites

void MenuLoopManager::addToFavorites(const std::string& song) {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_blacklist, song) != m_blacklist.end()) {
        if (getAdvancedLogs()) log::info("tried to favorite a blacklisted song: {}", song);
        return;
    }
    m_favorites.push_back(song);
}

void MenuLoopManager::addToFavorites() {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_blacklist, m_currentSong) != m_blacklist.end()) {
        if (getAdvancedLogs()) log::info("tried to favorite a blacklisted song: {}", m_currentSong);
        return;
    }
    m_favorites.push_back(m_currentSong);
}

// Held / Previous

void MenuLoopManager::setHeldSong(const std::string& value) {
    if (!getOverrideSong().empty()) return;
    m_heldSong = value;
}

void MenuLoopManager::setPreviousSong(const std::string& value) {
    if (!isSupportedFile(value)) {
        if (getAdvancedLogs()) log::info("previous song is not valid");
        return;
    }
    m_previousSong = value;
}

void MenuLoopManager::resetPreviousSong() {
    m_previousSong = "";
}

// Save / Load

void MenuLoopManager::saveLastMenuLoop() {
    if (m_isMenuLoop || !getOverrideSong().empty()) return;
    Mod::get()->setSavedValue("lastMenuLoop", m_currentSong);
    Mod::get()->setSavedValue("lastMenuLoopPath", std::filesystem::path(m_currentSong));
}

// Position restore

void MenuLoopManager::restoreLastMenuLoopPosition() {
    auto* colon = getColonMenuLoopStartTime();
    if ((colon && colon->getSettingValue<bool>("enable")) || !getShouldRestoreMenuLoopPoint()) {
        setPauseSongPositionTracking(false);
        return;
    }
    FMODAudioEngine::get()->setMusicTimeMS(getLastMenuLoopPosition(), false, 0);
    setShouldRestoreMenuLoopPoint(false);
    setPauseSongPositionTracking(false);
}
