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
    static const std::array<std::string, 5> ok = {".mp3", ".ogg", ".wav", ".flac", ".oga"};
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

    // ── Scan config dir for audio files ──
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(configDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        if (isAudioFile(path)) {
            sm.addSong(geode::utils::string::pathToString(path));
        }
    }

    // ── Scan additional folder if set ──
    auto extraFolder = Mod::get()->getSettingValue<std::filesystem::path>("menuLoopAdditionalFolder");
    if (!extraFolder.empty() && std::filesystem::exists(extraFolder, ec) && !ec) {
        for (auto const& entry : std::filesystem::directory_iterator(extraFolder, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (isAudioFile(path)) {
                sm.addSong(geode::utils::string::pathToString(path));
            }
        }
    }

    // ── Load playlist file if enabled ──
    if (Mod::get()->getSettingValue<bool>("menuLoopLoadPlaylistFile")) {
        auto playlistPath = Mod::get()->getSettingValue<std::filesystem::path>("menuLoopPlaylistFile");
    if (playlistPath.empty()) playlistPath = configDir / "playlistOne.txt";
        if (std::filesystem::exists(playlistPath, ec) && !ec) {
            auto content = geode::utils::file::readString(playlistPath);
            if (content.isOk()) {
                std::istringstream stream(content.unwrap());
                std::string line;
                while (std::getline(stream, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    auto trimmed = line;
                    // trim whitespace
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

    // ── Load blacklist ──
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

    // ── Load favorites ──
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

    // ── Apply blacklist ──
    for (const auto& bl : sm.getBlacklist()) {
        sm.removeSong(bl);
    }

    // ── Pick initial song ──
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

    // Ensure config files exist
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
    sm.setAdvancedLogs(Mod::get()->getSettingValue<bool>("menuLoopAdvancedLogs"));
    sm.setPlaylistIsEmpty(true);
    sm.setCalledOnce(false);

    auto* loader = Loader::get();
    sm.setVibecodedVentilla(loader->isModLoaded("joseii.ventilla"));

    // Scan and load songs
    scanAndLoadSongs();
}
