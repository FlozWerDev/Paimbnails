//
// GJItemIconHook.cpp - Hook GJItemIcon::init and changeToLockedState so every
// icon gets recolored at construction time, avoiding a per-frame ticker.
//
// The `ccColor3B unlockColor` parameter at the end is accepted but never read;
// reading it has caused crashes in other mod hooks. We forward {} to the original.
//

#include "../services/IconColorService.hpp"
#include "../services/IconConfigStore.hpp"
#include "../services/IconLockStyler.hpp"
#include "../services/IconRecolorEngine.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJItemIcon.hpp>
#include <Geode/modify/GJShopLayer.hpp>
#include "../../../framework/HookConventions.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using paimon::icons::IconColorService;
using paimon::icons::IconConfigStore;
using paimon::icons::IconDescriptor;
using paimon::icons::IconLockStyler;
using paimon::icons::IconRecolorEngine;
using paimon::icons::RecolorArea;

class $modify(PaimonGJItemIcon, GJItemIcon) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJItemIcon::changeToLockedState");
    }

    // We intentionally do NOT hook GJItemIcon::init. On Win64 the init
    // `ccColor3B` parameters are passed by hidden pointer, and
    // GJItemIcon::createBrowserItem leaves `unlockColor` pointing at garbage
    // (browser items have no unlock color). As the first hook in the chain our
    // detour's prologue would materialise that by-value parameter, dereference
    // the bad pointer and crash (EXCEPTION_ACCESS_VIOLATION) on garage entry.
    // Recoloring is handled by the container-based IconRecolorEngine instead
    // (PaimonIconsGarageGlue for the icon kit, the GJShopLayer glue below for
    // shops) -- the non-brittle approach.

    $override
    void changeToLockedState(float p0) {
        GJItemIcon::changeToLockedState(p0);
        IconLockStyler::get().apply(this);
    }
};

// Shops had no container-based recolor path (only the now-removed brittle
// GJItemIcon::init hook recolored their icons). Recolor the whole shop subtree
// once the layer is built, deferred one frame so the store items exist. Safe:
// GJShopLayer::init takes no by-value struct parameters.
class $modify(PaimonGJShopLayer, GJShopLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJShopLayer::init");
    }

    $override
    bool init(ShopType type) {
        if (!GJShopLayer::init(type)) return false;
        Ref<GJShopLayer> ref = this;
        Loader::get()->queueInMainThread([ref]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!ref) return;
            IconRecolorEngine::get().recolorSubtree(ref, RecolorArea::Shop);
        });
        return true;
    }
};
