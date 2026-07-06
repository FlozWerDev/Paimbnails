#pragma once

#include "MainLevels.hpp"
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
    *step = [ctx, step]() {
        if (isRuntimeShuttingDown()) return;

        int enqueued = 0;
        while (ctx->nextId <= kMainLevelMaxID && enqueued < ctx->batch) {
            ctx->load(ctx->nextId);
            ++ctx->nextId;
            ++enqueued;
        }

        if (ctx->nextId <= kMainLevelMaxID) {
            scheduleMainThreadDelay(ctx->delay, [step]() { (*step)(); });
        }
    };

    (*step)();
}

inline void fetchMainLevelManifestWithCache(std::vector<int> const& mainLevels,
                                            std::string context) {
    if (paimon::areMainLevelsFreshlyCached()) {
        geode::log::info(
            "[Paimbnails Cache] Main levels en cache permanente de 14 dias — "
            "se omite el manifest fetch ({})",
            context);
        return;
    }

    HttpClient::get().fetchManifest(mainLevels, [context](bool success) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (success) {
            paimon::markMainLevelsCached();
        }
        geode::log::info(
            "[Paimbnails Cache] Main level manifest fetch {} ({})",
            success ? "exitoso — ventana de 14 dias renovada"
                    : "fallido (se usara Worker fallback)",
            context);
    });
}

} // namespace paimon::preload
