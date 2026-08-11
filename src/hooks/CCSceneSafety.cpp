// Prevent getHighestChildZ from underflowing while a scene is empty.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>
#include "../blur/PopupBlurService.hpp"

using namespace geode::prelude;

class $modify(PaimonSafeCCScene, CCScene) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCScene::getHighestChildZ", geode::Priority::First);
    }

    int getHighestChildZ() {
        auto* children = this->getChildren();
        if (!children || children->count() == 0) {
            return 0;
        }
        return CCScene::getHighestChildZ();
    }

// Clean orphaned popup blurs when a scene is destroyed without normal close hooks.
    void destructor() {
        paimon::popupblur::cleanupAllActive(0.15f);
        CCScene::~CCScene();
    }
};
