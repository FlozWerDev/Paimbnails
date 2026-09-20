// Avoid underflow on empty scene.

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

// No fades during destruction.
    void destructor() {
        paimon::popupblur::cleanupAllActive(0.0f);
        CCScene::~CCScene();
    }
};
