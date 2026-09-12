#pragma once

#include <Geode/Geode.hpp>
#include "../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <atomic>
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
        // Deliberately process-lifetime: a static container could destroy
        // callbacks containing Ref/WeakRef after Cocos' pools are gone.
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
        if (isRuntimeShuttingDown()) {
            fn = nullptr;
            this->release();
            return;
        }
        if (auto callback = std::move(fn)) callback();
        fn = nullptr;
        this->release();
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

    auto* t = new detail::MainThreadDelayTask();
    t->fn = std::move(callback);
    detail::MainThreadDelayTask::track(t);
    sched->scheduleSelector(
        schedule_selector(detail::MainThreadDelayTask::fire), t,
        0.f, 0, std::max(0.f, delay), false
    );
}

// Must run while CCDirector, CCScheduler and WeakRefPool are still alive.
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
