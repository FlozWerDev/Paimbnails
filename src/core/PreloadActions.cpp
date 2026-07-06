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

void schedulePrefetchMainLevels() {
    using namespace paimon::preload;

    // Claim Bootstrap's flag so its fallback won't re-queue the same 22 tasks.
    (void)paimon::tryClaimMainLevelsPrefetch();

    std::vector<int> mainLevels;
    mainLevels.reserve(paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1);
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    g_thumbsTotal.store(static_cast<int>(mainLevels.size()), std::memory_order_release);
    g_thumbsLoaded.store(0, std::memory_order_release);

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

void schedulePrefetchEmotes() {
    using paimon::emotes::EmoteService;

    auto& service = EmoteService::get();

    if (!service.isLoaded()) {
        service.loadCatalogFromDisk();
    }

    if (service.isLoaded()) {
        startEmotePreloadIfReady();
        return;
    }

    if (service.isFetching()) {
        log::info("[Paimbnails Preload] EmoteService ya está fetcheando catálogo; esperaremos al callback");
        return;
    }

    log::info("[Paimbnails Preload] Catálogo de emotes no disponible — pidiendo al server");
    service.fetchAllEmotes([](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!success) {
            log::warn("[Paimbnails Preload] Fetch de catálogo de emotes falló");
            // Mark ready even on failure so the label doesn't wait forever.
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
    paimon::scheduleMainThreadDelay(14.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        schedulePrefetchEmotes();
    });
}

} // namespace paimon::preload
