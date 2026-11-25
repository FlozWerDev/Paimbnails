#include "../features/custom-hover/CustomHover.hpp"
#include <Geode/Geode.hpp>
#include "../features/versus/VersusInit.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../features/cursor/services/CursorManager.hpp"
#endif
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/HttpClient.hpp"
#include "../features/progressbar/services/ProgressBarManager.hpp"
#include "../features/custom-slider/services/CustomSliderManager.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../features/crash-reports/services/CrashReporter.hpp"
#include "../features/onboarding/WelcomeFlow.hpp"
#include "RuntimeLifecycle.hpp"
#include "../platform/PaimSettings.hpp"
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

// atomic: menulayer::init can re-enter when the scene reloads
std::atomic<bool> g_languageListenerRegistered{false};

}

namespace paimon {

void bootstrap() {
    log::info("[PaimonThumbnails][Init] Loaded event start");

    // ban enforced async earlier in this session: refuse re-init, game keeps running.
    if (paimon::ban::isBanned()) {
        log::warn("[PaimonThumbnails][Init] Aborting init: user is banned");
        return;
    }

    // known bans block init; online access requires a live session verdict.
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
#ifdef PAIM_USE_GEODE
        geode::utils::thread::setName("PaimonMigrations");
#endif
        if (paimon::isRuntimeShuttingDown()) return;

        if (clearCacheAtStartup) {
            cleanupDiskCache("startup-safety");
            auto saveDir = paimon::platform::activeBackend().getSaveDir();
            std::error_code ec;
            std::filesystem::remove(saveDir / "manifest_cache.json", ec);
            // geode state must update on the main thread.
            paimon::platform::activeBackend().queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
#ifdef PAIM_USE_GEODE
                Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
#endif
            });
        }

        if (paimon::isRuntimeShuttingDown()) return;
        // stat the 44 main-level thumbnails first: the +1s preload asks for
        // this from the main thread and would otherwise stat them itself.
        paimon::warmMainLevelsDiskCache();

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
        paimon::platform::activeBackend().observeSetting("language", +[](std::string_view) {
            // geode may fire this off the main thread: cocos/ui work below is main-only.
            paimon::platform::activeBackend().queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                std::string value = paimon::platform::gset<std::string>("language");
                applyLanguageSetting(value);
                log::info("[PaimonThumbnails][Language] Changed to '{}'", value);
            });
        });

#if defined(GEODE_IS_DESKTOP)
        // geode can fire setting callbacks on any thread.
        static std::atomic<bool> s_cursorSyncGuard{false};
        paimon::platform::activeBackend().observeSetting("custom-cursor-enable", +[](std::string_view) {
            paimon::platform::activeBackend().queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                bool value = paimon::platform::gset<bool>("custom-cursor-enable");
                if (CursorManager::get().config().enabled == value) return;
                if (s_cursorSyncGuard.exchange(true, std::memory_order_acq_rel)) return;
                CursorManager::get().config().enabled = value;
                CursorManager::get().applyConfigLive();
                s_cursorSyncGuard.store(false, std::memory_order_release);
            });
        });
#endif

        // settings panel bypasses setenabled: invalidate the settings cache.
        paimon::platform::activeBackend().observeAll(
            +[](std::string_view) {
                paimon::platform::invalidateSettingsCache();
            });
    }

    log::info("[PaimonThumbnails][Init] Startup init complete");

    // both deferred checks share the same shutdown/acceptance guards.
    auto scheduleIfAccepted = [](float delay, geode::CopyableFunction<void()> fn) {
        paimon::scheduleMainThreadDelay(delay, [fn = std::move(fn)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!paimon::onboarding::isAccepted()) return;
            if (fn) fn();
        });
    };

    scheduleIfAccepted(8.0f, []() {
        paimon::updates::UpdateChecker::get().checkAsync();
    });

    scheduleIfAccepted(15.0f, []() {
        paimon::crash::reportPendingCrashes();
    });
}

} // namespace paimon

$on_game(Loaded) {
    paimon::preload::g_gameLoaded.store(true, std::memory_order_release);
    paimon::bootstrap();
}
