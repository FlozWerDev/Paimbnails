#include <Geode/modify/LoadingLayer.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/MainThread.hpp"

using namespace geode::prelude;

class $modify(PaimonLoadingLayer, LoadingLayer) {
    struct Fields {
        bool m_backgroundApplied = false;
    };

    void loadAssets() {
        // Geode creates the boot layer before loading ordinary mods, so an
        // init hook misses startup. loadAssets also runs after our hooks load.
        if (!m_fields->m_backgroundApplied) {
            m_fields->m_backgroundApplied = true;
            paimon::captureMainThread();
            auto& backgrounds = LayerBackgroundManager::get();
            backgrounds.applyBackground(this, "loading");
            backgrounds.applyVanillaBackgroundTintFix(this);
        }
        LoadingLayer::loadAssets();
    }
};
