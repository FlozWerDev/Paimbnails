// LoadingLayer.cpp — Hook del LoadingLayer vanilla.
//
// Aquí hacemos dos cosas:
//   1. Preload del set core de assets de Paimbnails (igual que Globed):
//      las miniaturas de los 22 main levels y los emotes del catálogo.
//   2. Mostrar el progreso del preload en pantalla con un label estilo
//      "Paimbnails: 12/74" — exactamente la UX de Globed pero adaptada
//      a nuestros assets de red (no bloquea el LoadingLayer; las descargas
//      siguen en background si el usuario llega al menú antes de que
//      terminen).
//
// Diferencia clave con Globed: Globed bloquea el step 14 con
// PreloadManager::loadNextBatch porque sus assets son locales (sprite-
// sheets) y se procesan en milisegundos. Nuestros assets son red, así
// que arrancamos las descargas en paralelo y solo refrescamos un label.

#include <Geode/modify/LoadingLayer.hpp>

#include <fmt/format.h>

#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../utils/HttpClient.hpp"
#include "../core/MainLevels.hpp"
#include "../core/PreloadProgress.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace {

// Período de refresco del label X/Y en segundos. 0.1s = 10Hz, suficiente para
// que el usuario perciba el avance sin spamear setString cada frame.
constexpr float kProgressUpdateInterval = 0.1f;

// Encola la fase 1 del preload: thumbnails de los main levels (1-22).
// Llamado SOLO una vez por sesión (ver tryClaimPreload).
void schedulePrefetchMainLevels() {
    using namespace paimon::preload;

    // También reclamamos el flag legado de Bootstrap para que su fallback
    // (paimon::scheduleMainThreadDelay → ThumbnailLoader::requestLoad sin
    // callback) NO vuelva a encolar las mismas 22 tasks. ThumbnailLoader es
    // idempotente, pero ahorrarse 22 requestLoad redundantes mejora el
    // arranque y evita logs duplicados.
    (void)paimon::tryClaimMainLevelsPrefetch();

    std::vector<int> mainLevels;
    mainLevels.reserve(paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1);
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    // Publicamos el total ANTES de encolar las descargas para que el label
    // muestre "0/22" desde el primer tick en lugar de empezar en "0/0".
    g_thumbsTotal.store(static_cast<int>(mainLevels.size()), std::memory_order_release);
    g_thumbsLoaded.store(0, std::memory_order_release);

    // Manifest batched para resolver las URLs CDN de las 22 miniaturas en
    // una sola request. fire-and-forget: si falla, ThumbnailLoader cae al
    // Worker fallback.
    HttpClient::get().fetchManifest(mainLevels, [](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        log::info(
            "[Paimbnails Preload] Main level manifest fetch {}",
            success ? "succeeded" : "failed (using Worker fallback)"
        );
    });

    auto& loader = ThumbnailLoader::get();
    for (int levelID : mainLevels) {
        loader.requestLoad(
            levelID,
            fmt::format("{}.png", levelID),
            // Callback: se invoca en main thread tanto para hits de RAM como
            // para descargas exitosas, fallos y 404s. Cualquier "completion"
            // vale como progreso del preload.
            [](cocos2d::CCTexture2D*, bool /*success*/) {
                paimon::preload::g_thumbsLoaded.fetch_add(1, std::memory_order_acq_rel);
            },
            ThumbnailLoader::PriorityBootstrap
        );
    }

    log::info(
        "[Paimbnails Preload] Queued {} main level thumbnails for preload",
        mainLevels.size()
    );
}

// Arranca el preload de emotes una vez que el catálogo está disponible.
// Se llama desde dos puntos: si el catálogo ya estaba en disco (caso normal
// tras la primera ejecución del mod) o tras un fetch incremental al server.
void startEmotePreloadIfReady() {
    using namespace paimon::preload;
    using paimon::emotes::EmoteService;
    using paimon::emotes::EmoteCache;

    auto emotes = EmoteService::get().getAllEmotes();
    g_emotesTotal.store(static_cast<int>(emotes.size()), std::memory_order_release);
    g_emotesLoaded.store(0, std::memory_order_release);
    g_emotesCatalogReady.store(true, std::memory_order_release);

    if (emotes.empty()) {
        log::info(
            "[Paimbnails Preload] Emote catalog vacío — preload omitido"
        );
        return;
    }

    log::info(
        "[Paimbnails Preload] Iniciando preload de {} emotes",
        emotes.size()
    );

    EmoteCache::get().preloadAllToDisk(
        // Callback final: solo loggea el resumen.
        [](size_t downloaded, size_t skipped, size_t total) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info(
                "[Paimbnails Preload] Emote preload terminó: {} descargados, {} ya en cache, {} totales",
                downloaded, skipped, total
            );
        },
        // Callback per-step: actualiza el contador global. Llega en main
        // thread (EmoteCache lo despacha vía queueInMainThread).
        [](size_t completed, size_t total) {
            paimon::preload::g_emotesLoaded.store(
                static_cast<int>(completed),
                std::memory_order_release
            );
            // Total ya lo escribimos antes; lo refrescamos por si el catálogo
            // creció con un fetch incremental durante el preload.
            paimon::preload::g_emotesTotal.store(
                static_cast<int>(total),
                std::memory_order_release
            );
        }
    );
}

// Encola la fase 2: emotes. Si no hay catálogo aún, intenta cargarlo de
// disco; si tampoco, dispara un fetch al server y espera el callback.
void schedulePrefetchEmotes() {
    using paimon::emotes::EmoteService;

    auto& service = EmoteService::get();

    // Disco primero — barato, sin red.
    if (!service.isLoaded()) {
        service.loadCatalogFromDisk();
    }

    if (service.isLoaded()) {
        startEmotePreloadIfReady();
        return;
    }

    // Si llegamos acá, no hay catálogo en disco. Pedimos uno al server y
    // arrancamos el preload de emotes cuando termine. Mientras tanto, el
    // label muestra solo el progreso de thumbnails (con "esperando catálogo
    // de emotes…" si querés más detalle — por ahora simplificamos a X/Y
    // dinámico).
    if (service.isFetching()) {
        // Otro caller (Bootstrap delayed) ya está fetcheando. No dupliquemos.
        // Hacemos polling pasivo: el label se actualizará cuando el catálogo
        // llegue y el siguiente LoadingLayer (si lo hay) lo recoja.
        log::info("[Paimbnails Preload] EmoteService ya está fetcheando catálogo; esperaremos al callback de Bootstrap");
        return;
    }

    log::info("[Paimbnails Preload] Catálogo de emotes no disponible — pidiendo al server");
    service.fetchAllEmotes([](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!success) {
            log::warn("[Paimbnails Preload] Fetch de catálogo de emotes falló");
            // Marcamos catálogo "ready" aunque haya fallado para que el label
            // no se quede esperando para siempre — total = 0 emotes.
            paimon::preload::g_emotesCatalogReady.store(true, std::memory_order_release);
            return;
        }
        startEmotePreloadIfReady();
    });
}

// Arranca el preload completo. Solo lo invoca el primer caller que gana
// tryClaimPreload().
void startFullPreload() {
    schedulePrefetchMainLevels();
    schedulePrefetchEmotes();
}

} // namespace

class $modify(PaimonLoadingLayer, LoadingLayer) {
    struct Fields {
        // Label adicional debajo de m_caption para mostrar "Paimbnails: X/Y".
        // No reemplazamos m_caption porque GD lo usa para sus mensajes
        // aleatorios; preferimos coexistir.
        cocos2d::CCLabelBMFont* progressLabel = nullptr;
        // Una sola programación de updateProgressLabel por instancia.
        bool updateScheduled = false;
        // Marca para que el setup (label + preload claim) ocurra solo una vez
        // por instancia, sin importar cuántas veces vanilla GD invoque
        // loadAssets durante los 14 steps internos.
        bool setupDone = false;
    };

    static void onModify(auto& self) {
        // Ejecutamos nuestro código DESPUÉS del loadAssets vanilla (post-hook)
        // para que m_loadStep ya esté actualizado cuando lo leamos. La
        // prioridad late evita cualquier conflicto con otros mods que
        // hookeen el mismo método.
        (void)self.setHookPriorityPost("LoadingLayer::loadAssets", Priority::Late);
    }

    bool init(bool fromReload) {
        if (!LoadingLayer::init(fromReload)) {
            return false;
        }
        LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
        // OJO: NO arrancamos el preload acá. La razón es que con
        // "early-load": false (default de Paimbnails), este hook puede no
        // estar instalado todavía cuando GD invoca init() por primera vez.
        // El setup real ocurre en loadAssets() que sí se llama varias veces
        // a lo largo del proceso de carga, dándonos garantía de que al
        // menos uno de esos calls será interceptado por nuestro hook
        // (mismo patrón que usa Globed en su LoadingLayer).
        return true;
    }

    void loadAssets() {
        // Setup lazy en la primera invocación que interceptamos. A partir de
        // ahí, los siguientes loadAssets son no-op para nosotros (solo
        // delegan al original). Esto soporta perfectamente el caso early-
        // load:false sin necesidad de cambiar mod.json.
        if (!m_fields->setupDone) {
            m_fields->setupDone = true;

            if (paimon::preload::tryClaimPreload()) {
                startFullPreload();
            }

            this->createProgressLabel();
            this->updateProgressLabel(0.f);
            this->scheduleProgressUpdates();
        }

        LoadingLayer::loadAssets();
    }

    void createProgressLabel() {
        if (m_fields->progressLabel) return;

        // Fuente pequeña y estilo discreto. La situamos justo arriba del
        // borde inferior, centrada — sin pisar m_caption (que está más
        // arriba) ni la barra de progreso vanilla.
        auto label = cocos2d::CCLabelBMFont::create("Paimbnails: 0/0", "chatFont.fnt");
        label->setScale(0.55f);
        label->setOpacity(200);
        label->setID("paimbnails-preload-progress"_spr);

        auto winSize = cocos2d::CCDirector::get()->getWinSize();
        // Y = 28: por debajo de m_sliderBar pero por arriba del borde
        // (la sliderBar de GD está alrededor de Y = 50-70).
        label->setPosition({winSize.width / 2.f, 28.f});

        // Z alto para que quede sobre el background pero sin tapar nada
        // crucial.
        this->addChild(label, 100);
        m_fields->progressLabel = label;
    }

    void scheduleProgressUpdates() {
        if (m_fields->updateScheduled) return;
        m_fields->updateScheduled = true;
        this->schedule(
            schedule_selector(PaimonLoadingLayer::updateProgressLabel),
            kProgressUpdateInterval
        );
    }

    void updateProgressLabel(float /*dt*/) {
        if (!m_fields->progressLabel) return;

        using namespace paimon::preload;

        int loaded = getTotalLoaded();
        int total = getTotalCount();

        std::string text;
        if (total == 0) {
            // Sin total aún: probablemente seguimos esperando el catálogo
            // de emotes. Mostramos un mensaje pasivo en lugar de "0/0"
            // porque "0/0" se ve roto.
            text = "Paimbnails: cargando...";
        } else if (loaded < total) {
            text = fmt::format("Paimbnails: {}/{}", loaded, total);
        } else {
            text = fmt::format("Paimbnails: {}/{} listo!", loaded, total);
            // Una vez terminó, dejamos de programar updates — el label se
            // queda en "listo!" hasta que el LoadingLayer muera.
            this->unschedule(schedule_selector(PaimonLoadingLayer::updateProgressLabel));
            m_fields->updateScheduled = false;
        }
        m_fields->progressLabel->setString(text.c_str());
    }
};
