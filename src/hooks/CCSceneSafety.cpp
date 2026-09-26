#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>

using namespace geode::prelude;

class $modify(PaimonSafeCCScene, CCScene) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCScene::getHighestChildZ", geode::Priority::First);
    }

    int getHighestChildZ() {
        auto* children = this->getChildren();
        // empty scene underflows the vanilla max loop
        if (!children || children->count() == 0) {
            return 0;
        }
        return CCScene::getHighestChildZ();
    }
};
