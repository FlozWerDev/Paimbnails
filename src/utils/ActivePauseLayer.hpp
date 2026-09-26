#pragma once

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Hook.hpp>
#include <atomic>

namespace paimon {
    // raw atomic, identity-compared only: dodges the dangling-WeakRef crash
    // in WeakRefPool::check; stale values get overwritten.
    inline std::atomic<PauseLayer*>& activePauseLayerAtomic() {
        static std::atomic<PauseLayer*> s_activePauseLayer{nullptr};
        return s_activePauseLayer;
    }

    inline void setActivePauseLayer(PauseLayer* layer) {
        activePauseLayerAtomic().store(layer, std::memory_order_release);
    }

    inline PauseLayer* getActivePauseLayer() {
        return activePauseLayerAtomic().load(std::memory_order_acquire);
    }

    inline void clearActivePauseLayer(PauseLayer* layer) {
        // only clear when still ours; overlapping PauseLayers must not clear the newer one.
        auto& slot = activePauseLayerAtomic();
        PauseLayer* expected = layer;
        slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

    // tell PauseZoomManager (PlayLayer.cpp) the layer is closing so its ticker
    // stops calling showLayer()/setVisible() mid exit animation.
    void notifyPauseClosing();

    // capture flag: keeps PauseZoomManager::update() from restoring visibility
    // mid-capture, which would leak the menu into the screenshot.
    inline std::atomic<bool>& captureInProgressFlag() {
        static std::atomic<bool> s_inProgress{false};
        return s_inProgress;
    }

    inline bool isCaptureInProgress() {
        return captureInProgressFlag().load(std::memory_order_acquire);
    }

    inline void setCaptureInProgress(bool inProgress) {
        captureInProgressFlag().store(inProgress, std::memory_order_release);
    }

    // zoom-hide flag: visit() returns early when set, even though something
    // else keeps flipping m_bVisible back to true.
    inline std::atomic<bool>& pauseZoomHiddenFlag() {
        static std::atomic<bool> s_zoomHidden{false};
        return s_zoomHidden;
    }

    inline bool isPauseZoomHidden() {
        return pauseZoomHiddenFlag().load(std::memory_order_acquire);
    }

    // CCNode::visit filter hook: enabled only while pause-zoom hides the layer,
    // so normal frames pay no trampoline overhead.
    inline std::atomic<geode::Hook*>& pauseZoomVisitHookSlot() {
        static std::atomic<geode::Hook*> s_hook{nullptr};
        return s_hook;
    }

    inline void setPauseZoomVisitHook(geode::Hook* hook) {
        pauseZoomVisitHookSlot().store(hook, std::memory_order_release);
    }

    inline void setPauseZoomHidden(bool hidden) {
        bool prev = pauseZoomHiddenFlag().exchange(hidden, std::memory_order_acq_rel);
        if (prev == hidden) return;
        if (auto* hook = pauseZoomVisitHookSlot().load(std::memory_order_acquire)) {
            if (hidden) (void)hook->enable();
            else (void)hook->disable();
        }
    }

    // scan for a real PauseLayer; the atomic misses ones predating customSetup
    // (e.g. Esc + capture key in the same frame).
    inline bool hasPauseLayerInScene() {
        auto* director = cocos2d::CCDirector::get();
        if (!director) return false;
        auto* scene = director->getRunningScene();
        if (!scene) return false;
        auto* children = scene->getChildren();
        if (!children) return false;
        for (auto* obj : geode::cocos::CCArrayExt<cocos2d::CCObject*>(children)) {
            if (geode::cast::typeinfo_cast<PauseLayer*>(obj)) {
                return true;
            }
        }
        return false;
    }
}
