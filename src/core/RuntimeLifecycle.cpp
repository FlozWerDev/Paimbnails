// RuntimeLifecycle.cpp — Manejo de ciclo de vida: arranque y cierre.
// - cleanupDiskCache(): limpieza selectiva del cache de disco
// - $on_game(Exiting): limpieza de RAM y disco al cerrar el juego

#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/string.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/HttpClient.hpp"
#include "../video/VideoNormalizer.hpp"
#include "RuntimeLifecycle.hpp"
#include "QualityConfig.hpp"
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
}

namespace paimon {

bool isRuntimeShuttingDown() {
    return s_runtimeShuttingDown.load(std::memory_order_acquire);
}

void markRuntimeShuttingDown() {
    s_runtimeShuttingDown.store(true, std::memory_order_release);
}

} // namespace paimon

// limpieza de cache de disco, la uso tanto al arrancar como al salir
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

    log::info("[PaimonThumbnails] Cleaning quality cache tree ({})...", context);
    log::info("[PaimonThumbnails] Preserving GIF background cache (cache/gifs/)");

    std::filesystem::directory_iterator it(cacheDir, ec);
    if (ec) {
        log::warn("[PaimonThumbnails] Failed to open cache dir for iteration: {}", ec.message());
        return;
    }

    for (auto const& entry : it) {
        if (entry.path().filename() == "gifs") {
            continue;
        }

        std::error_code removeEc;
        std::filesystem::remove_all(entry.path(), removeEc);
        if (removeEc) {
            log::warn("[PaimonThumbnails] Failed to remove {}: {}",
                geode::utils::string::pathToString(entry.path()), removeEc.message());
        } else {
            log::info("[PaimonThumbnails] Removed {}",
                geode::utils::string::pathToString(entry.path()));
        }
    }
}

// al cerrar el juego:
// - activamos flags para que los destructores estaticos no hagan release() sobre objetos Cocos2d
// - limpiamos caches de datos del servidor (perfiles, GIFs, musica, profileimg)
// - NO tocamos datos offline del usuario (fondos de menu, thumbnails locales, settings)
$on_game(Exiting) {
    paimon::markRuntimeShuttingDown();
    log::info("[SHUTDOWN] === BEGIN EXIT SEQUENCE ===");

    // Para Ti: guardar perfil de recomendaciones antes de limpiar
    paimon::foryou::ForYouTracker::get().save();
    log::info("[SHUTDOWN] 1/14 ForYouTracker saved");

    HttpClient::get().cleanTasks();
    log::info("[SHUTDOWN] 2/14 HttpClient tasks cleaned");

    // cancelar preload de emotes antes de continuar
    paimon::emotes::EmoteCache::get().cancelPreload();
    log::info("[SHUTDOWN] 3/14 EmoteCache preload cancelled");

    ProfileThumbs::s_shutdownMode.store(true, std::memory_order_release);

    // cancelar tareas pendientes de ThumbnailLoader ANTES de limpiar disco
    // para que los hilos de fondo no reescriban archivos que vamos a borrar
    log::info("[SHUTDOWN] 4/14 ThumbnailLoader cleanup starting...");
    ThumbnailLoader::get().cleanup();
    log::info("[SHUTDOWN] 4/14 ThumbnailLoader cleanup DONE");
    paimon::video::VideoNormalizer::shutdownAsyncWork();

    bool clearCacheOnExit = paimon::settings::general::clearCacheOnExit();

    // persist disk index before closing (synchronous, no more workers)
    // only if we're not going to delete it immediately after
    if (!clearCacheOnExit) {
        paimon::cache::ThumbnailCache::get().saveDiskIndex(true);
        // flush saved values to disk so the index survives across sessions
        (void)Mod::get()->saveData();
    }
    log::info("[SHUTDOWN] 5/14 Disk index persisted");

    log::info("[SHUTDOWN] 6/14 LocalThumbs shutdown starting...");
    LocalThumbs::get().shutdown();
    log::info("[SHUTDOWN] 6/14 LocalThumbs shutdown DONE");

    log::info("[SHUTDOWN] 7/14 ProfileThumbs shutdown starting...");
    ProfileThumbs::get().shutdown();
    log::info("[SHUTDOWN] 7/14 ProfileThumbs shutdown DONE");

    // forzar escritura de colores pendientes antes del cierre
    // (siempre flush: thumbnails/ ya no se borra en el cleanup)
    LevelColors::get().flushIfDirty();
    log::info("[SHUTDOWN] 8/14 LevelColors flushed");

    // === RAM cleanup (siempre, para evitar crashes con destructores estaticos) ===

    // 1. cache de perfiles de otros usuarios (thumbnails + GIFs en memoria)
    ProfileThumbs::get().clearAllCache();
    ProfileThumbs::get().clearNoProfileCache();

    // 1b. limpiar callbacks pendientes que capturan Ref<GJScoreCell> etc.
    //     si no se limpian, el destructor estatico de ProfileThumbs los destruiria
    //     despues de que CCPoolManager ya murio → crash
    ProfileThumbs::get().clearPendingDownloads();
    log::info("[SHUTDOWN] 9/14 ProfileThumbs caches cleared");

    // 2. cache global de GIFs animados en RAM
    log::info("[SHUTDOWN] 10/14 AnimatedGIFSprite clearCache starting...");
    AnimatedGIFSprite::clearCache();
    log::info("[SHUTDOWN] 10/14 AnimatedGIFSprite clearCache DONE");

    // 2b. cache global de videos en disco (temp files)
    log::info("[SHUTDOWN] 11/14 VideoThumbnailSprite clearCache starting...");
    VideoThumbnailSprite::clearCache();
    log::info("[SHUTDOWN] 11/14 VideoThumbnailSprite clearCache DONE");

    // 2c. cache de emotes en RAM (Ref<CCTexture2D> + gifData)
    //     limpiamos solo RAM — el disco se mantiene como cache persistente
    paimon::emotes::EmoteCache::get().clearRam();
    log::info("[SHUTDOWN] 12/14 EmoteCache RAM cleared");

    // 3. detener audio dinamico/perfil de forma forzada (evita estados intermedios
    // de fades/transiciones durante shutdown)
    DynamicSongManager::get()->forceKill();
    ProfileMusicManager::get().forceStop();
    PetManager::get().releaseSharedResources();
    CursorManager::get().releaseSharedResources();
    log::info("[SHUTDOWN] 13/14 Audio + resources released");

    // 3b. release shared video players before MF shuts down.
    // Without this, the LayerBackgroundManager static singleton destructor
    // runs during atexit after MF is already torn down, causing
    // WindowsDecoder::close() to crash in msmpeg2vdec.dll.
    log::info("[SHUTDOWN] 14/14 releaseAllSharedVideos starting...");
    LayerBackgroundManager::get().releaseAllSharedVideos();
    log::info("[SHUTDOWN] 14/14 releaseAllSharedVideos DONE");

    // 3c. release BlurSystem FBOs and textures (must be on GL thread)
    BlurSystem::getInstance()->destroy();

    // === Disk cleanup (solo si clear-cache-on-exit esta activado) ===

    bool clearCache = clearCacheOnExit;
    if (!clearCache) {
        log::info("[PaimonThumbnails] Disk cache cleanup disabled by setting");
        return;
    }

    // 4. cache regenerable quality-aware (thumbnails, GIFs, profiles, manifests derivados)
    cleanupDiskCache("exit");

    // clear disk index saved value so it doesn't reference deleted files on next launch
    Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());

    // 5. caches regenerables del servidor (profile music, profile images, manifest CDN)
    auto saveDir = Mod::get()->getSaveDir();
    removePathIfExists(saveDir / "manifest_cache.json", "manifest cache");
    removePathIfExists(saveDir / "profile_music", "profile music cache");
    removePathIfExists(saveDir / "profileimg_cache", "profile image cache");
    // NO tocamos thumbnails/, saved_thumbnails/ ni downloaded_thumbnails/
    // porque son datos locales del usuario que no se pueden regenerar

    log::info("[PaimonThumbnails] All caches cleaned on exit");
    log::info("[SHUTDOWN] === EXIT SEQUENCE COMPLETE ===");
}
