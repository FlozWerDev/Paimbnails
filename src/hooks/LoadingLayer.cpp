// More Icons needs installed gallery entries before resolving the equipped icon.
// Speculative preloads start from MenuLayer/Bootstrap, after Geode loads assets.
#include <Geode/modify/LoadingLayer.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/icon-gallery/services/GalleryInstaller.hpp"
#include "../utils/MainThread.hpp"

using namespace geode::prelude;

class $modify(PaimonLoadingLayer, LoadingLayer) {
    bool init(bool fromReload) {
        if (!LoadingLayer::init(fromReload)) return false;
        paimon::captureMainThread();
        paimon::icon_gallery::GalleryInstaller::registerAllInstalled();
        LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
        return true;
    }
};
