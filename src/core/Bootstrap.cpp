#include "../features/custom-hover/CustomHover.hpp"
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
#include "../blur/BlurSystem.hpp"
#include "../blur/BlurDiskCache.hpp"
#include "../utils/GDRobTopCache.hpp"

#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../features/rtx/services/RTXManager.hpp"
#include "../features/frame-interp/services/FrameInterpolator.hpp"
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

    // banned cache means no init; without cache this schedules a server check
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

    LayerBackgroundManager::get().migrateFromLegacy();
    LayerBackgroundManager::get().migrateToGlobalMusic();
    LayerBackgroundManager::get().migrateExternalAssetsToManagedStorage();
    TransitionManager::get().loadConfig();
    ProgressBarManager::get().loadConfig();
    paimon::slider::CustomSliderManager::get().loadConfig();
    paimon::hover::init();

    bool const clearCacheAtStartup = paimon::settings::general::clearCacheOnExit();

    paimon::ThreadTracker::get().spawn([clearCacheAtStartup]() {
        geode::utils::thread::setName("PaimonMigrations");
        if (paimon::isRuntimeShuttingDown()) return;

        if (clearCacheAtStartup) {
            cleanupDiskCache("startup-safety");
            auto saveDir = Mod::get()->getSaveDir();
            std::error_code ec;
            std::filesystem::remove(saveDir / "manifest_cache.json", ec);
            // Geode state must update on the main thread.
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

        // Geode can fire setting callbacks on any thread.
        static std::atomic<bool> s_cursorSyncGuard{false};
        geode::listenForSettingChanges<bool>("custom-cursor-enable", +[](bool value) {
            if (s_cursorSyncGuard.exchange(true, std::memory_order_acq_rel)) return;
            CursorManager::get().config().enabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard.store(false, std::memory_order_release);
        });

        // settings panel bypasses setEnabled: invalidate the settings cache.
        geode::listenForAllSettingChanges(
            +[](std::string_view, std::shared_ptr<geode::SettingV3>) {
                paimon::settings::internal::invalidateSettingsCache();
            });
    }

    log::info("[PaimonThumbnails][Init] Startup init complete");

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
