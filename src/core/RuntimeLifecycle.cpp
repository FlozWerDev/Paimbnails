// Lifecycle: startup and shutdown.
// - cleanupDiskCache(): selective disk-cache cleanup
// - $on_game(Exiting): RAM and disk cleanup on game exit

#include <Geode/Geode.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/menu-music/services/SongCoverCache.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/BlurDiskCache.hpp"
#include "../utils/GDRobTopCache.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/HttpClient.hpp"
#include "../video/VideoNormalizer.hpp"
#include "RuntimeLifecycle.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "Settings.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
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

// Run one shutdown step, swallowing exceptions so a failure in one phase
// doesn't abort the rest. Without this, a throwing manager would leave caches
// unsaved and crash instead of exiting cleanly.
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

// Disk-cache cleanup, used at both startup and exit. Preserves main levels
// (1-22): even with "clear-cache-on-exit" on, official-level thumbnails stay on
// disk so the next session starts with a warm cache.
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

    // Also clear the blur disk cache when clear-cache-on-exit is set.
    log::info("[PaimonBlur] Clearing blur disk cache ({})", context);
    paimon::blur::BlurDiskCache::get().clear();
}

// On game exit:
// - set flags so static destructors don't release() Cocos2d objects
// - clear server-data caches (profiles, GIFs, music, profileimg)
// - leave user offline data alone (menu backgrounds, local thumbnails, settings)
$on_game(Exiting) {
    // Destroy EventBus subscribers before Cocos2d tears down. Their lambdas
    // capture WeakRef<CCNode>; destroying them during atexit (EventBus dtor)
    // crashes because the WeakRefPool is already gone.
    paimon::EventBus::get().beginShutdown();

    paimon::markRuntimeShuttingDown();
    paimon::ThreadTracker::get().shutdown();
    log::info("[SHUTDOWN] === BEGIN EXIT SEQUENCE ===");

    // Auto-update: if a .geode is staged and auto-update is on, spawn the
    // PowerShell helper now. It waits for the game process to exit before
    // touching the file, so launching it at shutdown start is safe. We don't
    // relaunch; the user gets the new version next time they open GD.
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

    // For You: save recommendation profile before cleanup
    safeShutdownStep("foryou-save", []() {
        paimon::foryou::ForYouTracker::get().save();
    });
    log::info("[SHUTDOWN] 1/14 ForYouTracker saved");

    safeShutdownStep("http-clean-tasks", []() {
        HttpClient::get().cleanTasks(false);
    });
    log::info("[SHUTDOWN] 2/14 HttpClient tasks cleaned");

    // Cancel emote preload and decode workers before continuing
    safeShutdownStep("emote-shutdown", []() {
        paimon::emotes::EmoteCache::get().shutdown();
    });
    log::info("[SHUTDOWN] 3/14 EmoteCache shutdown complete");

    safeShutdownStep("profile-thumbs-flag", []() {
        ProfileThumbs::s_shutdownMode.store(true, std::memory_order_release);
    });
    safeShutdownStep("discord-shutdown", []() {
        paimon::discord::DiscordPresenceManager::get().shutdown();
    });

    safeShutdownStep("song-cover-cache-cleanup", []() {
        paimon::menumusic::SongCoverCache::get().cleanup();
    });

    // Cancel pending ThumbnailLoader tasks before clearing disk so background
    // threads don't rewrite files we're about to delete.
    log::info("[SHUTDOWN] 4/14 ThumbnailLoader cleanup starting...");
    safeShutdownStep("thumbnail-loader-cleanup", []() {
        ThumbnailLoader::get().cleanup();
    });
    log::info("[SHUTDOWN] 4/14 ThumbnailLoader cleanup DONE");
    safeShutdownStep("video-normalizer-shutdown", []() {
        paimon::video::VideoNormalizer::shutdownAsyncWork();
    });

    // Blur disk cache: mark shutdown so it rejects new lookups/stores.
    safeShutdownStep("blur-disk-cache-shutdown", []() {
        paimon::blur::BlurDiskCache::get().shutdown();
    });
    safeShutdownStep("gd-robtop-cache-shutdown", []() {
        paimon::gd::GDRobTopCache::get().shutdown();
    });

    bool clearCacheOnExit = paimon::settings::general::clearCacheOnExit();

    // persist disk index before closing (synchronous, no more workers)
    // only if we're not going to delete it immediately after
    if (!clearCacheOnExit) {
        safeShutdownStep("save-disk-index", []() {
            paimon::cache::ThumbnailCache::get().saveDiskIndex(true);
            // flush saved values to disk so the index survives across sessions
            (void)Mod::get()->saveData();
        });
    }
    log::info("[SHUTDOWN] 5/14 Disk index persisted");

    log::info("[SHUTDOWN] 6/14 LocalThumbs shutdown starting...");
    safeShutdownStep("local-thumbs-shutdown", []() {
        LocalThumbs::get().shutdown();
    });
    log::info("[SHUTDOWN] 6/14 LocalThumbs shutdown DONE");

    log::info("[SHUTDOWN] 7/14 ProfileThumbs shutdown starting...");
    safeShutdownStep("profile-thumbs-shutdown", []() {
        ProfileThumbs::get().shutdown();
    });
    log::info("[SHUTDOWN] 7/14 ProfileThumbs shutdown DONE");

    // Flush pending colors before exit (always: thumbnails/ is no longer
    // deleted in cleanup).
    safeShutdownStep("level-colors-flush", []() {
        LevelColors::get().flushIfDirty();
    });
    log::info("[SHUTDOWN] 8/14 LevelColors flushed");

    // 1. other users' profile cache (in-memory thumbnails + GIFs)
    safeShutdownStep("profile-thumbs-clear-cache", []() {
        ProfileThumbs::get().clearAllCache();
        ProfileThumbs::get().clearNoProfileCache();
    });

    // 1b. clear pending callbacks capturing Ref<GJScoreCell> etc.; otherwise
    //     ProfileThumbs' static destructor would destroy them after
    //     CCPoolManager is gone -> crash.
    safeShutdownStep("profile-thumbs-clear-pending", []() {
        ProfileThumbs::get().clearPendingDownloads();
    });
    log::info("[SHUTDOWN] 9/14 ProfileThumbs caches cleared");

    // 2. global in-RAM animated-GIF cache
    log::info("[SHUTDOWN] 10/14 AnimatedGIFSprite clearCache starting...");
    safeShutdownStep("animated-gif-clear", []() {
        AnimatedGIFSprite::clearCache();
    });
    log::info("[SHUTDOWN] 10/14 AnimatedGIFSprite clearCache DONE");

    // 2b. global on-disk video cache (temp files)
    log::info("[SHUTDOWN] 11/14 VideoThumbnailSprite clearCache starting...");
    safeShutdownStep("video-thumbnail-clear", []() {
        VideoThumbnailSprite::clearCache();
    });
    log::info("[SHUTDOWN] 11/14 VideoThumbnailSprite clearCache DONE");

    // 2c. in-RAM emote cache (Ref<CCTexture2D> + gifData); clear RAM only,
    //     disk stays as persistent cache.
    safeShutdownStep("emote-cache-clear-ram", []() {
        paimon::emotes::EmoteCache::get().clearRam();
    });
    log::info("[SHUTDOWN] 12/14 EmoteCache RAM cleared");

    safeShutdownStep("thumbnail-bg-event-clear", []() {
        paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = 0;
        // Release the reference (decrement refcount). Defensive: may be a no-op
        // if the GL context or CCPoolManager are already gone.
        paimon::ThumbnailBackgroundChangedEvent::setLastTexture(nullptr);
    });

    // 3. force-stop dynamic/profile audio (avoids mid-fade/transition states
    // during shutdown)
    safeShutdownStep("dynamic-song-kill", []() {
        DynamicSongManager::get()->forceKill();
    });
    safeShutdownStep("beat-shader-shutdown", []() {
        paimon::beat_shaders::BeatShaderManager::get().shutdown();
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
    log::info("[SHUTDOWN] 13/14 Audio + resources released");

    // 3b. release shared video players before MF shuts down.
    // Without this, the LayerBackgroundManager static singleton destructor
    // runs during atexit after MF is already torn down, causing
    // WindowsDecoder::close() to crash in msmpeg2vdec.dll.
    log::info("[SHUTDOWN] 14/14 releaseAllSharedVideos starting...");
    safeShutdownStep("layer-bg-release-videos", []() {
        LayerBackgroundManager::get().releaseAllSharedVideos();
    });
    log::info("[SHUTDOWN] 14/14 releaseAllSharedVideos DONE");

    // 3c. release BlurSystem FBOs and textures (must be on GL thread)
    safeShutdownStep("blur-system-destroy", []() {
        BlurSystem::getInstance()->destroy();
    });

    bool clearCache = clearCacheOnExit;
    if (!clearCache) {
        log::info("[PaimonThumbnails] Disk cache cleanup disabled by setting");
        log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ===");
        return;
    }

    // 4. regenerable quality-aware cache (thumbnails, GIFs, profiles, derived manifests)
    safeShutdownStep("disk-cache-cleanup", []() {
        cleanupDiskCache("exit");
    });

    // clear disk index saved value so it doesn't reference deleted files on next launch
    safeShutdownStep("disk-index-clear", []() {
        Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
    });

    // 5. regenerable server caches (profile music, profile images, CDN manifest)
    safeShutdownStep("server-cache-remove", []() {
        auto saveDir = Mod::get()->getSaveDir();
        removePathIfExists(saveDir / "manifest_cache.json", "manifest cache");
        removePathIfExists(saveDir / "profile_music", "profile music cache");
        removePathIfExists(saveDir / "profileimg_cache", "profile image cache");
        // Leave thumbnails/, saved_thumbnails/ and downloaded_thumbnails/ alone:
        // they're user-local data that can't be regenerated.
    });

    log::info("[PaimonThumbnails] All caches cleaned on exit");
    log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ===");
}
