// Deferred mod init, kicked off from MenuLayer::init().

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
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "MainLevelPrefetch.hpp"
#include "Settings.hpp"
#include "../features/paidraw/PaiDrawManager.hpp"
#include "../video/VideoNormalizer.hpp"
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

    paimon::video::VideoPlayer::bindMainThreadId();

    PaimonCheckStartupIncompatibilities();
    PaimonLogModCompatWarnings();

    // Framework: register features, permissions, hooks
    paimon::initFramework();

    // After Mod::get() is valid: set download threads before thumb preload.
    ThumbnailLoader::get().applyConcurrentDownloadsSetting();

    // PaiDraw
    paidraw::PaiDrawManager::get().init();

    // Lazy FFT pipeline init; attaches the FMOD DSP only when enabled.
    paimon::beat_shaders::BeatShaderManager::get().init();

    // Persistent cache of pre-computed blur textures. Async init reads the
    // index off the I/O pool, so the main thread isn't blocked; on later runs a
    // populated cache lets requestLoad lift blur textures straight from disk.
    paimon::blur::BlurDiskCache::get().init();
    paimon::gd::GDRobTopCache::get().init();
    // Popup post-processing: deferred init in CCScene::visit (needs an active GL context).

    // Safety net: if clear-cache-on-exit is on and the last session crashed
    // before $on_game(Exiting), disk caches may have leaked. Runs in the
    // background so the recursive cleanup doesn't stall startup.
    bool const clearCacheAtStartup = paimon::settings::general::clearCacheOnExit();

    // Cleanup: remove orphaned video cache files (>7 days)
    paimon::video::VideoNormalizer::cleanupOrphanedCache();

    // Run independent migrations, cleanups and config loads in parallel.
    paimon::ThreadTracker::get().spawn([clearCacheAtStartup]() {
        geode::utils::thread::setName("PaimonMigrations");
        if (paimon::isRuntimeShuttingDown()) return;

        // Deferred cache cleanup (used to run on the main thread)
        if (clearCacheAtStartup) {
            cleanupDiskCache("startup-safety");
            auto saveDir = Mod::get()->getSaveDir();
            std::error_code ec;
            std::filesystem::remove(saveDir / "manifest_cache.json", ec);
            // setSavedValue touches Geode structures, so do it on the main thread.
            geode::Loader::get()->queueInMainThread([]() {
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

    // Batch fetch manifest for main levels first, then prefetch
    std::vector<int> mainLevels;
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    // Load main assets ASAP. If LoadingLayer already started the prefetch (the
    // normal case), tryClaimMainLevelsPrefetch() returns false and we just log;
    // otherwise (e.g. texture reload) this fallback ensures they get loaded.
    if (paimon::tryClaimMainLevelsPrefetch()) {
        // Immediate manifest fetch; honors the 14-day cache window (skips the
        // network if all 22 are already on disk and fresh).
        paimon::preload::fetchMainLevelManifestWithCache(mainLevels, "Bootstrap");

        // Enqueue on the next frame to avoid contending with the first menu render.
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

        // Sync mod.json settings -> CursorManager config. Guard against re-entry
        // when saveConfig syncs back; atomic since listenForSettingChanges can
        // fire from any thread in Geode.
        static std::atomic<bool> s_cursorSyncGuard{false};
        geode::listenForSettingChanges<bool>("custom-cursor-enable", +[](bool value) {
            if (s_cursorSyncGuard.exchange(true, std::memory_order_acq_rel)) return;
            CursorManager::get().config().enabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard.store(false, std::memory_order_release);
        });
        // custom-cursor-scale / -trail are saved values (not mod.json settings),
        // applied live by the popup, so no setting listener fires for them.

        // Bump the global version so LevelCell & LevelInfoLayer re-cache settings.
        // Only mod.json keys fire listenForSettingChanges; granular keys are saved
        // values configured from the in-mod settings panel.
        geode::listenForSettingChanges<bool>("levelcell-hover-effects", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<bool>("compact-list-mode", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<double>("level-thumb-width", &paimonOnSettingChanged<double>);
        geode::listenForSettingChanges<std::string>("levelinfo-background-style", &paimonOnSettingChanged<std::string>);
    }

    log::info("[PaimonThumbnails][Init] Applying startup init");

    log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
    // Not a web request, so this can't move to WebTask.
    // Disk I/O + CPU work; runs in the background so the main thread isn't blocked.
    paimon::scheduleMainThreadDelay(3.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::ThreadTracker::get().spawn([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            if (paimon::isRuntimeShuttingDown()) return;
            LevelColors::get().extractColorsFromCache();
            if (paimon::isRuntimeShuttingDown()) return;
            geode::Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                log::info("[PaimonThumbnails][Init] Color extraction finished");
            });
        });
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");

    // Defer non-critical services so they don't block initial load.

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

    // Shader pre-warm: load after thumbnails are ready
    paimon::scheduleMainThreadDelay(10.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        Shaders::prewarmLevelInfoShaders();

        // Precompile the user's configured dynamic background shaders (rain,
        // matrix, crt, ...) so first entry doesn't pay the 4-10ms compile as a stutter.
        Shaders::prewarmConfiguredBackgroundShaders();

        // Verify the .glsl files in resources/shaders load correctly; on
        // failure the log shows the expected path.
        paimon::shaders::preloadBlurShaders();
    });

    // UpdateChecker: query GitHub Releases for new versions, after a short
    // delay so it doesn't compete with initial load.
    paimon::scheduleMainThreadDelay(8.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::updates::UpdateChecker::get().checkAsync();
    });
}

} // namespace paimon

$on_game(Loaded) {
    paimon::bootstrap();
}
