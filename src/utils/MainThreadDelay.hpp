#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <algorithm>
#include <atomic>

namespace paimon {

inline void scheduleMainThreadDelay(float delay, geode::CopyableFunction<void()> callback) {
    if (!callback) return;
    auto* sched = cocos2d::CCDirector::sharedDirector()->getScheduler();
    if (!sched) return;

    struct Task final : cocos2d::CCObject {
        geode::CopyableFunction<void()> fn;
        void fire(float) {
            if (auto* s = cocos2d::CCDirector::sharedDirector()->getScheduler())
                s->unscheduleSelector(schedule_selector(Task::fire), this);
            if (auto cb = std::move(fn)) cb();
            this->release();
        }
    };

    auto* t = new Task();
    t->fn = std::move(callback);
    sched->scheduleSelector(
        schedule_selector(Task::fire), t,
        0.f, 0, std::max(0.f, delay), false
    );
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
