#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LeaderboardsLayer.hpp>

#include "../services/MenuPhysicsManager.hpp"
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

namespace {
    inline void apply(cocos2d::CCNode* host) {
        paimon::menuphysics::MenuPhysicsManager::get().onLayerEntered(host);
    }
}

class $modify(PaimonMenuPhysicsMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        return true;
    }

    void onEnterTransitionDidFinish() {
        MenuLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsCreatorLayer, CreatorLayer) {
    bool init() {
        if (!CreatorLayer::init()) return false;
        return true;
    }

    void onEnterTransitionDidFinish() {
        CreatorLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsLevelSelectLayer, LevelSelectLayer) {
    bool init(int page) {
        if (!LevelSelectLayer::init(page)) return false;
        return true;
    }

    void onEnterTransitionDidFinish() {
        LevelSelectLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};

class $modify(PaimonMenuPhysicsGarageLayer, GJGarageLayer) {
    bool init() {
        if (!GJGarageLayer::init()) return false;
        return true;
    }

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
    bool init(LeaderboardType type, LeaderboardStat stat) {
        if (!LeaderboardsLayer::init(type, stat)) return false;
        return true;
    }

    void onEnterTransitionDidFinish() {
        LeaderboardsLayer::onEnterTransitionDidFinish();
        apply(this);
    }
};
