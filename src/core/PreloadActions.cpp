#include "PreloadActions.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

#include "MainLevels.hpp"
#include "MainLevelPrefetch.hpp"
#include "PreloadProgress.hpp"
#include "RuntimeLifecycle.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/global-icon/services/GlobalIconStorage.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/ThreadTracker.hpp"

#include <functional>

using namespace geode::prelude;

namespace {

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
    // Global icons pile one directory per visited profile; trim the oldest off
    // the main thread once the startup rush is over (disk-only, no menu hitch).
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
