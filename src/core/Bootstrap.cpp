#include <Geode/Geode.hpp>
#include "../features/versus/VersusInit.hpp"
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
#include "../features/crash-reports/services/CrashReporter.hpp"
#include "RuntimeLifecycle.hpp"
#include "StartupIncompatibilityCheck.hpp"
#include "ModCompatWarnings.hpp"
#include "BanGate.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "MainLevelPrefetch.hpp"
#include "PreloadProgress.hpp"
#include "PreloadActions.hpp"
#include "Settings.hpp"
#include "../features/paidraw/PaiDrawManager.hpp"
#include "../video/VideoPlayer.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/BlurDiskCache.hpp"
#include "../utils/GDRobTopCache.hpp"

#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../features/rtx/services/RTXManager.hpp"
#include "../features/frame-interp/services/FrameInterpolator.hpp"
#include "../features/icon-gradients/GradientCache.hpp"
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
    paimon::rtx::RTXManager::get().init();
    paimon::frameinterp::FrameInterpolator::get().init();

    paimon::blur::BlurDiskCache::get().init();
    paimon::gd::GDRobTopCache::get().init();

    paimon::versus::init();

    // These managers publish shared config and touch Geode/Cocos state.
    LayerBackgroundManager::get().migrateFromLegacy();
    LayerBackgroundManager::get().migrateToGlobalMusic();
    LayerBackgroundManager::get().migrateExternalAssetsToManagedStorage();
    TransitionManager::get().loadConfig();
    ProgressBarManager::get().loadConfig();
    paimon::slider::CustomSliderManager::get().loadConfig();

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
    });

    if (paimon::preload::tryClaimPreload()) {
        paimon::preload::startFullPreload();
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

        // Module toggles backed by mod.json settings are read through the
        // version-stamped cache in ModuleRegistry, and Geode's own settings
        // panel writes them without going through modules::setEnabled.
        geode::listenForAllSettingChanges(
            +[](std::string_view, std::shared_ptr<geode::SettingV3>) {
                paimon::settings::internal::invalidateSettingsCache();
            });
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
    });

    // The gradient shader set is ~1240 independent programs (one per sprite
    // key; each holds its own uniforms). Compiling them on mod load blocked the
    // main thread for over a minute on low-end machines, so it is spread across
    // frames once the game is already past its own loading.
    paimon::scheduleMainThreadDelay(10.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::icon_gradients::GradientCache::prewarmShaders();
    });

    paimon::scheduleMainThreadDelay(8.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::updates::UpdateChecker::get().checkAsync();
    });

    paimon::scheduleMainThreadDelay(15.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::crash::reportPendingCrashes();
    });
}

} // namespace paimon

$on_game(Loaded) {
    paimon::preload::g_gameLoaded.store(true, std::memory_order_release);
    paimon::bootstrap();
}
