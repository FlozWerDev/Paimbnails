// Recolor icons at construction/lock transitions without a per-frame ticker.
// The trailing unlockColor is unsafe to read on Win64, so hooks pass {}.

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

    // Do not hook GJItemIcon::init: Win64 may pass its trailing ccColor3B through
    // a dangling hidden pointer for browser items. Recolor container subtrees.

    $override
    void changeToLockedState(float p0) {
        GJItemIcon::changeToLockedState(p0);
        // Use a durable marker instead of testing vanilla opacity.
        this->setUserObject(paimon::icons::kIconLockedKey, CCBool::create(true));
        // Snapshot vanilla locked colors before styling.
        IconRecolorEngine::get().snapshotLockedVanilla(this);
        IconLockStyler::get().apply(this);
    }
};

// Recolor the shop subtree after its items exist; init has no unsafe by-value
// parameters, unlike GJItemIcon::init.
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
