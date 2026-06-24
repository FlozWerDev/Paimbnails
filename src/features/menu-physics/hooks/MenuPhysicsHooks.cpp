#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LeaderboardsLayer.hpp>

#include "../services/MenuPhysicsManager.hpp"

using namespace geode::prelude;

namespace {
    inline void apply(cocos2d::CCNode* host) {
        paimon::menuphysics::MenuPhysicsManager::get().onLayerEntered(host);
    }
}

class $modify(PaimonMenuPhysicsMenuLayer, MenuLayer) {
    void onEnterTransitionDidFinish() {
        MenuLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsCreatorLayer, CreatorLayer) {
    void onEnterTransitionDidFinish() {
        CreatorLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsLevelSelectLayer, LevelSelectLayer) {
    void onEnterTransitionDidFinish() {
        LevelSelectLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsGarageLayer, GJGarageLayer) {
    void onEnterTransitionDidFinish() {
        GJGarageLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsBrowserLayer, LevelBrowserLayer) {
    void onEnterTransitionDidFinish() {
        LevelBrowserLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsLeaderboardsLayer, LeaderboardsLayer) {
    void onEnterTransitionDidFinish() {
        LeaderboardsLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};
