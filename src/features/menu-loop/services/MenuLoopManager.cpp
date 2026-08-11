#include "MenuLoopManager.hpp"
#include <Geode/utils/string.hpp>
#include <ranges>

using namespace geode::prelude;
using namespace paimon::menuloop;


static bool isSupportedFile(const std::string_view path) {
    if (path.empty()) return false;
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(std::filesystem::path(path).extension()));
    static const std::array<std::string, 7> ok = {
        ".mp3", ".wav", ".ogg", ".oga", ".flac", ".m4a", ".opus"
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
    m_songToSongDataMap.clear();
}


void MenuLoopManager::pickRandomSong() {
    if (m_isOverride) {
        m_isMenuLoop = false;
        m_currentSong = m_overrideSong;
    } else if (!m_songs.empty()) {
        m_isMenuLoop = false;
        std::vector<std::string> candidates;
        for (const auto& song : m_songs) {
            std::error_code existsEc;
            if (!std::filesystem::is_regular_file(toProblematicString(song), existsEc) || existsEc) {
                continue;
            }
            if (std::ranges::find(m_blacklist, song) != m_blacklist.end()) continue;
            candidates.push_back(song);
            if (std::ranges::find(m_favorites, song) != m_favorites.end()) {
                candidates.push_back(song);
            }
        }

        const bool hasAlternative = std::ranges::any_of(candidates, [&](const std::string& song) {
            return song != m_currentSong;
        });
        if (hasAlternative) std::erase(candidates, m_currentSong);

        if (candidates.empty()) {
            m_isMenuLoop = true;
            m_currentSong = "menuLoop.mp3";
        } else {
            m_currentSong = candidates[randomIndex(static_cast<int>(candidates.size()))];
            if (getAdvancedLogs()) log::info("[MenuLoop] new song: {}", m_currentSong);
        }
    } else {
        m_isMenuLoop = true;
        m_currentSong = "menuLoop.mp3";
    }
    updateCurrentSongMetadata();
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
    updateCurrentSongMetadata();
}

void MenuLoopManager::setCurrentSongToSavedSong() {
    if (!getOverrideSong().empty()) return;
    const auto lastMenuLoop = Mod::get()->getSavedValue<std::string>("lastMenuLoop");
    const auto lastMenuLoopPath = Mod::get()->getSavedValue<std::filesystem::path>("lastMenuLoopPath");
    std::error_code existsEc1, existsEc2;
    if (std::ranges::find(m_songs, lastMenuLoop) != m_songs.end()
        && std::filesystem::exists(toProblematicString(lastMenuLoop), existsEc1) && !existsEc1) {
        m_currentSong = lastMenuLoop;
    } else if (const auto normalized = toNormalizedString(lastMenuLoopPath);
        std::ranges::find(m_songs, normalized) != m_songs.end()
        && std::filesystem::exists(lastMenuLoopPath, existsEc2) && !existsEc2) {
        m_currentSong = toNormalizedString(lastMenuLoopPath);
    }
    updateCurrentSongMetadata();
}


void MenuLoopManager::setOverride(const std::string& path) {
    if (!isSupportedFile(path) && !path.empty()) {
        if (getAdvancedLogs()) log::info("invalid file offered for override song: {}", path);
        return;
    }
    if (isSupportedFile(path)) {
        m_overrideSong = path;
        m_isOverride = true;
        m_isMenuLoop = false;
        updateCurrentSongMetadata();
        if (getAdvancedLogs()) log::info("set override to true: {}", path);
        return;
    }
    m_overrideSong = "";
    m_isOverride = false;
    updateCurrentSongMetadata();
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
    updateCurrentSongMetadata();
}


void MenuLoopManager::addToBlacklist(const std::string& song) {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_favorites, song) != m_favorites.end()) {
        if (getAdvancedLogs()) log::info("tried to blacklist a favorited song: {}", song);
        return;
    }
    if (std::ranges::find(m_blacklist, song) == m_blacklist.end()) {
        m_blacklist.push_back(song);
    }
    if (auto it = m_songToSongDataMap.find(song); it != m_songToSongDataMap.end()) {
        it->second.type = SongType::Blacklisted;
    }
}

void MenuLoopManager::addToBlacklist() {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_favorites, m_currentSong) != m_favorites.end()) {
        if (getAdvancedLogs()) log::info("tried to blacklist a favorited song: {}", m_currentSong);
        return;
    }
    addToBlacklist(m_currentSong);
}


void MenuLoopManager::addToFavorites(const std::string& song) {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_blacklist, song) != m_blacklist.end()) {
        if (getAdvancedLogs()) log::info("tried to favorite a blacklisted song: {}", song);
        return;
    }
    if (std::ranges::find(m_favorites, song) == m_favorites.end()) {
        m_favorites.push_back(song);
    }
    if (auto it = m_songToSongDataMap.find(song); it != m_songToSongDataMap.end()) {
        it->second.type = SongType::Favorited;
    }
}

void MenuLoopManager::addToFavorites() {
    if (!getOverrideSong().empty()) return;
    if (std::ranges::find(m_blacklist, m_currentSong) != m_blacklist.end()) {
        if (getAdvancedLogs()) log::info("tried to favorite a blacklisted song: {}", m_currentSong);
        return;
    }
    addToFavorites(m_currentSong);
}


void MenuLoopManager::setHeldSong(const std::string& value) {
    if (!getOverrideSong().empty()) return;
    m_heldSong = value;
}

void MenuLoopManager::resetHeldSong() {
    m_heldSong.clear();
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


void MenuLoopManager::saveLastMenuLoop() {
    if (m_isMenuLoop || !getOverrideSong().empty()) return;
    Mod::get()->setSavedValue("lastMenuLoop", m_currentSong);
    Mod::get()->setSavedValue("lastMenuLoopPath", std::filesystem::path(m_currentSong));
}


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

void MenuLoopManager::updateCurrentSongMetadata() {
    m_isMenuLoop = m_currentSong.empty() || m_currentSong == "menuLoop.mp3";
    m_hashedCurrentSong = std::hash<std::string>{}(m_currentSong);

    if (m_isMenuLoop) {
        m_displayName = "Original Menu Loop";
        return;
    }
    if (auto it = m_songToSongDataMap.find(m_currentSong); it != m_songToSongDataMap.end()
        && !it->second.displayName.empty()) {
        m_displayName = it->second.displayName;
        return;
    }
    m_displayName = geode::utils::string::pathToString(
        std::filesystem::path(m_currentSong).stem());
}
