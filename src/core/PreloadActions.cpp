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
#include "../features/global-icon/services/GlobalIconStorage.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/ThreadTracker.hpp"

#include <functional>

using namespace geode::prelude;

namespace {

// The loading screen can stay up for a long time on a slow machine; starting
// the emote download there would fight the game for disk and bandwidth.
void scheduleAfterGameLoaded(float delay, std::function<void()> fn) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (paimon::preload::g_gameLoaded.load(std::memory_order_acquire)) {
        paimon::scheduleMainThreadDelay(delay, [fn]() { fn(); });
        return;
    }
    paimon::scheduleMainThreadDelay(0.5f, [delay, fn]() {
        if (paimon::isRuntimeShuttingDown()) return;
        scheduleAfterGameLoaded(delay, fn);
    });
}

void schedulePrefetchMainLevels() {
    using namespace paimon::preload;

    // All startup entry points share this claim, including late mod loading.
    if (!paimon::tryClaimMainLevelsPrefetch()) return;

    std::vector<int> mainLevels;
    mainLevels.reserve(paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1);
    for (int i = paimon::kMainLevelMinID; i <= paimon::kMainLevelMaxID; i++) {
        mainLevels.push_back(i);
    }

    g_thumbsTotal.store(static_cast<int>(mainLevels.size()), std::memory_order_release);
    g_thumbsLoaded.store(0, std::memory_order_release);

    paimon::preload::fetchMainLevelManifestWithCache(mainLevels, "Preload", [] {
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
        }, 1, 0.1f);

        log::info(
            "[Paimbnails Preload] Stagger-queued {} main level thumbnails",
            paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1
        );
    });
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
        log::info("[Paimbnails Preload] Emote catalog vacio - preload omitido");
        return;
    }

    log::info("[Paimbnails Preload] Iniciando preload de {} emotes", emotes.size());

    EmoteCache::get().preloadAllToDisk(
        [](size_t downloaded, size_t skipped, size_t total) {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info(
                "[Paimbnails Preload] Emote preload termino: {} descargados, {} ya en cache, {} totales",
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
        log::info("[Paimbnails Preload] EmoteService ya esta fetcheando catalogo; esperaremos al callback");
    } else {
        log::info("[Paimbnails Preload] Catalogo de emotes no disponible - pidiendo al server");
    }

    service.fetchAllEmotes([](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!success) {
            log::warn("[Paimbnails Preload] Fetch de catalogo de emotes fallo");
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
    // No cache stats, HTTP client construction or texture uploads while Geode
    // is loading binaries/resources. Publish the total now for the menu label.
    g_thumbsTotal.store(paimon::kMainLevelMaxID - paimon::kMainLevelMinID + 1,
        std::memory_order_release);
    scheduleAfterGameLoaded(1.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        schedulePrefetchMainLevels();
    });
    scheduleAfterGameLoaded(14.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        schedulePrefetchEmotes();
    });
    // Downloaded global icons pile up one directory per visited profile; trim
    // the oldest once the startup rush is over. disk-only, so it runs off the
    // main thread instead of hitching the menu 20s in.
    scheduleAfterGameLoaded(20.0f, []() {
        if (paimon::isRuntimeShuttingDown()) return;
        paimon::ThreadTracker::get().spawn([]() {
            geode::utils::thread::setName("PaimonGlobalIconPrune");
            if (paimon::isRuntimeShuttingDown()) return;
            paimon::globalicon::GlobalIconStorage::get().pruneCache();
        });
    });
}

} // namespace paimon::preload
