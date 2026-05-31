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
#include "../features/custom-slider/services/CustomSliderManager.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "RuntimeLifecycle.hpp"
#include "StartupIncompatibilityCheck.hpp"
#include "QualityConfig.hpp"
#include "MainLevels.hpp"
#include "Settings.hpp"
#include "../features/paidraw/PaiDrawManager.hpp"
#include "../video/VideoNormalizer.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/GLSLLoader.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/BlurDiskCache.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
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

// atomic: MenuLayer::init puede re-entrar si la escena se recarga
std::atomic<bool> g_languageListenerRegistered{false};

template <typename T>
void paimonOnSettingChanged(T const&) {
    paimon::settings::internal::g_settingsVersion.fetch_add(1, std::memory_order_relaxed);
}
}

void PaimonOnModLoaded() {
    log::info("[PaimonThumbnails][Init] Loaded event start");

    PaimonCheckStartupIncompatibilities();

    // â”€â”€ Framework: registra features, permisos y hooks â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    paimon::initFramework();

    // â”€â”€ PaiDraw â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    paidraw::PaiDrawManager::get().init();

    // ── Beat Shaders ───────────────────────────────────────────────
    // Initializes the audio FFT pipeline lazily — only attaches the FMOD DSP
    // when the feature is enabled.
    paimon::beat_shaders::BeatShaderManager::get().init();

    // â”€â”€ Blur disk cache â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Inicializa cache persistente de texturas blur pre-calculadas. El init
    // es async (lee el indice en I/O pool) — no bloquea el main thread.
    // En la segunda entrada al juego con cache poblado, los callbacks de
    // requestLoad no necesitan correr blur GPU; levantan la textura blur
    // desde disco directamente.
    paimon::blur::BlurDiskCache::get().init();

    // â”€â”€ Startup cache cleanup safety net â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Si clear-cache-on-exit esta activo y el juego crasheo en la sesion anterior
    // sin pasar por $on_game(Exiting), los caches de disco podrian haber quedado.
    // Se ejecuta en background para NO bloquear el main thread durante el arranque
    // (la limpieza recursiva del arbol de cache puede tardar cientos de ms).
    bool const clearCacheAtStartup = paimon::settings::general::clearCacheOnExit();

    // ── Cleanup: remove orphaned video cache files (>7 days) ────
    paimon::video::VideoNormalizer::cleanupOrphanedCache();

    // ── Paralelizar migraciones, limpiezas y cargas de configuración ──
    // Estas operaciones son independientes y pueden ejecutarse en paralelo
    paimon::ThreadTracker::get().spawn([clearCacheAtStartup]() {
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

    paimon::ThreadTracker::get().spawn([]() {
        geode::utils::thread::setName("PaimonConfigLoad");
        if (paimon::isRuntimeShuttingDown()) return;
        TransitionManager::get().loadConfig();
        ProgressBarManager::get().loadConfig();
        paimon::slider::CustomSliderManager::get().loadConfig();
    });

    log::info("[PaimonThumbnails] Queueing main level thumbnails...");

    // Batch fetch manifest for main levels first, then prefetch
    std::vector<int> mainLevels;
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    // Estilo globed2 (PreloadManager): los assets principales se cargan lo
    // antes posible. Si LoadingLayer ya disparo el prefetch (caso normal),
    // tryClaimMainLevelsPrefetch() devolvera false y aqui solo logueamos.
    // En arranques sin LoadingLayer (reload de texturas, etc.) este es el
    // fallback que asegura que las miniaturas main se carguen.
    if (paimon::tryClaimMainLevelsPrefetch()) {
        // Manifest fetch inmediato — no bloquea nada.
        HttpClient::get().fetchManifest(mainLevels, [](bool success) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info("[PaimonThumbnails] (Bootstrap) Manifest fetch {}",
                success ? "succeeded" : "failed (will use Worker fallback)");
        });

        // Encolar todos en el siguiente frame para no contender con el
        // primer render del menu.
        paimon::scheduleMainThreadDelay(0.0f, []() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto& loader = ThumbnailLoader::get();
            for (int levelID = paimon::kMainLevelMinID;
                 levelID <= paimon::kMainLevelMaxID; ++levelID) {
                loader.requestLoad(
                    levelID, fmt::format("{}.png", levelID), nullptr,
                    ThumbnailLoader::PriorityBootstrap);
            }
            log::info("[PaimonThumbnails] (Bootstrap) {} main level thumbnails enqueued",
                paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1);
        });
    } else {
        log::info("[PaimonThumbnails] Main level prefetch already kicked off by LoadingLayer");
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

        // â”€â”€ Custom Cursor settings sync â”€â”€
        // Sync mod.json settings -> CursorManager config
        // Use a guard to prevent infinite re-entry when saveConfig syncs back
        // Usamos atomic<bool> para seguridad thread-safe (listenForSettingChanges
        // puede ser llamado desde cualquier thread en Geode).
        static std::atomic<bool> s_cursorSyncGuard{false};
        geode::listenForSettingChanges<bool>("custom-cursor-enable", +[](bool value) {
            if (s_cursorSyncGuard.exchange(true, std::memory_order_acq_rel)) return;
            CursorManager::get().config().enabled = value;
            CursorManager::get().applyConfigLive();
            s_cursorSyncGuard.store(false, std::memory_order_release);
        });
        // custom-cursor-scale / custom-cursor-trail are saved values (not mod.json
        // settings) configured from the cursor config popup, so listenForSettingChanges
        // would never fire for them; the popup applies them live directly.

        // â”€â”€ Thumbnail / Background settings reactivity â”€â”€
        // Increment global version so LevelCell & LevelInfoLayer re-cache settings
        // Only keys registered in mod.json fire listenForSettingChanges. The
        // granular keys (levelcell-background-*, transparent-list-mode, anim-*,
        // gallery-autocycle, levelinfo-effect-intensity/extra-styles/bg-darkness)
        // are saved values configured from the in-mod settings panel.
        geode::listenForSettingChanges<bool>("levelcell-hover-effects", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<bool>("compact-list-mode", &paimonOnSettingChanged<bool>);
        geode::listenForSettingChanges<double>("level-thumb-width", &paimonOnSettingChanged<double>);
        geode::listenForSettingChanges<std::string>("levelinfo-background-style", &paimonOnSettingChanged<std::string>);
    }

    log::info("[PaimonThumbnails][Init] Applying startup init");

    log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
    // hilo de I/O de disco + procesamiento CPU â€” no migrable a WebTask (no es peticion web).
    // el delay y la extraccion se ejecutan en background para no bloquear el main thread.
    paimon::scheduleMainThreadDelay(0.5f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::ThreadTracker::get().spawn([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            if (paimon::isRuntimeShuttingDown()) return;
            LevelColors::get().extractColorsFromCache();
            if (paimon::isRuntimeShuttingDown()) return;
            geode::Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                log::info("[PaimonThumbnails][Init] Color extraction finished");
            });
        });
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");

    // â”€â”€ Lazy initialization: cargar servicios no crÃ­ticos de forma diferida â”€â”€
    // Los emotes y shaders solo se cargan cuando el usuario los necesita,
    // reduciendo el tiempo de carga inicial del mod.

    // Emote catalog: carga diferida con delay mÃ¡s largo para no competir con thumbnails
    paimon::scheduleMainThreadDelay(12.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        
        // Cargar catÃ¡logo desde disco primero (rÃ¡pido)
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

    // Shader pre-warm: cargar despuÃ©s de que los thumbnails estÃ©n listos
    paimon::scheduleMainThreadDelay(10.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        Shaders::prewarmLevelInfoShaders();

        // Precompila los shaders de fondo dinamicos que el usuario tiene
        // configurados (rain, matrix, crt, ...). Asi la primera entrada a la
        // capa con ese fondo no paga el compile (4-10ms) como micro-stutter.
        Shaders::prewarmConfiguredBackgroundShaders();

        // Fase 0 de migracion a .glsl: verifica que los archivos .glsl
        // instalados en resources/shaders se pueden leer correctamente.
        // Si falla, el log indica la ruta esperada para debug. Las fases
        // 1-4 iran migrando cada shader inline a su equivalente .glsl.
        paimon::shaders::preloadBlurShaders();
    });

    // â”€â”€ UpdateChecker: consulta GitHub Releases para detectar nuevas versiones.
    // Se hace con un pequeÃ±o delay para no competir con la carga inicial.
    paimon::scheduleMainThreadDelay(8.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::updates::UpdateChecker::get().checkAsync();
    });

    // ── Precarga de layouts de botones ──────────────────────────────
    // ButtonLayoutManager::load() recorre directorios y parsea archivos .txt
    // por escena. Antes corria sincrono dentro de LevelInfoLayer::init() la
    // primera vez → micro-freeze al abrir el primer nivel. Lo hacemos durante
    // el idle del menu (main thread, fuera del camino de entrada). El guard
    // m_loaded interno hace que la llamada posterior en init() sea no-op.
    paimon::scheduleMainThreadDelay(6.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        ButtonLayoutManager::get().load();
    });
}
