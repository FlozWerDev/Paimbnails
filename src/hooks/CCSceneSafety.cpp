// Guards CCScene::getHighestChildZ against crashing on an empty scene
// (count()==0 → count()-1 = UINT_MAX → out-of-bounds read). Can happen
// during scene transitions when other mods call into the game before the
// scene has children. Priority::First runs the check before vanilla code.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>
#include "../blur/PopupBlurService.hpp"

using namespace geode::prelude;

class $modify(PaimonSafeCCScene, CCScene) {
    static void onModify(auto& self) {
        // Run before any other mod to catch the empty-scene case before vanilla cocos.
        (void)self.setHookPriorityPre("cocos2d::CCScene::getHighestChildZ", geode::Priority::First);
    }

    float getHighestChildZ() {
        // No children → return 0; otherwise count()-1 underflows to UINT_MAX.
        auto* children = this->getChildren();
        if (!children || children->count() == 0) {
            return 0.0f;
        }
        return CCScene::getHighestChildZ();
    }

    // Sweep orphaned blurs when a scene is destroyed; safety net for a popup
    // closed without firing keyBackClicked/onExit. Usually a cheap no-op.
    void cleanup() {
        paimon::popupblur::cleanupAllActive(0.15f);
        CCScene::cleanup();
    }
};
