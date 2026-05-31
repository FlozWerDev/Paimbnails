// MenuLayerPreloadFallback.cpp — Garantiza que el preload arranque
// y el label X/Y se muestre incluso si el LoadingLayer hook no llegó
// a interceptar nada (caso típico: mod con early-load:false que se
// terminó de cargar después de los 14 steps de LoadingLayer::loadAssets).
//
// Diseño:
//   - Modify class separado del PaimonMenuLayer existente para no tocar
//     ese archivo (que ya tiene mucha lógica). Geode permite múltiples
//     $modify sobre el mismo target sin conflicto; cada uno encadena su
//     callback al pipeline del hook.
//   - Solo crea el label si tryClaimPreload() fue ganado por nosotros
//     (es decir, el LoadingLayer NO lo hizo). Si LoadingLayer ya tomó
//     el preload, el label vive en LoadingLayer y aquí no duplicamos.
//   - El label se posiciona discretamente en la esquina superior-
//     izquierda del menú, debajo del nombre de usuario, para no pisar
//     elementos clickeables. Se autodestruye cuando el preload termina.

#include <Geode/modify/MenuLayer.hpp>

#include <fmt/format.h>

#include "../core/PreloadProgress.hpp"
#include "../core/MainLevels.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../utils/HttpClient.hpp"

using namespace geode::prelude;

namespace {

constexpr float kProgressUpdateInterval = 0.1f;

// Misma lógica de schedulePrefetchMainLevels / schedulePrefetchEmotes
// que tiene LoadingLayer.cpp, pero local al fallback. Se invoca solo si
// tryClaimPreload() devolvió true (es decir, LoadingLayer no la corrió).
void startPreloadFallback() {
    using namespace paimon::preload;
    using paimon::emotes::EmoteService;
    using paimon::emotes::EmoteCache;

    // ── Thumbnails 1-22 ──
    (void)paimon::tryClaimMainLevelsPrefetch();

    std::vector<int> mainLevels;
    mainLevels.reserve(paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1);
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    g_thumbsTotal.store(static_cast<int>(mainLevels.size()), std::memory_order_release);
    g_thumbsLoaded.store(0, std::memory_order_release);

    HttpClient::get().fetchManifest(mainLevels, [](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        log::info(
            "[Paimbnails Preload Fallback] Manifest fetch {}",
            success ? "succeeded" : "failed (will use Worker fallback)"
        );
    });

    auto& loader = ThumbnailLoader::get();
    for (int levelID : mainLevels) {
        loader.requestLoad(
            levelID,
            fmt::format("{}.png", levelID),
            [](cocos2d::CCTexture2D*, bool) {
                paimon::preload::g_thumbsLoaded.fetch_add(1, std::memory_order_acq_rel);
            },
            ThumbnailLoader::PriorityBootstrap
        );
    }

    log::info(
        "[Paimbnails Preload Fallback] Queued {} main level thumbnails",
        mainLevels.size()
    );

    // ── Emotes ──
    auto& service = EmoteService::get();
    auto fireEmotePreload = []() {
        auto emotes = paimon::emotes::EmoteService::get().getAllEmotes();
        g_emotesTotal.store(static_cast<int>(emotes.size()), std::memory_order_release);
        g_emotesLoaded.store(0, std::memory_order_release);
        g_emotesCatalogReady.store(true, std::memory_order_release);

        if (emotes.empty()) {
            log::info("[Paimbnails Preload Fallback] Catálogo de emotes vacío");
            return;
        }
        log::info(
            "[Paimbnails Preload Fallback] Iniciando preload de {} emotes",
            emotes.size()
        );
        EmoteCache::get().preloadAllToDisk(
            [](size_t downloaded, size_t skipped, size_t total) {
                if (paimon::isRuntimeShuttingDown()) return;
                log::info(
                    "[Paimbnails Preload Fallback] Emotes: {} bajados, {} cache, {} totales",
                    downloaded, skipped, total
                );
            },
            [](size_t completed, size_t total) {
                paimon::preload::g_emotesLoaded.store(
                    static_cast<int>(completed), std::memory_order_release);
                paimon::preload::g_emotesTotal.store(
                    static_cast<int>(total), std::memory_order_release);
            }
        );
    };

    if (!service.isLoaded()) {
        service.loadCatalogFromDisk();
    }

    if (service.isLoaded()) {
        fireEmotePreload();
    } else if (service.isFetching()) {
        log::info("[Paimbnails Preload Fallback] EmoteService ya está fetcheando, esperaremos");
    } else {
        log::info("[Paimbnails Preload Fallback] Catálogo no disponible — pidiendo al server");
        service.fetchAllEmotes([fireEmotePreload](bool success) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!success) {
                log::warn("[Paimbnails Preload Fallback] Fetch de catálogo falló");
                paimon::preload::g_emotesCatalogReady.store(true, std::memory_order_release);
                return;
            }
            fireEmotePreload();
        });
    }
}

} // namespace

class $modify(PaimonMenuLayerPreload, MenuLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* progressLabel = nullptr;
        bool updateScheduled = false;
        // True si esta instancia es la dueña del preload (lo arrancó). Si es
        // false (LoadingLayer ya lo había arrancado), igual mostramos el
        // label, pero solo si todavía no terminó.
        bool ownsPreload = false;
    };

    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        // Si nadie reclamó todavía (LoadingLayer hook nunca corrió), arrancamos
        // nosotros. Si ya fue reclamado, solo observamos los contadores.
        if (paimon::preload::tryClaimPreload()) {
            m_fields->ownsPreload = true;
            startPreloadFallback();
        }

        // Mostrar label solo si:
        //   - Hay un total > 0 (preload arrancó), Y
        //   - Aún no terminó.
        // Si el preload ya terminó antes de llegar al menú, no spammeamos
        // nada.
        if (paimon::preload::getTotalCount() > 0 && !paimon::preload::isFinished()) {
            this->createPreloadLabel();
            this->updatePreloadLabel(0.f);
            this->schedulePreloadLabelUpdates();
        } else if (m_fields->ownsPreload) {
            // Edge case: arrancamos preload acá y el catálogo de emotes
            // todavía no llegó (total=0). Mostramos el label igual con
            // "cargando…" hasta que el total se conozca.
            this->createPreloadLabel();
            this->updatePreloadLabel(0.f);
            this->schedulePreloadLabelUpdates();
        }

        return true;
    }

    void createPreloadLabel() {
        if (m_fields->progressLabel) return;
        // Verificar también si ya existe en la escena por otra instancia
        // (poco probable, pero defensivo).
        if (this->getChildByID("paimbnails-menu-preload-progress"_spr)) return;

        auto label = cocos2d::CCLabelBMFont::create("Paimbnails: 0/0", "chatFont.fnt");
        label->setScale(0.45f);
        label->setOpacity(180);
        label->setID("paimbnails-menu-preload-progress"_spr);
        label->setAnchorPoint({0.f, 1.f});

        auto winSize = cocos2d::CCDirector::get()->getWinSize();
        // Esquina superior-izquierda, alejado de los botones de username y
        // controles. Y = winSize.height - 6, X = 6.
        label->setPosition({6.f, winSize.height - 6.f});
        this->addChild(label, 1000);
        m_fields->progressLabel = label;
    }

    void schedulePreloadLabelUpdates() {
        if (m_fields->updateScheduled) return;
        m_fields->updateScheduled = true;
        this->schedule(
            schedule_selector(PaimonMenuLayerPreload::updatePreloadLabel),
            kProgressUpdateInterval
        );
    }

    void updatePreloadLabel(float /*dt*/) {
        if (!m_fields->progressLabel) return;
        using namespace paimon::preload;

        int loaded = getTotalLoaded();
        int total = getTotalCount();

        std::string text;
        if (total == 0) {
            text = "Paimbnails: cargando...";
        } else if (loaded < total) {
            text = fmt::format("Paimbnails: {}/{}", loaded, total);
        } else {
            text = fmt::format("Paimbnails: {}/{} listo!", loaded, total);
            this->unschedule(schedule_selector(PaimonMenuLayerPreload::updatePreloadLabel));
            m_fields->updateScheduled = false;
            // Después de 2 segundos en estado "listo!", removemos el label
            // para no taparle la pantalla al usuario indefinidamente.
            auto label = m_fields->progressLabel;
            this->scheduleOnce(
                schedule_selector(PaimonMenuLayerPreload::removePreloadLabel),
                2.0f
            );
            (void)label;
        }
        m_fields->progressLabel->setString(text.c_str());
    }

    void removePreloadLabel(float /*dt*/) {
        if (m_fields->progressLabel) {
            m_fields->progressLabel->removeFromParent();
            m_fields->progressLabel = nullptr;
        }
    }
};
