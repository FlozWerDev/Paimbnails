// Inicializacion del mod — llamado desde $on_mod(Loaded).

#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>
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
#include "RuntimeLifecycle.hpp"
#include "QualityConfig.hpp"
#include "Settings.hpp"
#include "../video/VideoNormalizer.hpp"
#include "../utils/Shaders.hpp"
#include <thread>
#include <filesystem>
#include <functional>
#include <memory>

namespace paimon { void initFramework(); }

using namespace geode::prelude;

namespace {
void applyLanguageSetting(std::string const& langStr) {
    if (langStr == "english") {
        Localization::get().setLanguage(Localization::Language::ENGLISH);
    } else {
        Localization::get().setLanguage(Localization::Language::SPANISH);
    }
}

template <typename T>
void paimonOnSettingChanged(T const&) {
    paimon::settings::internal::g_settingsVersion.fetch_add(1, std::memory_order_relaxed);
}
}

void PaimonOnModLoaded() {
    log::info("[PaimonThumbnails][Init] Loaded event start");

    // ── Framework: registra features, permisos y hooks ──────────────
    paimon::initFramework();

    // ── Startup cache cleanup safety net ──────────────────────────────
    // Si clear-cache-on-exit esta activo y el juego crasheo en la sesion anterior
    // sin pasar por $on_game(Exiting), los caches de disco podrian haber quedado.
    // Limpiamos ahora para garantizar que no se acumule almacenamiento.
    if (paimon::settings::general::clearCacheOnExit()) {
        cleanupDiskCache("startup-safety");
        auto saveDir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::remove(saveDir / "manifest_cache.json", ec);
        Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
    }

    // ── Cleanup: remove orphaned video cache files (>7 days) ────────
    paimon::video::VideoNormalizer::cleanupOrphanedCache();

    // migra los settings legacy al formato unificado per-layer
    LayerBackgroundManager::get().migrateFromLegacy();
    // migra la musica per-layer a global (solo corre una vez)
    LayerBackgroundManager::get().migrateToGlobalMusic();

    // carga la configuracion de transiciones personalizables
    TransitionManager::get().loadConfig();

    // carga la configuracion de la barra de progreso personalizada
    ProgressBarManager::get().loadConfig();

    log::info("[PaimonThumbnails] Queueing main level thumbnails...");

    // Batch fetch manifest for main levels first, then prefetch
    std::vector<int> mainLevels;
    for (int i = 1; i <= 22; i++) mainLevels.push_back(i);

    paimon::scheduleMainThreadDelay(0.75f, [mainLevels = std::move(mainLevels)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        HttpClient::get().fetchManifest(mainLevels, [](bool success) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info("[PaimonThumbnails] Manifest fetch {}, starting sequential prefetch", success ? "succeeded" : "failed (will use Worker fallback)");

            auto prefetchNext = std::make_shared<std::function<void(int)>>();
            *prefetchNext = [prefetchNext](int levelID) {
                if (levelID > 22 || paimon::isRuntimeShuttingDown()) return;
                ThumbnailLoader::get().requestLoad(levelID, fmt::format("{}.png", levelID), [prefetchNext, levelID](cocos2d::CCTexture2D*, bool) {
                    (*prefetchNext)(levelID + 1);
                }, ThumbnailLoader::PriorityBootstrap);
            };
            (*prefetchNext)(1);
        });
    });

    std::string langStr = paimon::settings::general::language();
    log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
    applyLanguageSetting(langStr);

    // ── Registrar listeners de settings (storage static garantiza lifetime) ──
    // Idioma
    static auto s_langListener = geode::listenForSettingChanges<std::string>(
        "language", [](std::string value) {
            applyLanguageSetting(value);
            log::info("[PaimonThumbnails][Language] Changed to '{}'", value);
        });

    // ── Custom Cursor settings sync ──
    // Sync mod.json settings -> CursorManager config
    // Guard para evitar re-entrada si applyConfigLive sincroniza de vuelta
    static bool s_cursorSyncGuard = false;
    static auto s_cursorEnableListener = geode::listenForSettingChanges<bool>(
        "custom-cursor-enable", [](bool value) {
            if (s_cursorSyncGuard) return;
            s_cursorSyncGuard = true;
            CursorManager::get().config().enabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard = false;
        });
    static auto s_cursorScaleListener = geode::listenForSettingChanges<double>(
        "custom-cursor-scale", [](double value) {
            if (s_cursorSyncGuard) return;
            s_cursorSyncGuard = true;
            CursorManager::get().config().scale = static_cast<float>(value);
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard = false;
        });
    static auto s_cursorTrailListener = geode::listenForSettingChanges<bool>(
        "custom-cursor-trail", [](bool value) {
            if (s_cursorSyncGuard) return;
            s_cursorSyncGuard = true;
            CursorManager::get().config().trailEnabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard = false;
        });

    // ── Thumbnail / Background settings reactivity ──
    // Incrementa version global para que LevelCell y LevelInfoLayer re-cacheen settings
    static auto s_bgTypeListener    = geode::listenForSettingChanges<std::string>("levelcell-background-type",   &paimonOnSettingChanged<std::string>);
    static auto s_hoverListener     = geode::listenForSettingChanges<bool>       ("levelcell-hover-effects",     &paimonOnSettingChanged<bool>);
    static auto s_compactListener   = geode::listenForSettingChanges<bool>       ("compact-list-mode",           &paimonOnSettingChanged<bool>);
    static auto s_transpListener    = geode::listenForSettingChanges<bool>       ("transparent-list-mode",       &paimonOnSettingChanged<bool>);
    static auto s_thumbWListener    = geode::listenForSettingChanges<double>     ("level-thumb-width",           &paimonOnSettingChanged<double>);
    static auto s_bgBlurListener    = geode::listenForSettingChanges<double>     ("levelcell-background-blur",   &paimonOnSettingChanged<double>);
    static auto s_bgDarkListener    = geode::listenForSettingChanges<double>     ("levelcell-background-darkness",&paimonOnSettingChanged<double>);
    static auto s_animTypeListener  = geode::listenForSettingChanges<std::string>("levelcell-anim-type",         &paimonOnSettingChanged<std::string>);
    static auto s_galleryListener   = geode::listenForSettingChanges<bool>       ("levelcell-gallery-autocycle", &paimonOnSettingChanged<bool>);
    static auto s_liBgListener      = geode::listenForSettingChanges<std::string>("levelinfo-background-style",  &paimonOnSettingChanged<std::string>);
    static auto s_liIntListener     = geode::listenForSettingChanges<int64_t>    ("levelinfo-effect-intensity",  &paimonOnSettingChanged<int64_t>);
    static auto s_liExtraListener   = geode::listenForSettingChanges<std::string>("levelinfo-extra-styles",      &paimonOnSettingChanged<std::string>);
    static auto s_liDarkListener    = geode::listenForSettingChanges<int64_t>    ("levelinfo-bg-darkness",       &paimonOnSettingChanged<int64_t>);

    log::info("[PaimonThumbnails][Init] Applying startup init");

    log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
    // hilo de I/O de disco + procesamiento CPU — no migrable a WebTask (no es peticion web).
    // el delay y la extraccion se ejecutan en background para no bloquear el main thread.
    paimon::scheduleMainThreadDelay(0.5f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        std::thread([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            if (paimon::isRuntimeShuttingDown()) return;
            LevelColors::get().extractColorsFromCache();
            geode::Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                log::info("[PaimonThumbnails][Init] Color extraction finished");
            });
        }).detach();
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");

    // ── Shader pre-warm: compila shaders de LevelInfoLayer durante idle del menu ──
    paimon::scheduleMainThreadDelay(1.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        Shaders::prewarmLevelInfoShaders();
    });

    // ── Emote catalog: load from disk + incremental fetch + preload confirmation ──
    paimon::emotes::EmoteService::get().loadCatalogFromDisk();

    auto startEmotePreload = []() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto& svc = paimon::emotes::EmoteService::get();
        log::info("[PaimonEmotes] Catalog ready: {} emotes ({} GIFs, {} stickers)",
                  svc.getAllEmotes().size(), svc.getGifEmotes().size(), svc.getStaticEmotes().size());

        paimon::scheduleMainThreadDelay(2.0f, []() {
            if (paimon::isRuntimeShuttingDown()) return;
            paimon::emotes::EmoteCache::get().preloadAllToDisk([](size_t downloaded, size_t skipped, size_t total) {
                log::info("[PaimonEmotes] Emote sync complete: {} new, {} cached, {} total",
                          downloaded, skipped, total);
            });
        });
    };

    // Always attempt fetch (incremental via timelast if catalog is cached, full otherwise)
    paimon::emotes::EmoteService::get().fetchAllEmotes([startEmotePreload](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (success) {
            log::info("[PaimonEmotes] Catalog fetch/update succeeded");
        } else {
            log::info("[PaimonEmotes] Catalog fetch failed, using cached data if available");
        }
        startEmotePreload();
    });
}

// $on_mod(Loaded) se ejecuta exactamente una vez cuando el mod se carga,
// antes del primer frame. CCDirector y el scheduler ya estan disponibles.
// Los callbacks con delay se ejecutaran una vez que una escena este activa.
$on_mod(Loaded) {
    PaimonOnModLoaded();
}
