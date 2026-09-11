#pragma once

#include "MainLevels.hpp"
#include "BackgroundPreload.hpp"
#include "RuntimeLifecycle.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/function.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace paimon::preload {

template <typename LoadFn>
void staggerMainLevelThumbnailLoads(LoadFn&& loadFn, int batchSize = 4, float batchDelaySec = 0.06f) {
    struct Ctx {
        int nextId = kMainLevelMinID;
        int batch = 4;
        float delay = 0.06f;
        geode::CopyableFunction<void(int)> load;
    };

    auto ctx = std::make_shared<Ctx>(Ctx{
        kMainLevelMinID,
        std::max(1, batchSize),
        std::max(0.03f, batchDelaySec),
        geode::CopyableFunction<void(int)>(std::forward<LoadFn>(loadFn)),
    });

    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakStep = step;
    *step = [ctx, weakStep]() {
        if (isRuntimeShuttingDown()) return;

        if (!canRunBackgroundPreload()) {
            if (auto next = weakStep.lock()) {
                scheduleMainThreadDelay(0.5f, [next]() { (*next)(); });
            }
            return;
        }

        int enqueued = 0;
        while (ctx->nextId <= kMainLevelMaxID && enqueued < ctx->batch) {
            ctx->load(ctx->nextId);
            ++ctx->nextId;
            ++enqueued;
        }

        if (ctx->nextId <= kMainLevelMaxID) {
            if (auto next = weakStep.lock()) {
                scheduleMainThreadDelay(ctx->delay, [next]() { (*next)(); });
            }
        }
    };

    (*step)();
}

inline void fetchMainLevelManifestWithCache(std::vector<int> const& mainLevels,
                                            std::string context,
                                            std::function<void()> onReady = {}) {
    if (paimon::areMainLevelsFreshlyCached()) {
        geode::log::info(
            "[Paimbnails Cache] Main levels en cache permanente de 30 dias - "
            "se omite el manifest fetch ({})",
            context);
        if (onReady) onReady();
        return;
    }

    HttpClient::get().fetchManifest(mainLevels, [context, onReady = std::move(onReady)](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (success) {
            paimon::markMainLevelsCached();
        }
        geode::log::info(
            "[Paimbnails Cache] Main level manifest fetch {} ({})",
            success ? "exitoso - ventana de 30 dias renovada"
                    : "fallido (se usara Worker fallback)",
            context);
        if (onReady) onReady();
    });
}

} // namespace paimon::preload
