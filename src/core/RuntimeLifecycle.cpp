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
#include "RuntimeLifecycle.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "Settings.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../features/dynamic-volume/services/DynamicVolumeManager.hpp"
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
    // Destroy EventBus subscribers before Cocos2d tears down: their lambdas
    // capture WeakRef<CCNode>, and destroying them during atexit crashes once
    // the WeakRefPool is gone.
    paimon::EventBus::get().beginShutdown();

    paimon::markRuntimeShuttingDown();
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

    safeShutdownStep("foryou-save", []() {
        paimon::foryou::TasteProfile::get().save();
    });
    log::info("[SHUTDOWN] 1/14 TasteProfile saved");

    safeShutdownStep("http-clean-tasks", []() {
        HttpClient::get().cleanTasks(false);
    });
    log::info("[SHUTDOWN] 2/14 HttpClient tasks cleaned");

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

    log::info("[SHUTDOWN] 4/14 ThumbnailLoader cleanup starting...");
    safeShutdownStep("thumbnail-loader-cleanup", []() {
        ThumbnailLoader::get().cleanup();
    });
    log::info("[SHUTDOWN] 4/14 ThumbnailLoader cleanup DONE");
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

    safeShutdownStep("level-colors-flush", []() {
        LevelColors::get().flushIfDirty();
    });
    log::info("[SHUTDOWN] 8/14 LevelColors flushed");

    safeShutdownStep("profile-thumbs-clear-cache", []() {
        ProfileThumbs::get().clearAllCache();
        ProfileThumbs::get().clearNoProfileCache();
    });

    // Clear pending callbacks capturing Ref<GJScoreCell> etc.; otherwise the
    // static destructor would destroy them after CCPoolManager is gone -> crash.
    safeShutdownStep("profile-thumbs-clear-pending", []() {
        ProfileThumbs::get().clearPendingDownloads();
    });
    log::info("[SHUTDOWN] 9/14 ProfileThumbs caches cleared");

    log::info("[SHUTDOWN] 10/14 AnimatedGIFSprite clearCache starting...");
    safeShutdownStep("animated-gif-clear", []() {
        AnimatedGIFSprite::clearCache();
    });
    log::info("[SHUTDOWN] 10/14 AnimatedGIFSprite clearCache DONE");

    log::info("[SHUTDOWN] 11/14 VideoThumbnailSprite clearCache starting...");
    safeShutdownStep("video-thumbnail-clear", []() {
        VideoThumbnailSprite::clearCache();
    });
    log::info("[SHUTDOWN] 11/14 VideoThumbnailSprite clearCache DONE");

    safeShutdownStep("emote-cache-clear-ram", []() {
        paimon::emotes::EmoteCache::get().clearRam();
    });
    log::info("[SHUTDOWN] 12/14 EmoteCache RAM cleared");

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
    // Before the FMOD engine goes away: releases our fader/meter DSPs.
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
    log::info("[SHUTDOWN] 13/14 Audio + resources released");

    // Release shared video players before MF shuts down: the LayerBackgroundManager
    // static destructor otherwise runs during atexit after MF is torn down, crashing
    // WindowsDecoder::close() in msmpeg2vdec.dll.
    log::info("[SHUTDOWN] 14/14 releaseAllSharedVideos starting...");
    safeShutdownStep("layer-bg-release-videos", []() {
        LayerBackgroundManager::get().releaseAllSharedVideos();
    });
    log::info("[SHUTDOWN] 14/14 releaseAllSharedVideos DONE");

    safeShutdownStep("blur-system-destroy", []() {
        BlurSystem::getInstance()->destroy();
    });

    bool clearCache = clearCacheOnExit;
    if (!clearCache) {
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

    log::info("[PaimonThumbnails] All caches cleaned on exit");
    log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ===");
}
