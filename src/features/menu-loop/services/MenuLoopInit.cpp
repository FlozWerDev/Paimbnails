#include "MenuLoopManager.hpp"
#include "MenuLoopControl.hpp"
#include "MenuLoopScan.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>

using namespace geode::prelude;
using namespace paimon::menuloop;

namespace {

void ensureFileExists(std::filesystem::path const& path, std::string const& content) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) && !ec)
        (void)geode::utils::file::writeString(path, content);
}

void startSongScan() {
    auto* mod = Mod::get();
    MenuLoopScanInput input;
    input.configDir = mod->getConfigDir();
    input.extraFolder = menuLoopUtf8Path(mod->getSavedValue<std::string>("menuLoopAdditionalFolder", ""));
    input.playlistFile = menuLoopUtf8Path(mod->getSavedValue<std::string>("menuLoopPlaylistFile", ""));
    input.usePlaylist = mod->getSavedValue<bool>("menuLoopLoadPlaylistFile", false);
    input.restoreSaved = mod->getSettingValue<bool>("menuLoopSaveSongOnGameClose");
    input.savedSong = mod->getSavedValue<std::string>("lastMenuLoop", "");
    input.savedPath = menuLoopPathString(mod->getSavedValue<std::filesystem::path>("lastMenuLoopPath"));

    auto& manager = MenuLoopManager::get();
    // Opening Menu Music before/during this task must never lose live edits.
    if (!manager.getSongs().empty()) {
        manager.setFinishedCalculatingSongLengths(true);
        return;
    }
    auto initialSong = manager.getCurrentSong();
    auto initialBlacklist = manager.getBlacklist();
    auto initialFavorites = manager.getFavorites();
    bool started = paimon::ThreadTracker::get().spawn(
        [input = std::move(input), initialSong, initialBlacklist, initialFavorites]() {
        geode::utils::thread::setName("PaimonMenuLoopScan");
        auto start = std::chrono::steady_clock::now();
        MenuLoopScanResult result;
        try {
            if (paimon::isRuntimeShuttingDown()) return;
            for (auto name : {"playlistOne.txt", "playlistTwo.txt", "playlistThree.txt",
                              "blacklist.txt", "favorites.txt"}) {
                ensureFileExists(input.configDir / name, "# Menu Loop: one song path per line\n");
            }
            result = scanMenuLoopSongs(input, paimon::isRuntimeShuttingDown);
        } catch (std::exception const& e) {
            log::warn("[MenuLoop] Song scan failed: {}", e.what());
        }
        if (paimon::isRuntimeShuttingDown()) return;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        geode::queueInMainThread([result = std::move(result), initialSong,
                                 initialBlacklist, initialFavorites, elapsed]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto& sm = MenuLoopManager::get();
            sm.setFinishedCalculatingSongLengths(true);
            if (!sm.getSongs().empty() || sm.getBlacklist() != initialBlacklist
                || sm.getFavorites() != initialFavorites) return;

            sm.getSongs() = std::move(result.songs);
            sm.getBlacklist() = std::move(result.blacklist);
            sm.getFavorites() = std::move(result.favorites);
            std::unordered_set<std::string> favorites(sm.getFavorites().begin(), sm.getFavorites().end());
            auto& metadata = sm.getSongToSongDataEntries();
            metadata.reserve(sm.getSongs().size());
            for (auto const& song : sm.getSongs()) {
                metadata.emplace(song, SongData{song,
                    menuLoopPathString(menuLoopUtf8Path(song).stem()),
                    favorites.contains(song) ? SongType::Favorited : SongType::Normal});
            }
            bool selectSong = !sm.isOverride() && sm.getCurrentSong() == initialSong;
            if (selectSong) sm.setCurrentSong(result.selectedSong);
            log::info("[MenuLoop] Scanned {} songs in {} ms off the main thread",
                sm.getSongsSize(), elapsed);

            // Only switch audio if the user is still at the menu. Gameplay and
            // editor music must not be interrupted by a late disk scan.
            auto* scene = CCDirector::get()->getRunningScene();
            if (selectSong && result.selectedSong != "menuLoop.mp3"
                && scene && scene->getChildByType<MenuLayer>(0)
                && !isVanillaMenuLoopDisabled() && !sm.getGeodify() && !sm.getVibecodedVentilla()) {
                MenuLoopControl::stopMenuMusic();
                GameManager::get()->playMenuMusic();
            }
        });
    });
    if (!started) manager.setFinishedCalculatingSongLengths(true);
}

} // namespace

$on_mod(Loaded) {
    auto& sm = MenuLoopManager::get();
    // Menu Music can autoplay before the async scan completes. Its filters
    // must already be available; only these two small lists are read here.
    auto configDir = sm.getConfigDir();
    std::unordered_set<std::string> blocked, favorites;
    readMenuLoopList(configDir / "blacklist.txt", paimon::isRuntimeShuttingDown,
        [&](std::string song) {
            if (blocked.insert(song).second) sm.getBlacklist().push_back(std::move(song));
        });
    readMenuLoopList(configDir / "favorites.txt", paimon::isRuntimeShuttingDown,
        [&](std::string song) {
            if (!blocked.contains(song) && favorites.insert(song).second)
                sm.getFavorites().push_back(std::move(song));
        });
    sm.setConstantShuffleMode(Mod::get()->getSettingValue<bool>("menuLoopConstantShuffle"));
    sm.setShouldRestoreMenuLoopPoint(true);
    sm.setAdvancedLogs(Mod::get()->getSavedValue<bool>("menuLoopAdvancedLogs", false));
    sm.setVibecodedVentilla(Loader::get()->isModLoaded("joseii.ventilla"));
    listenForSettingChanges<bool>("menuLoopConstantShuffle", [](bool enabled) {
        MenuLoopManager::get().setConstantShuffleMode(enabled);
    });
}

$on_game(Loaded) {
    paimon::scheduleMainThreadDelay(0.5f, [] {
        if (!paimon::isRuntimeShuttingDown()) startSongScan();
    });
}
