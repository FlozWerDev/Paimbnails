#include <Geode/Geode.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongSubmerge.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/menu-music/services/SongCoverCache.hpp"
#include "../features/menu-music/services/MenuMusicEffects.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/BlurDiskCache.hpp"
#include "../utils/GDRobTopCache.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/foryou/services/TasteProfile.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../features/icon-maker/services/IconShare.hpp"
#include "../features/collab-editor/CollabManager.hpp"
#include "RuntimeLifecycle.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "Settings.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../features/dynamic-volume/services/DynamicVolumeManager.hpp"
#include "../features/transitions/services/LevelEntryEffects.hpp"
#include "../features/transitions/services/TransitionMedia.hpp"
#include "../features/transitions/services/DynamicTransitionManager.hpp"
#include "../framework/ModEvents.hpp"
#include "../framework/EventBus.hpp"
#include "../utils/ThreadTracker.hpp"
#include <filesystem>
#include <atomic>

using namespace geode::prelude;

namespace {
std::atomic<bool> s_runtimeShuttingDown{false};

void removePathIfExists(std::filesystem::path const& path, char const* label) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }

    std::filesystem::remove_all(path, ec);
    if (ec) {
        log::warn("[PaimonThumbnails] Failed to remove {} at {}: {}", label, geode::utils::string::pathToString(path), ec.message());
    } else {
        log::info("[PaimonThumbnails] Removed {} at {}", label, geode::utils::string::pathToString(path));
    }
}

// Swallow exceptions so a failure in one phase doesn't abort the rest of shutdown.
template <typename Fn>
void safeShutdownStep(char const* stepName, Fn&& fn) {
    try {
        fn();
    } catch (std::exception const& e) {
        log::error("[SHUTDOWN] step '{}' threw: {}", stepName, e.what());
    } catch (...) {
        log::error("[SHUTDOWN] step '{}' threw unknown exception", stepName);
    }
}
}

namespace paimon {

bool isRuntimeShuttingDown() {
    return s_runtimeShuttingDown.load(std::memory_order_acquire) || ThreadTracker::get().isShuttingDown();
}

void markRuntimeShuttingDown() {
    s_runtimeShuttingDown.store(true, std::memory_order_release);
}

} // namespace paimon

void cleanupDiskCache(char const* context) {
    bool clearCache = paimon::settings::general::clearCacheOnExit();

    if (!clearCache) {
        log::info("[PaimonThumbnails] Cache cleanup disabled by setting ({})", context);
        return;
    }

    auto const cacheDir = paimon::quality::cacheDir();

    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        log::info("[PaimonThumbnails] Cache dir does not exist, nothing to clean ({})", context);
        return;
    }

    log::info("[PaimonThumbnails] Cleaning quality cache tree ({}; preserving main levels 1-22 + cache/gifs/)", context);

    auto [preserved, removed] = paimon::clearCachePreservingMainLevels(cacheDir, {"gifs"});
    log::info("[PaimonThumbnails] Cache cleanup ({}): preserved {} entries, removed {}",
        context, preserved, removed);

    log::info("[PaimonBlur] Clearing blur disk cache ({})", context);
    paimon::blur::BlurDiskCache::get().clear();
}

$on_game(Exiting) {
    // EventBus first: subscriber lambdas crash in atexit once the WeakRefPool is gone.
    paimon::EventBus::get().beginShutdown();

    paimon::markRuntimeShuttingDown();
    paimon::cancelAllMainThreadDelays();
    pt::cancelPendingFilePick();
    paimon::icon_maker::IconShare::cancelPendingPick();
    paimon::collab::CollabManager::get().disconnect();
    FramebufferCapture::cancelPending();
    paimon::ThreadTracker::get().shutdown();
    log::info("[SHUTDOWN] === BEGIN EXIT SEQUENCE ===");

    safeShutdownStep("auto-update-stage", []() {
        if (paimon::settings::general::autoUpdate()) {
            auto& checker = paimon::updates::UpdateChecker::get();
            if (checker.hasPendingInstall()) {
                log::info("[SHUTDOWN] Auto-update: applying pending update silently");
                if (!checker.applyPendingUpdateInPlace()) {
                    log::warn("[SHUTDOWN] Auto-update: failed to spawn updater helper");
                }
            }
        }
    });
    safeShutdownStep("update-checker-shutdown", []() {
        paimon::updates::UpdateChecker::get().shutdown();
    });

    safeShutdownStep("foryou-save", []() {
        paimon::foryou::TasteProfile::get().save();
    });

    safeShutdownStep("http-clean-tasks", []() {
        HttpClient::get().cleanTasks(false);
    });

    safeShutdownStep("emote-shutdown", []() {
        paimon::emotes::EmoteCache::get().shutdown();
    });

    safeShutdownStep("profile-thumbs-flag", []() {
        ProfileThumbs::s_shutdownMode.store(true, std::memory_order_release);
    });
    safeShutdownStep("discord-shutdown", []() {
        paimon::discord::DiscordPresenceManager::get().shutdown();
    });

    safeShutdownStep("song-cover-cache-cleanup", []() {
        paimon::menumusic::SongCoverCache::get().cleanup();
    });

    safeShutdownStep("thumbnail-loader-cleanup", []() {
        ThumbnailLoader::get().cleanup();
    });
    safeShutdownStep("blur-disk-cache-shutdown", []() {
        paimon::blur::BlurDiskCache::get().shutdown();
    });
    safeShutdownStep("gd-robtop-cache-shutdown", []() {
        paimon::gd::GDRobTopCache::get().shutdown();
    });

    bool clearCacheOnExit = paimon::settings::general::clearCacheOnExit();

    if (!clearCacheOnExit) {
        safeShutdownStep("save-disk-index", []() {
            paimon::cache::ThumbnailCache::get().saveDiskIndex(true);
            (void)Mod::get()->saveData();
        });
    }

    safeShutdownStep("local-thumbs-shutdown", []() {
        LocalThumbs::get().shutdown();
    });

    safeShutdownStep("level-colors-flush", []() {
        LevelColors::get().flushIfDirty();
    });

    safeShutdownStep("profile-thumbs-clear-cache", []() {
        ProfileThumbs::get().clearAllCache();
        ProfileThumbs::get().clearNoProfileCache();
    });

    // drop pending callbacks holding Refs; statics dying after CCPoolManager crash.
    safeShutdownStep("profile-thumbs-clear-pending", []() {
        ProfileThumbs::get().clearPendingDownloads();
    });

    safeShutdownStep("animated-gif-clear", []() {
        AnimatedGIFSprite::clearCache();
    });

    safeShutdownStep("video-thumbnail-clear", []() {
        VideoThumbnailSprite::clearCache();
    });

    safeShutdownStep("emote-cache-clear-ram", []() {
        paimon::emotes::EmoteCache::get().clearRam();
    });

    safeShutdownStep("thumbnail-bg-event-clear", []() {
        paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = 0;
        paimon::ThumbnailBackgroundChangedEvent::setLastTexture(nullptr);
    });

    safeShutdownStep("dynamic-song-kill", []() {
        DynamicSongManager::get()->forceKill();
    });
    // Before the FMOD engine goes away: releases the dive filter's DSPs.
    safeShutdownStep("dynamic-song-submerge-shutdown", []() {
        paimon::dynsong::SubmergeEffect::get().shutdown();
    });
    safeShutdownStep("beat-shader-shutdown", []() {
        paimon::beat_shaders::BeatShaderManager::get().shutdown();
    });
    safeShutdownStep("menu-music-effects-shutdown", []() {
        paimon::menumusic::MenuMusicEffects::get().shutdown();
    });
    safeShutdownStep("dynamic-volume-shutdown", []() {
        paimon::dynvol::DynamicVolumeManager::get().shutdown();
    });
    safeShutdownStep("profile-music-stop", []() {
        ProfileMusicManager::get().forceStop();
    });
    safeShutdownStep("pet-release", []() {
        PetManager::get().releaseSharedResources();
    });
    safeShutdownStep("cursor-release", []() {
        CursorManager::get().releaseSharedResources();
    });

    // release shared videos before MF dies; the static destructor crashes in msmpeg2vdec.dll.
    safeShutdownStep("layer-bg-release-videos", []() {
        LayerBackgroundManager::get().releaseAllSharedVideos();
    });

    safeShutdownStep("blur-system-destroy", []() {
        BlurSystem::getInstance()->destroy();
    });

    safeShutdownStep("transition-watchdog-disarm", []() {
        paimon::transitions::shutdownLevelTransitionWatchdog();
    });
    // join the media worker before statics die so it can't touch the cache in atexit.
    safeShutdownStep("transition-media-shutdown", []() {
        paimon::transitions::shutdownTransitionMedia();
    });
    safeShutdownStep("dynamic-transition-history-clear", []() {
        paimon::transitions::dynamic::clearHistory();
    });

    if (!clearCacheOnExit) {
        log::info("[PaimonThumbnails] Disk cache cleanup disabled by setting");
        log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ===");
        return;
    }

    safeShutdownStep("disk-cache-cleanup", []() {
        cleanupDiskCache("exit");
    });

    safeShutdownStep("disk-index-clear", []() {
        Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
    });

    safeShutdownStep("server-cache-remove", []() {
        auto saveDir = Mod::get()->getSaveDir();
        removePathIfExists(saveDir / "manifest_cache.json", "manifest cache");
        removePathIfExists(saveDir / "profile_music", "profile music cache");
        removePathIfExists(saveDir / "profileimg_cache", "profile image cache");
    });

    log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ===");
}
