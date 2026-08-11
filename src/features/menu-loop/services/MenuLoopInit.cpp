#include "MenuLoopManager.hpp"
#include "MenuLoopControl.hpp"
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace paimon::menuloop;

namespace {

static bool isAudioFile(const std::filesystem::path& p) {
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(p.extension()));
    static const std::array<std::string, 7> ok = {
        ".mp3", ".ogg", ".wav", ".flac", ".oga", ".m4a", ".opus"
    };
    return std::ranges::find(ok, ext) != ok.end();
}

static void ensureFileExists(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        (void)geode::utils::file::writeString(path, content);
    }
}

static void scanAndLoadSongs() {
    auto& sm = MenuLoopManager::get();
    auto configDir = sm.getConfigDir();

    sm.clearSongs();
    sm.getBlacklist().clear();
    sm.getFavorites().clear();
    const bool loadPlaylist = Mod::get()->getSavedValue<bool>(
        "menuLoopLoadPlaylistFile", false);

    std::error_code ec;
    if (!loadPlaylist) {
        for (auto const& entry : std::filesystem::recursive_directory_iterator(
                 configDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (isAudioFile(path)) {
                sm.addSong(geode::utils::string::pathToString(path));
            }
        }

        auto extraFolder = std::filesystem::path(
            Mod::get()->getSavedValue<std::string>("menuLoopAdditionalFolder", ""));
        if (!extraFolder.empty() && std::filesystem::exists(extraFolder, ec) && !ec) {
            for (auto const& entry : std::filesystem::recursive_directory_iterator(
                     extraFolder, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!entry.is_regular_file()) continue;
                auto path = entry.path();
                if (isAudioFile(path)) {
                    sm.addSong(geode::utils::string::pathToString(path));
                }
            }
        }
    }

    if (loadPlaylist) {
        auto playlistPath = std::filesystem::path(Mod::get()->getSavedValue<std::string>("menuLoopPlaylistFile", ""));
    if (playlistPath.empty()) playlistPath = configDir / "playlistOne.txt";
        if (std::filesystem::exists(playlistPath, ec) && !ec) {
            auto content = geode::utils::file::readString(playlistPath);
            if (content.isOk()) {
                std::istringstream stream(content.unwrap());
                std::string line;
                while (std::getline(stream, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    auto trimmed = line;
                    auto start = trimmed.find_first_not_of(" \t\r\n");
                    if (start == std::string::npos) continue;
                    auto end = trimmed.find_last_not_of(" \t\r\n");
                    trimmed = trimmed.substr(start, end - start + 1);
                    if (!trimmed.empty() && std::filesystem::exists(trimmed, ec)) {
                        sm.addSong(trimmed);
                    }
                }
            }
        }
    }

    auto blPath = configDir / "blacklist.txt";
    if (std::filesystem::exists(blPath, ec) && !ec) {
        auto content = geode::utils::file::readString(blPath);
        if (content.isOk()) {
            std::istringstream stream(content.unwrap());
            std::string line;
            while (std::getline(stream, line)) {
                if (line.empty() || line[0] == '#') continue;
                sm.addToBlacklist(line);
            }
        }
    }

    auto favPath = configDir / "favorites.txt";
    if (std::filesystem::exists(favPath, ec) && !ec) {
        auto content = geode::utils::file::readString(favPath);
        if (content.isOk()) {
            std::istringstream stream(content.unwrap());
            std::string line;
            while (std::getline(stream, line)) {
                if (line.empty() || line[0] == '#') continue;
                sm.addToFavorites(line);
            }
        }
    }

    for (const auto& bl : sm.getBlacklist()) {
        sm.removeSong(bl);
    }

    auto& songData = sm.getSongToSongDataEntries();
    for (const auto& song : sm.getSongs()) {
        auto path = std::filesystem::path(song);
        SongData data;
        data.path = song;
        data.displayName = geode::utils::string::pathToString(path.stem());
        data.type = std::ranges::find(sm.getFavorites(), song) != sm.getFavorites().end()
            ? SongType::Favorited
            : SongType::Normal;
        songData.emplace(song, std::move(data));
    }

    if (Mod::get()->getSettingValue<bool>("menuLoopSaveSongOnGameClose")) {
        sm.setCurrentSongToSavedSong();
    } else {
        sm.pickRandomSong();
    }

    sm.setFinishedCalculatingSongLengths(true);

    log::info("[MenuLoop] Loaded {} songs, blacklist: {}, favorites: {}",
        sm.getSongsSize(), sm.getBlacklist().size(), sm.getFavorites().size());
}

} // namespace

$on_mod(Loaded) {
    auto& sm = MenuLoopManager::get();
    auto configDir = sm.getConfigDir();

    ensureFileExists(configDir / "playlistOne.txt", "# Menu Loop Playlist 1\n");
    ensureFileExists(configDir / "playlistTwo.txt", "# Menu Loop Playlist 2\n");
    ensureFileExists(configDir / "playlistThree.txt", "# Menu Loop Playlist 3\n");
    ensureFileExists(configDir / "blacklist.txt",
        "# Menu Loop Blacklist\n"
        "# Add song paths (one per line) to blacklist them\n"
    );
    ensureFileExists(configDir / "favorites.txt",
        "# Menu Loop Favorites\n"
        "# Add song paths (one per line) to favorite them\n"
    );

    // Initialize state from settings
    sm.setConstantShuffleMode(Mod::get()->getSettingValue<bool>("menuLoopConstantShuffle"));
    sm.setLastMenuLoopPosition(0);
    sm.setShouldRestoreMenuLoopPoint(true);
    sm.setFinishedCalculatingSongLengths(false);
    sm.setAdvancedLogs(Mod::get()->getSavedValue<bool>("menuLoopAdvancedLogs", false));
    sm.setPlaylistIsEmpty(true);
    sm.setCalledOnce(false);

    auto* loader = Loader::get();
    sm.setVibecodedVentilla(loader->isModLoaded("joseii.ventilla"));

    scanAndLoadSongs();

    listenForSettingChanges<bool>("menuLoopConstantShuffle", [](bool enabled) {
        MenuLoopManager::get().setConstantShuffleMode(enabled);
    });
}
