#include <Geode/Geode.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/HttpClient.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/progressbar/services/ProgressBarManager.hpp"
#include "../features/custom-slider/services/CustomSliderManager.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "RuntimeLifecycle.hpp"
#include "StartupIncompatibilityCheck.hpp"
#include "ModCompatWarnings.hpp"
#include "BanGate.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "MainLevelPrefetch.hpp"
#include "Settings.hpp"
#include "../features/paidraw/PaiDrawManager.hpp"
#include "../video/VideoPlayer.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/GLSLLoader.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/BlurDiskCache.hpp"
#include "../utils/GDRobTopCache.hpp"

#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../utils/ThreadTracker.hpp"
#include <thread>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <functional>
#include <memory>

namespace paimon { void initFramework(); }

using namespace geode::prelude;

namespace {
void applyLanguageSetting(std::string const& langStr) {
    Localization::get().setLanguage(Localization::languageFromId(langStr), false);
}

// atomic: MenuLayer::init can re-enter when the scene reloads
std::atomic<bool> g_languageListenerRegistered{false};

template <typename T>
void paimonOnSettingChanged(T const&) {
    paimon::settings::internal::g_settingsVersion.fetch_add(1, std::memory_order_relaxed);
}
}

namespace paimon {

void bootstrap() {
    log::info("[PaimonThumbnails][Init] Loaded event start");

    // Hard ban gate: if the local .paimon cache marks the user as banned, don't
    // initialize anything. Otherwise (no cache) this schedules a server check.
    if (paimon::ban::runStartupBanGate()) {
        log::warn("[PaimonThumbnails][Init] Aborting init: user is banned");
        return;
    }

    paimon::video::VideoPlayer::bindMainThreadId();

    PaimonCheckStartupIncompatibilities();
    PaimonLogModCompatWarnings();

    paimon::initFramework();

    ThumbnailLoader::get().applyConcurrentDownloadsSetting();

    paidraw::PaiDrawManager::get().init();

    paimon::beat_shaders::BeatShaderManager::get().init();

    paimon::blur::BlurDiskCache::get().init();
    paimon::gd::GDRobTopCache::get().init();

    bool const clearCacheAtStartup = paimon::settings::general::clearCacheOnExit();

    paimon::ThreadTracker::get().spawn([clearCacheAtStartup]() {
        geode::utils::thread::setName("PaimonMigrations");
        if (paimon::isRuntimeShuttingDown()) return;

        if (clearCacheAtStartup) {
            cleanupDiskCache("startup-safety");
            auto saveDir = Mod::get()->getSaveDir();
            std::error_code ec;
            std::filesystem::remove(saveDir / "manifest_cache.json", ec);
            // setSavedValue touches Geode structures, so do it on the main thread.
            geode::queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
            });
        }

        if (paimon::isRuntimeShuttingDown()) return;
        LevelColors::get().preloadIndexFromDisk();
        LayerBackgroundManager::get().migrateFromLegacy();
        LayerBackgroundManager::get().migrateToGlobalMusic();
        LayerBackgroundManager::get().migrateExternalAssetsToManagedStorage();
    });

    paimon::ThreadTracker::get().spawn([]() {
        geode::utils::thread::setName("PaimonConfigLoad");
        if (paimon::isRuntimeShuttingDown()) return;
        TransitionManager::get().loadConfig();
        ProgressBarManager::get().loadConfig();
        paimon::slider::CustomSliderManager::get().loadConfig();
    });

    log::info("[PaimonThumbnails] Queueing main level thumbnails...");

    std::vector<int> mainLevels;
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    if (paimon::tryClaimMainLevelsPrefetch()) {
        paimon::preload::fetchMainLevelManifestWithCache(mainLevels, "Bootstrap");

        paimon::scheduleMainThreadDelay(0.25f, []() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto& loader = ThumbnailLoader::get();
            paimon::preload::staggerMainLevelThumbnailLoads([&loader](int levelID) {
                loader.requestLoad(
                    levelID, fmt::format("{}.png", levelID), nullptr,
                    ThumbnailLoader::PriorityBootstrap);
            });
            log::info("[PaimonThumbnails] (Bootstrap) main level thumbnails stagger-enqueued");
        });
    } else {
        log::info("[PaimonThumbnails] Main level prefetch already kicked off by LoadingLayer");
    }

    std::string langStr = paimon::settings::general::language();
    log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
    applyLanguageSetting(langStr);
    bool expected = false;
    if (g_languageListenerRegistered.compare_exchange_strong(expected, true)) {
        geode::listenForSettingChanges<std::string>("language", +[](std::string value) {
            applyLanguageSetting(value);
            log::info("[PaimonThumbnails][Language] Changed to '{}'", value);
        });

        // Re-entry guard: listenForSettingChanges can fire from any thread in Geode.
        static std::atomic<bool> s_cursorSyncGuard{false};
        geode::listenForSettingChanges<bool>("custom-cursor-enable", +[](bool value) {
            if (s_cursorSyncGuard.exchange(true, std::memory_order_acq_rel)) return;
            CursorManager::get().config().enabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard.store(false, std::memory_order_release);
        });

        geode::listenForSettingChanges<bool>("levelcell-hover-effects", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<bool>("compact-list-mode", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<double>("level-thumb-width", &paimonOnSettingChanged<double>);
        geode::listenForSettingChanges<std::string>("levelinfo-background-style", &paimonOnSettingChanged<std::string>);
    }

    log::info("[PaimonThumbnails][Init] Applying startup init");

    log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
    paimon::scheduleMainThreadDelay(3.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::ThreadTracker::get().spawn([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            if (paimon::isRuntimeShuttingDown()) return;
            LevelColors::get().extractColorsFromCache();
            if (paimon::isRuntimeShuttingDown()) return;
            geode::queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                log::info("[PaimonThumbnails][Init] Color extraction finished");
            });
        });
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");

    paimon::scheduleMainThreadDelay(12.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;

        paimon::emotes::EmoteService::get().loadCatalogFromDisk();
        
        auto& svc = paimon::emotes::EmoteService::get();
        log::info("[PaimonEmotes] Catalog loaded: {} emotes ({} GIFs, {} stickers)",
                  svc.getAllEmotes().size(), svc.getGifEmotes().size(), svc.getStaticEmotes().size());

        paimon::emotes::EmoteService::get().fetchAllEmotes([](bool success) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info("[PaimonEmotes] Catalog fetch {}", success ? "succeeded" : "failed (using cached)");
            
            log::info("[PaimonEmotes] Emote disk preload skipped at startup; assets load on demand");
        });
    });

    paimon::scheduleMainThreadDelay(10.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        Shaders::prewarmLevelInfoShaders();
        Shaders::prewarmConfiguredBackgroundShaders();
        paimon::shaders::preloadBlurShaders();
    });

    paimon::scheduleMainThreadDelay(8.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::updates::UpdateChecker::get().checkAsync();
    });
}

} // namespace paimon

$on_game(Loaded) {
    paimon::bootstrap();
}
