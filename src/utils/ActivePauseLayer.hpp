#pragma once

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Hook.hpp>
#include <atomic>

namespace paimon {
    // raw atomic, identity-compared only: a weakref would crash on dangling
    // pause layers (weakrefpool::check). stale values get overwritten.
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
        // only clear when still ours; a closing layer must not clear a newer one.
        auto& slot = activePauseLayerAtomic();
        PauseLayer* expected = layer;
        slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

    inline bool isPauseLayerClosing(PauseLayer* layer) {
        return layer && layer->getUserObject("paimon-pause-closing");
    }

    inline void setPauseLayerClosing(PauseLayer* layer, bool closing) {
        if (layer) {
            layer->setUserObject("paimon-pause-closing",
                closing ? cocos2d::CCBool::create(true) : nullptr);
        }
    }

    inline bool isPauseLayerOpen(PauseLayer* layer) {
        if (!layer || !layer->isRunning() || !layer->getParent() || isPauseLayerClosing(layer)) return false;
        auto* director = cocos2d::CCDirector::get();
        auto* scene = director ? director->getRunningScene() : nullptr;
        auto* root = static_cast<cocos2d::CCNode*>(layer);
        while (root->getParent()) root = root->getParent();
        return scene && !director->getNextScene() && root == scene;
    }

    inline PauseLayer* findOpenPauseLayerInScene() {
        auto* director = cocos2d::CCDirector::get();
        auto* scene = director ? director->getRunningScene() : nullptr;
        auto* children = scene ? scene->getChildren() : nullptr;
        if (!children) return nullptr;

        PauseLayer* fallback = nullptr;
        auto* active = getActivePauseLayer();
        for (auto* obj : geode::cocos::CCArrayExt<cocos2d::CCObject*>(children)) {
            auto* layer = geode::cast::typeinfo_cast<PauseLayer*>(obj);
            if (!isPauseLayerOpen(layer)) continue;
            if (layer == active) return layer;
            fallback = layer;
        }
        return fallback;
    }

    // tells pausezoommanager (playlayer.cpp) the layer is closing so its ticker
    // stops calling showlayer()/setvisible() during the exit animation.
    void notifyPauseClosing();

    // native gameplay can resume without a PauseLayer button.
    void synchronizePauseLayers(PlayLayer* playLayer);

    // capture flag: pausezoommanager::update() must not restore visibility
    // mid-capture or the menu leaks into the screenshot.
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

    // zoom-hide flag: visit() returns early when set, even though m_bvisible
    // keeps being flipped back to true elsewhere.
    inline std::atomic<bool>& pauseZoomHiddenFlag() {
        static std::atomic<bool> s_zoomHidden{false};
        return s_zoomHidden;
    }

    inline bool isPauseZoomHidden() {
        return pauseZoomHiddenFlag().load(std::memory_order_acquire);
    }

    // ccnode::visit filter hook: on only while pause-zoom hides the layer,
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

    // scan for a real pauselayer; the atomic misses ones that predate
    // customsetup (e.g. esc + capture key in the same frame).
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
