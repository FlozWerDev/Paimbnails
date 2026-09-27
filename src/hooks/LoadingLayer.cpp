// Speculative preloads start from MenuLayer/Bootstrap, after Geode loads assets.
#include <Geode/modify/LoadingLayer.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/MainThread.hpp"

using namespace geode::prelude;

class $modify(PaimonLoadingLayer, LoadingLayer) {
    bool init(bool refresh) {
        if (!LoadingLayer::init(refresh)) return false;
        paimon::captureMainThread();
        LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
        return true;
    }
};
