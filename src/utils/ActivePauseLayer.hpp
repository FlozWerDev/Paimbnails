#pragma once

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Hook.hpp>
#include <atomic>

namespace paimon {
    // Active PauseLayer registry.
    // Raw atomic pointer instead of a WeakRef: the pointer is only compared for
    // identity (never dereferenced), so we avoid the dangling-WeakRef crash in
    // WeakRefPool::check. A stale pointer is harmless — the next setActivePauseLayer
    // overwrites it atomically before anyone calls visit() on a dead layer.
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
        // Only clear if the active PauseLayer is exactly this one, so overlapping
        // PauseLayers don't clear the wrong (newer) one.
        auto& slot = activePauseLayerAtomic();
        PauseLayer* expected = layer;
        slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

    // Tell PauseZoomManager (in PlayLayer.cpp) the PauseLayer is closing, so its
    // ticker stops calling showLayer()/setVisible() during the exit animation.
    void notifyPauseClosing();

    // "Capture in progress" flag: keeps PauseZoomManager::update() from restoring
    // PauseLayer visibility between setVisible(false) and the actual capture,
    // which would otherwise leave the menu in the screenshot.
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

    // "PauseLayer hidden by zoom" flag. Checked in PauseLayer::visit() (overridden
    // in PauseLayer.cpp): when set, visit() returns early so the layer isn't drawn
    // regardless of m_bVisible, which something else keeps flipping back to true.
    inline std::atomic<bool>& pauseZoomHiddenFlag() {
        static std::atomic<bool> s_zoomHidden{false};
        return s_zoomHidden;
    }

    inline bool isPauseZoomHidden() {
        return pauseZoomHiddenFlag().load(std::memory_order_acquire);
    }

    // The CCNode::visit filter hook (PaimonPauseZoomVisitFilter in PlayLayer.cpp).
    // CCNode::visit runs for every node every frame, so the hook stays disabled
    // except while the PauseLayer is actually hidden by pause-zoom; that removes
    // the trampoline overhead from the 99.9% of frames that don't need the filter.
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

    // Scan the scene for a real PauseLayer. More reliable than the atomic registry,
    // which only updates in customSetup/onExit and can miss a PauseLayer that exists
    // before customSetup runs (e.g. Esc + capture-key in the same frame).
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
