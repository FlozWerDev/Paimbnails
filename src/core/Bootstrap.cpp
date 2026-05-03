// Inicializacion diferida del mod desde MenuLayer::init().

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
#include "../features/updates/services/UpdateChecker.hpp"
#include "RuntimeLifecycle.hpp"
#include "QualityConfig.hpp"
#include "Settings.hpp"
#include "../video/VideoNormalizer.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
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

// atomic: MenuLayer::init puede re-entrar si la escena se recarga
std::atomic<bool> g_languageListenerRegistered{false};

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
    // Se ejecuta en background para NO bloquear el main thread durante el arranque
    // (la limpieza recursiva del arbol de cache puede tardar cientos de ms).
    bool const clearCacheAtStartup = paimon::settings::general::clearCacheOnExit();

    // ── Cleanup: remove orphaned video cache files (>7 days) ────────
    paimon::video::VideoNormalizer::cleanupOrphanedCache();

    // ── Paralelizar migraciones, limpiezas y cargas de configuración ──
    // Estas operaciones son independientes y pueden ejecutarse en paralelo
    std::thread migrationThread([clearCacheAtStartup]() {
        geode::utils::thread::setName("PaimonMigrations");
        if (paimon::isRuntimeShuttingDown()) return;

        // Limpieza diferida del cache (antes corria en el main thread)
        if (clearCacheAtStartup) {
            cleanupDiskCache("startup-safety");
            auto saveDir = Mod::get()->getSaveDir();
            std::error_code ec;
            std::filesystem::remove(saveDir / "manifest_cache.json", ec);
            // setSavedValue toca estructuras de Geode: lo movemos al main thread.
            geode::Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
            });
        }

        if (paimon::isRuntimeShuttingDown()) return;
        LayerBackgroundManager::get().migrateFromLegacy();
        LayerBackgroundManager::get().migrateToGlobalMusic();
        LayerBackgroundManager::get().migrateExternalAssetsToManagedStorage();
    });
    migrationThread.detach();

    std::thread configThread([]() {
        geode::utils::thread::setName("PaimonConfigLoad");
        if (paimon::isRuntimeShuttingDown()) return;
        TransitionManager::get().loadConfig();
        ProgressBarManager::get().loadConfig();
    });
    configThread.detach();

    log::info("[PaimonThumbnails] Queueing main level thumbnails...");

    // Batch fetch manifest for main levels first, then prefetch
    std::vector<int> mainLevels;
    for (int i = 1; i <= 22; i++) mainLevels.push_back(i);

    // Optimización: iniciar manifest fetch antes y con prefetch paralelo
    paimon::scheduleMainThreadDelay(5.0f, [mainLevels = std::move(mainLevels)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        
        // Iniciar manifest fetch inmediatamente
        HttpClient::get().fetchManifest(mainLevels, [](bool success) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info("[PaimonThumbnails] Manifest fetch {}, starting parallel prefetch", success ? "succeeded" : "failed (will use Worker fallback)");
        });

        // Prefetch paralelo en batches espaciados para no saturar la red.
        // IMPORTANTE: este callback corre en el main thread (cocos2d scheduler),
        // por eso NO usamos std::this_thread::sleep_for entre batches —
        // bloquearia el render del menu. En su lugar, encadenamos batches
        // mediante scheduleMainThreadDelay para mantener el frame pacing fluido.
        constexpr int PARALLEL_PREFETCH_BATCH = 2;
        constexpr float BATCH_INTERVAL_SEC = 0.20f;
        for (int batchStart = 1, batchIndex = 0; batchStart <= 22;
             batchStart += PARALLEL_PREFETCH_BATCH, ++batchIndex) {
            int batchEnd = std::min(batchStart + PARALLEL_PREFETCH_BATCH, 23);
            float batchDelay = BATCH_INTERVAL_SEC * static_cast<float>(batchIndex);
            paimon::scheduleMainThreadDelay(batchDelay, [batchStart, batchEnd]() {
                if (paimon::isRuntimeShuttingDown()) return;
                for (int levelID = batchStart; levelID < batchEnd; ++levelID) {
                    ThumbnailLoader::get().requestLoad(
                        levelID, fmt::format("{}.png", levelID), nullptr,
                        ThumbnailLoader::PriorityBootstrap);
                }
            });
        }

        log::info("[PaimonThumbnails] Startup blur prewarm skipped; blur will build on demand");
    });

    std::string langStr = paimon::settings::general::language();
    log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
    applyLanguageSetting(langStr);
    bool expected = false;
    if (g_languageListenerRegistered.compare_exchange_strong(expected, true)) {
        geode::listenForSettingChanges<std::string>("language", +[](std::string value) {
            applyLanguageSetting(value);
            log::info("[PaimonThumbnails][Language] Changed to '{}'", value);
        });

        // ── Custom Cursor settings sync ──
        // Sync mod.json settings -> CursorManager config
        // Use a guard to prevent infinite re-entry when saveConfig syncs back
        static bool s_cursorSyncGuard = false;
        geode::listenForSettingChanges<bool>("custom-cursor-enable", +[](bool value) {
            if (s_cursorSyncGuard) return;
            s_cursorSyncGuard = true;
            CursorManager::get().config().enabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard = false;
        });
        geode::listenForSettingChanges<double>("custom-cursor-scale", +[](double value) {
            if (s_cursorSyncGuard) return;
            s_cursorSyncGuard = true;
            CursorManager::get().config().scale = static_cast<float>(value);
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard = false;
        });
        geode::listenForSettingChanges<bool>("custom-cursor-trail", +[](bool value) {
            if (s_cursorSyncGuard) return;
            s_cursorSyncGuard = true;
            CursorManager::get().config().trailEnabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard = false;
        });

        // ── Thumbnail / Background settings reactivity ──
        // Increment global version so LevelCell & LevelInfoLayer re-cache settings
        geode::listenForSettingChanges<std::string>("levelcell-background-type", &paimonOnSettingChanged<std::string>);
        geode::listenForSettingChanges<bool>("levelcell-hover-effects", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<bool>("compact-list-mode", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<bool>("transparent-list-mode", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<double>("level-thumb-width", &paimonOnSettingChanged<double>);
        geode::listenForSettingChanges<double>("levelcell-background-blur", &paimonOnSettingChanged<double>);
        geode::listenForSettingChanges<double>("levelcell-background-darkness", &paimonOnSettingChanged<double>);
        geode::listenForSettingChanges<std::string>("levelcell-anim-type", &paimonOnSettingChanged<std::string>);
        geode::listenForSettingChanges<bool>("levelcell-gallery-autocycle", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<std::string>("levelinfo-background-style", &paimonOnSettingChanged<std::string>);
        geode::listenForSettingChanges<int64_t>("levelinfo-effect-intensity", &paimonOnSettingChanged<int64_t>);
        geode::listenForSettingChanges<std::string>("levelinfo-extra-styles", &paimonOnSettingChanged<std::string>);
        geode::listenForSettingChanges<int64_t>("levelinfo-bg-darkness", &paimonOnSettingChanged<int64_t>);
    }

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

    // ── Lazy initialization: cargar servicios no críticos de forma diferida ──
    // Los emotes y shaders solo se cargan cuando el usuario los necesita,
    // reduciendo el tiempo de carga inicial del mod.

    // Emote catalog: carga diferida con delay más largo para no competir con thumbnails
    paimon::scheduleMainThreadDelay(12.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        
        // Cargar catálogo desde disco primero (rápido)
        paimon::emotes::EmoteService::get().loadCatalogFromDisk();
        
        auto& svc = paimon::emotes::EmoteService::get();
        log::info("[PaimonEmotes] Catalog loaded: {} emotes ({} GIFs, {} stickers)",
                  svc.getAllEmotes().size(), svc.getGifEmotes().size(), svc.getStaticEmotes().size());

        // Fetch en background (incremental si ya hay datos cacheados)
        paimon::emotes::EmoteService::get().fetchAllEmotes([](bool success) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info("[PaimonEmotes] Catalog fetch {}", success ? "succeeded" : "failed (using cached)");
            
            log::info("[PaimonEmotes] Emote disk preload skipped at startup; assets load on demand");
        });
    });

    // Shader pre-warm: cargar después de que los thumbnails estén listos
    paimon::scheduleMainThreadDelay(10.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        Shaders::prewarmLevelInfoShaders();
    });

    // ── UpdateChecker: consulta GitHub Releases para detectar nuevas versiones.
    // Se hace con un pequeño delay para no competir con la carga inicial.
    paimon::scheduleMainThreadDelay(8.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::updates::UpdateChecker::get().checkAsync();
    });
}
