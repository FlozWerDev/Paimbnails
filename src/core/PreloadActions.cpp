// Shared implementation for the main-level thumbnail + emote preload.

#include "PreloadActions.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

#include "MainLevels.hpp"
#include "MainLevelPrefetch.hpp"
#include "PreloadProgress.hpp"
#include "RuntimeLifecycle.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/MainThreadDelay.hpp"

using namespace geode::prelude;

namespace {

// Phase 1: queue main-level (1-22) thumbnails.
void schedulePrefetchMainLevels() {
    using namespace paimon::preload;

    // Claim Bootstrap's legacy flag so its fallback won't re-queue the same 22
    // tasks. ThumbnailLoader is idempotent, but skipping 22 redundant
    // requestLoad calls speeds startup and avoids duplicate logs.
    (void)paimon::tryClaimMainLevelsPrefetch();

    std::vector<int> mainLevels;
    mainLevels.reserve(paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1);
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    g_thumbsTotal.store(static_cast<int>(mainLevels.size()), std::memory_order_release);
    g_thumbsLoaded.store(0, std::memory_order_release);

    // Batched manifest to resolve all 22 CDN URLs in one request; honors the
    // 14-day cache window.
    paimon::preload::fetchMainLevelManifestWithCache(mainLevels, "Preload");

    auto& loader = ThumbnailLoader::get();
    paimon::preload::staggerMainLevelThumbnailLoads([&loader](int levelID) {
        loader.requestLoad(
            levelID,
            fmt::format("{}.png", levelID),
            [](cocos2d::CCTexture2D*, bool /*success*/) {
                paimon::preload::g_thumbsLoaded.fetch_add(1, std::memory_order_acq_rel);
            },
            ThumbnailLoader::PriorityBootstrap
        );
    }, 4, 0.06f);

    log::info(
        "[Paimbnails Preload] Stagger-queued {} main level thumbnails",
        mainLevels.size()
    );
}

// Start emote preload once the catalog is available.
void startEmotePreloadIfReady() {
    using namespace paimon::preload;
    using paimon::emotes::EmoteCache;
    using paimon::emotes::EmoteService;

    auto emotes = EmoteService::get().getAllEmotes();
    g_emotesTotal.store(static_cast<int>(emotes.size()), std::memory_order_release);
    g_emotesLoaded.store(0, std::memory_order_release);
    g_emotesCatalogReady.store(true, std::memory_order_release);

    if (emotes.empty()) {
        log::info("[Paimbnails Preload] Emote catalog vacío — preload omitido");
        return;
    }

    log::info("[Paimbnails Preload] Iniciando preload de {} emotes", emotes.size());

    EmoteCache::get().preloadAllToDisk(
        [](size_t downloaded, size_t skipped, size_t total) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info(
                "[Paimbnails Preload] Emote preload terminó: {} descargados, {} ya en cache, {} totales",
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
}

// Phase 2: emotes. If no catalog yet, try loading from disk; otherwise fetch
// from the server and wait for the callback.
void schedulePrefetchEmotes() {
    using paimon::emotes::EmoteService;

    auto& service = EmoteService::get();

    // Disk first: cheap, no network.
    if (!service.isLoaded()) {
        service.loadCatalogFromDisk();
    }

    if (service.isLoaded()) {
        startEmotePreloadIfReady();
        return;
    }

    if (service.isFetching()) {
        // Another caller (delayed Bootstrap) is already fetching; don't duplicate.
        log::info("[Paimbnails Preload] EmoteService ya está fetcheando catálogo; esperaremos al callback");
        return;
    }

    log::info("[Paimbnails Preload] Catálogo de emotes no disponible — pidiendo al server");
    service.fetchAllEmotes([](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!success) {
            log::warn("[Paimbnails Preload] Fetch de catálogo de emotes falló");
            // Mark the catalog "ready" even on failure so the label doesn't wait
            // forever; total = 0 emotes.
            paimon::preload::g_emotesCatalogReady.store(true, std::memory_order_release);
            return;
        }
        startEmotePreloadIfReady();
    });
}

} // namespace

namespace paimon::preload {

void startFullPreload() {
    schedulePrefetchMainLevels();
    // Defer emotes until after the menu so they don't compete with thumbnails
    // and GPU decode during loading / the first seconds in the menu.
    paimon::scheduleMainThreadDelay(14.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        schedulePrefetchEmotes();
    });
}

} // namespace paimon::preload
