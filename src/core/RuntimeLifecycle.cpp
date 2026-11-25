#include "../utils/FilesystemPath.hpp"
#include <Geode/Geode.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/auto-preview/services/LevelDataProvider.hpp"
#include "../features/profiles/services/ProfileImageCache.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include "../features/thread-images/ChatImageService.hpp"
#include "../features/level-thumbs/services/LevelThumbsClient.hpp"
#include "../features/icon-gradients/services/GradientImage.hpp"
#include "../features/icon-gradients/services/GradientAnimationManager.hpp"
#include "../features/twitch-requests/services/StreamOverlayServer.hpp"
#include "../features/editor-mcp/EditorMcpServer.hpp"
#include "../features/twitch-requests/services/TwitchLevelBriefCache.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongSubmerge.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/backgrounds/services/WallpaperThumbnailCache.hpp"
#include "../features/backgrounds/ui/LiveWallpaperNode.hpp"
#include "../features/pet/services/PetManager.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../features/cursor/services/CursorManager.hpp"
#endif
#include "../features/menu-music/services/SongCoverCache.hpp"
#include "../features/menu-music/services/MenuMusicEffects.hpp"
#include "../features/editor-music/services/EditorMusicPlayer.hpp"
#include "../features/menu-music/services/MenuMusicLibrarySave.hpp"
#include "../features/menu-music/ui/components/VinylDisc.hpp"
#include "../features/button-skins/services/ButtonSkinRuntime.hpp"
#include "../features/gif-import/services/GifCompute.hpp"
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
#include "../utils/AsyncImageLoad.hpp"
#include "../utils/TextureBudget.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/WebSocketClient.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../features/icon-maker/services/IconThumbs.hpp"
#include "../features/icon-maker/services/IconBuildService.hpp"
#include "../features/icon-maker/services/IconShare.hpp"
#include "../features/collab-editor/CollabManager.hpp"
#include "RuntimeLifecycle.hpp"
#include "../platform/PaimSettings.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "Settings.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/thumb-alerts/services/ThumbFeedSocket.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../features/dynamic-volume/services/DynamicVolumeManager.hpp"
#include "../features/transitions/services/LevelEntryEffects.hpp"
#include "../features/transitions/services/TransitionMedia.hpp"
#include "../features/transitions/services/DynamicTransitionManager.hpp"
#include "../framework/ModEvents.hpp"
#include "../framework/EventBus.hpp"
#include "../utils/ThreadTracker.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

using namespace geode::prelude;

namespace {
std::atomic<bool> s_runtimeShuttingDown{false};

// Heap-leaked so detached shutdown waiters never touch destroyed statics in atexit.
std::mutex& shutdownWaitMutex() {
    static auto* m = new std::mutex();
    return *m;
}

std::condition_variable& shutdownWaitCV() {
    static auto* cv = new std::condition_variable();
    return *cv;
}

void removePathIfExists(std::filesystem::path const& path, char const* label) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }

    std::filesystem::remove_all(path, ec);
    if (ec) {
        log::warn("[PaimonThumbnails] Failed to remove {} at {}: {}", label, paimon::paths::toUtf8(path), ec.message());
    } else {
        log::info("[PaimonThumbnails] Removed {} at {}", label, paimon::paths::toUtf8(path));
    }
}

// swallow exceptions so a failure in one phase doesn't abort the rest of shutdown.
template <typename Fn>
void safeShutdownStep(char const* stepName, Fn&& fn) {
    auto const start = std::chrono::steady_clock::now();
    try {
        fn();
    } catch (std::exception const& e) {
        log::error("[SHUTDOWN] step '{}' threw: {}", stepName, e.what());
    } catch (...) {
        log::error("[SHUTDOWN] step '{}' threw unknown exception", stepName);
    }
    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();
    if (elapsedMs >= 100) {
        log::info("[SHUTDOWN] step '{}' took {}ms", stepName, elapsedMs);
    }
}
}

namespace paimon {

bool isRuntimeShuttingDown() {
    return s_runtimeShuttingDown.load(std::memory_order_acquire) || ThreadTracker::get().isShuttingDown();
}

void markRuntimeShuttingDown() {
    {
        std::lock_guard lock(shutdownWaitMutex());
        s_runtimeShuttingDown.store(true, std::memory_order_release);
    }
    shutdownWaitCV().notify_all();
}

bool waitForShutdownOr(std::chrono::milliseconds duration) {
    if (isRuntimeShuttingDown()) return true;
    std::unique_lock lock(shutdownWaitMutex());
    return shutdownWaitCV().wait_for(lock, duration, [] {
        return s_runtimeShuttingDown.load(std::memory_order_acquire)
            || ThreadTracker::get().isShuttingDown();
    });
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
    auto const exitStart = std::chrono::steady_clock::now();

    // ── Phase 1: Broadcast shutdown & cancel UI-bound work ──────────────────
    // eventbus first: subscriber lambdas crash in atexit once the weakrefpool is gone.
    paimon::EventBus::get().beginShutdown();
    paimon::markRuntimeShuttingDown();
    paimon::ThreadTracker::get().requestShutdown();
    log::info("[SHUTDOWN] === BEGIN EXIT SEQUENCE ===");

    paimon::cancelAllMainThreadDelays();
    pt::cancelPendingFilePick();
    paimon::icon_maker::IconShare::cancelPendingPick();
    FramebufferCapture::cancelPending();

    if (paimon::autopreview::LevelDataProvider::isInitialized()) {
        safeShutdownStep("level-data-provider-shutdown", []() {
            paimon::autopreview::LevelDataProvider::get().shutdown();
        });
    }
    if (paimon::gd::GDRobTopCache::isInitialized()) {
        safeShutdownStep("gd-robtop-cache-shutdown", []() {
            paimon::gd::GDRobTopCache::get().shutdown();
        });
    }
    if (HttpClient::isInitialized()) {
        safeShutdownStep("http-clean-tasks", []() {
            HttpClient::get().cleanTasks(false);
        });
    }

    safeShutdownStep("collab-disconnect", []() {
        paimon::collab::CollabManager::get().disconnect();
    });
    safeShutdownStep("gradient-animation-shutdown", []() {
        paimon::icon_gradients::GradientAnimationManager::get().shutdown();
    });
    safeShutdownStep("twitch-level-cache-shutdown", []() {
        paimon::twitch::TwitchLevelBriefCache::get().shutdown();
    });
    safeShutdownStep("icon-build-callbacks-shutdown", []() {
        paimon::icon_maker::IconBuildService::shutdown();
    });

    safeShutdownStep("auto-update-stage", []() {
        if (paimon::settings::general::autoUpdate() &&
            paimon::updates::UpdateChecker::isInitialized()) {
            auto& checker = paimon::updates::UpdateChecker::get();
            if (checker.hasPendingInstall()) {
                log::info("[SHUTDOWN] Auto-update: applying pending update silently");
                if (!checker.applyPendingUpdateInPlace()) {
                    log::warn("[SHUTDOWN] Auto-update: failed to spawn updater helper");
                }
            }
        }
    });

    // Flush dirty persistent state only if that subsystem was actually initialized.
    if (paimon::foryou::TasteProfile::isInitialized()) {
        safeShutdownStep("foryou-save", []() {
            paimon::foryou::TasteProfile::get().save();
        });
    }
    if (LevelColors::isInitialized()) {
        safeShutdownStep("level-colors-flush", []() {
            LevelColors::get().flushIfDirty();
        });
    }

    bool const clearCacheOnExit = paimon::settings::general::clearCacheOnExit();
    if (!clearCacheOnExit && paimon::cache::ThumbnailCache::isAlive()) {
        safeShutdownStep("save-disk-index", []() {
            paimon::cache::ThumbnailCache::get().saveDiskIndex(true);
        });
    }

    // ── Phase 2: Parallel non-blocking stop signals ─────────────────────────
    safeShutdownStep("request-stop-signals", []() {
        paimon::net::WebSocketClient::shutdownAll();
        paimon::wallpaper::LiveWallpaperNode::requestStopAll();
        paimon::transitions::requestStopTransitionMedia();
        paimon::asyncimg::requestShutdown();
        VideoThumbnailSprite::requestShutdown();
        AnimatedGIFSprite::requestStopWorker();

        if (paimon::twitch::StreamOverlayServer::isInitialized()) {
            paimon::twitch::StreamOverlayServer::get().requestStop();
        }
        if (paimon::editormcp::EditorMcpServer::isInitialized()) {
            paimon::editormcp::EditorMcpServer::get().requestStop();
        }
        if (ThumbnailLoader::isInitialized()) {
            ThumbnailLoader::get().requestShutdown();
        }
        if (paimon::chatimg::ChatImageService::isInitialized()) {
            paimon::chatimg::ChatImageService::get().requestShutdown();
        }
        if (paimon::levelthumbs::LevelThumbsClient::isInitialized()) {
            paimon::levelthumbs::LevelThumbsClient::get().requestShutdown();
        }
        if (ProfileImageService::isInitialized()) {
            ProfileImageService::get().requestShutdown();
        }
        if (paimon::blur::BlurDiskCache::isInitialized()) {
            paimon::blur::BlurDiskCache::get().requestShutdown();
        }
        if (paimon::emotes::EmoteCache::isInitialized()) {
            paimon::emotes::EmoteCache::get().requestShutdown();
        }
        if (LocalThumbs::isInitialized()) {
            LocalThumbs::get().markShuttingDown();
        }
        if (paimon::wallpaper::WallpaperThumbnailCache::isInitialized()) {
            paimon::wallpaper::WallpaperThumbnailCache::get().shutdown();
        }
        if (paimon::updates::UpdateChecker::isInitialized()) {
            paimon::updates::UpdateChecker::get().shutdown();
        }
        paimon::thumbalerts::ThumbFeedSocket::get().stop();
        if (paimon::discord::DiscordPresenceManager::isInitialized()) {
            paimon::discord::DiscordPresenceManager::get().shutdown();
        }
        ProfileThumbs::s_shutdownMode.store(true, std::memory_order_release);
    });

    // ── Phase 3: Parallel join/wait sharing one 1500ms budget ───────────────
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);

    if (paimon::menumusic::MenuMusicLibrarySaveQueue::isInitialized()) {
        safeShutdownStep("menumusic-save-flush", [deadline]() {
            if (!paimon::menumusic::MenuMusicLibrarySaveQueue::get().shutdownUntil(deadline)) {
                log::warn("[SHUTDOWN] Menu music library save did not complete cleanly");
            }
        });
    }
    safeShutdownStep("thread-tracker-wait", [deadline]() {
        paimon::ThreadTracker::get().waitShutdown(deadline, std::chrono::milliseconds(150));
    });
    if (ThumbnailLoader::isInitialized()) {
        safeShutdownStep("thumbnail-loader-cleanup", [deadline]() {
            ThumbnailLoader::get().cleanupUntil(deadline);
        });
    }
    if (paimon::chatimg::ChatImageService::isInitialized()) {
        safeShutdownStep("chat-image-wait", [deadline]() {
            paimon::chatimg::ChatImageService::get().waitShutdown(deadline);
        });
    }
    if (paimon::levelthumbs::LevelThumbsClient::isInitialized()) {
        safeShutdownStep("level-thumbs-wait", [deadline]() {
            paimon::levelthumbs::LevelThumbsClient::get().waitShutdown(deadline);
        });
    }
    safeShutdownStep("asyncimg-wait", [deadline]() {
        paimon::asyncimg::waitShutdown(deadline);
    });
    if (ProfileImageService::isInitialized()) {
        safeShutdownStep("profile-image-service-wait", [deadline]() {
            ProfileImageService::get().waitShutdown(deadline);
        });
    }
    if (paimon::blur::BlurDiskCache::isInitialized()) {
        safeShutdownStep("blur-disk-cache-wait", [deadline]() {
            paimon::blur::BlurDiskCache::get().waitShutdown(deadline);
        });
    }
    if (paimon::emotes::EmoteCache::isInitialized()) {
        safeShutdownStep("emote-cache-wait", [deadline]() {
            paimon::emotes::EmoteCache::get().waitShutdown(deadline);
        });
    }
    safeShutdownStep("animated-gif-wait", [deadline]() {
        AnimatedGIFSprite::waitShutdownWorker(deadline);
    });
    safeShutdownStep("video-thumbnail-wait", [deadline]() {
        VideoThumbnailSprite::waitShutdown(deadline);
    });
    safeShutdownStep("live-wallpapers-wait", [deadline]() {
        paimon::wallpaper::LiveWallpaperNode::finishShutdown(deadline);
    });
    if (paimon::twitch::StreamOverlayServer::isInitialized()) {
        safeShutdownStep("stream-overlay-wait", [deadline]() {
            paimon::twitch::StreamOverlayServer::get().waitShutdown(deadline);
        });
    }
    if (paimon::editormcp::EditorMcpServer::isInitialized()) {
        safeShutdownStep("editor-mcp-wait", [deadline]() {
            paimon::editormcp::EditorMcpServer::get().waitShutdown(deadline);
        });
    }
    if (LocalThumbs::isInitialized()) {
        safeShutdownStep("local-thumbs-wait", [deadline]() {
            LocalThumbs::get().waitShutdown(deadline);
        });
    }
    // join the media worker before statics die so it can't touch the cache in atexit.
    safeShutdownStep("transition-media-wait", [deadline]() {
        paimon::transitions::finishShutdownTransitionMedia(deadline);
    });

    // ── Phase 4: Release GL/FMOD/Video/D3D11 resources & exit disk cleanup ──
    // release shared videos before mf dies; the static destructor crashes in msmpeg2vdec.dll.
    if (LayerBackgroundManager::isInitialized()) {
        safeShutdownStep("layer-bg-release-videos", []() {
            LayerBackgroundManager::get().releaseAllSharedVideos();
        });
    }
    safeShutdownStep("dynamic-song-kill", []() {
        DynamicSongManager::get()->forceKill();
    });
    // before the fmod engine goes away: releases the dive filter's dsps.
    safeShutdownStep("dynamic-song-submerge-shutdown", []() {
        paimon::dynsong::SubmergeEffect::get().shutdown();
    });
    safeShutdownStep("beat-shader-shutdown", []() {
        paimon::beat_shaders::BeatShaderManager::get().shutdown();
    });
    safeShutdownStep("menu-music-effects-shutdown", []() {
        paimon::menumusic::MenuMusicEffects::get().shutdown();
    });
    safeShutdownStep("editor-music-shutdown", []() {
        paimon::editormusic::EditorMusicPlayer::get().shutdown();
    });
    safeShutdownStep("dynamic-volume-shutdown", []() {
        paimon::dynvol::DynamicVolumeManager::get().shutdown();
    });
    safeShutdownStep("profile-music-stop", []() {
        ProfileMusicManager::get().forceStop();
    });
    safeShutdownStep("song-cover-cache-cleanup", []() {
        paimon::menumusic::SongCoverCache::get().cleanup();
    });
    safeShutdownStep("pet-release", []() {
        PetManager::get().releaseSharedResources();
    });
#if defined(GEODE_IS_DESKTOP)
    safeShutdownStep("cursor-release", []() {
        CursorManager::get().releaseSharedResources();
    });
#endif
    if (paimon::buttonskin::ButtonSkinRuntime::isInitialized()) {
        safeShutdownStep("button-skin-shutdown", []() {
            paimon::buttonskin::ButtonSkinRuntime::get().shutdown();
        });
    }

    safeShutdownStep("clear-resource-caches", []() {
        if (paimon::chatimg::ChatImageService::isInitialized()) {
            paimon::chatimg::ChatImageService::get().clearTextureCache();
        }
        shutdownProfileImgCache();
        paimon::icon_gradients::shutdownGradientImageCache();
        paimon::icon_maker::IconThumbs::get().clear();
        ProfileThumbs::get().clearAllCache();
        ProfileThumbs::get().clearNoProfileCache();
        ProfileThumbs::get().clearPendingDownloads();
        if (paimon::emotes::EmoteCache::isInitialized()) {
            paimon::emotes::EmoteCache::get().clearRam();
        }
        AnimatedGIFSprite::clearCacheForReload();
        VideoThumbnailSprite::clearCache();
        paimon::menumusic::VinylDisc::clearTextureCache();
        paimon::image::clearBudgeted();
        paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = 0;
        paimon::ThumbnailBackgroundChangedEvent::setLastTexture(nullptr);
    });

    safeShutdownStep("blur-system-destroy", []() {
        BlurSystem::getInstance()->destroy();
    });
    safeShutdownStep("gif-compute-shutdown", []() {
        paimon::gifimport::shutdownComputeDevice();
    });
    safeShutdownStep("transition-watchdog-disarm", []() {
        paimon::transitions::shutdownLevelTransitionWatchdog();
    });
    safeShutdownStep("dynamic-transition-history-clear", []() {
        paimon::transitions::dynamic::clearHistory();
    });

    if (clearCacheOnExit) {
        safeShutdownStep("disk-cache-cleanup", []() {
            cleanupDiskCache("exit");
        });

        safeShutdownStep("disk-index-clear", []() {
#ifdef PAIM_USE_GEODE
            Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
#endif
        });

        safeShutdownStep("server-cache-remove", []() {
            auto saveDir = paimon::platform::activeBackend().getSaveDir();
            removePathIfExists(saveDir / "manifest_cache.json", "manifest cache");
            removePathIfExists(saveDir / "profile_music", "profile music cache");
            removePathIfExists(saveDir / "profileimg_cache", "profile image cache");
        });
    } else {
        log::info("[PaimonThumbnails] Disk cache cleanup disabled by setting");
    }

    auto const totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - exitStart
    ).count();
    log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ({}ms) ===", totalMs);
}
