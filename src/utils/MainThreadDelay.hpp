#pragma once

#include <Geode/Geode.hpp>
#include "../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace paimon {

namespace detail {

struct MainThreadDelayTask final : cocos2d::CCObject {
    geode::CopyableFunction<void()> fn;

    static std::mutex& registryMutex() {
        static auto* mutex = new std::mutex();
        return *mutex;
    }

    static std::unordered_set<MainThreadDelayTask*>& registry() {
        // process-lifetime: statics could destroy ref/weakref callbacks after cocos pools.
        static auto* tasks = new std::unordered_set<MainThreadDelayTask*>();
        return *tasks;
    }

    static void track(MainThreadDelayTask* task) {
        std::lock_guard lock(registryMutex());
        registry().insert(task);
    }

    static void untrack(MainThreadDelayTask* task) {
        std::lock_guard lock(registryMutex());
        registry().erase(task);
    }

    void fire(float) {
        if (auto* dir = cocos2d::CCDirector::get()) {
            if (auto* scheduler = dir->getScheduler()) {
                scheduler->unscheduleSelector(
                    schedule_selector(MainThreadDelayTask::fire), this
                );
            }
        }
        untrack(this);
        auto callback = std::move(fn);
        fn = nullptr;
        this->release();
        if (!isRuntimeShuttingDown() && callback) callback();
    }
};

} // namespace detail

inline void scheduleMainThreadDelay(float delay, geode::CopyableFunction<void()> callback) {
    if (!callback) return;
    if (isRuntimeShuttingDown()) return;
    auto* director = cocos2d::CCDirector::get();
    if (!director) return;
    auto* sched = director->getScheduler();
    if (!sched) return;

    auto t = geode::Ref<detail::MainThreadDelayTask>::adopt(new detail::MainThreadDelayTask());
    t->fn = std::move(callback);
    detail::MainThreadDelayTask::track(t.data());
    try {
        sched->scheduleSelector(
            schedule_selector(detail::MainThreadDelayTask::fire), t.data(),
            0.f, 0, std::isfinite(delay) ? std::max(0.f, delay) : 0.f, false
        );
    } catch (...) {
        sched->unscheduleSelector(schedule_selector(detail::MainThreadDelayTask::fire), t.data());
        detail::MainThreadDelayTask::untrack(t.data());
        throw;
    }
    t.take();
}

// ccdirector, ccscheduler and weakrefpool must still be alive.
inline void cancelAllMainThreadDelays() {
    std::vector<detail::MainThreadDelayTask*> tasks;
    {
        std::lock_guard lock(detail::MainThreadDelayTask::registryMutex());
        auto& registry = detail::MainThreadDelayTask::registry();
        tasks.assign(registry.begin(), registry.end());
        registry.clear();
    }

    auto* director = cocos2d::CCDirector::get();
    auto* scheduler = director ? director->getScheduler() : nullptr;
    for (auto* task : tasks) {
        if (!task) continue;
        if (scheduler) {
            scheduler->unscheduleSelector(
                schedule_selector(detail::MainThreadDelayTask::fire), task
            );
        }
        task->fn = nullptr;
        task->release();
    }
}

inline std::atomic<uint32_t> g_deferredModSaveGeneration = 0;

inline void requestDeferredModSave(float delay = 0.2f) {
    auto generation = ++g_deferredModSaveGeneration;
    scheduleMainThreadDelay(std::max(0.f, delay), [generation]() {
        if (generation != g_deferredModSaveGeneration.load(std::memory_order_acquire)) return;
        auto* mod = geode::Mod::get();
        if (!mod) return;
        (void)mod->saveData();
    });
}

} // namespace paimon
