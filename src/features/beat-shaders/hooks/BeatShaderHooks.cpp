// BeatShaderHooks.cpp — wires Beat Shaders into common GD layers.
//
// Strategy: after the layer's background is set up by Paimbnails' existing
// per-layer hooks, we let BeatShaderManager:
//   1. Patch the LayerBgConfig.shader for that layer to one of our beat
//      shaders (when the feature is enabled).
//   2. Re-apply the background so the new shader is in effect.
//   3. Flip m_audioReactive on every ShaderBgSprite in the scene so the
//      audio uniforms get pushed.
//
// We use Priority::Late so we run AFTER the existing per-layer hooks that
// install the custom background — otherwise we'd be modifying a config that
// gets overwritten right after.

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/modify/LeaderboardsLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/GJGarageLayer.hpp>

#include "../services/BeatShaderManager.hpp"

using namespace geode::prelude;

namespace {

void apply(cocos2d::CCLayer* layer, char const* key) {
    if (!layer) return;
    geode::log::info("[BeatShaders/Hook] init done for layer '{}'", key);
    paimon::beat_shaders::BeatShaderManager::get().applyToLayer(layer, key);
}

} // anonymous namespace

class $modify(PaimonBeatMenuHook, MenuLayer) {
    static void onModify(auto& self) {
        // Run after the existing Paimbnails MenuLayer hooks that build the
        // background — they have their own setup and we just want to retag
        // the shader at the very end.
        (void)self.setHookPriorityPost("MenuLayer::init", Priority::Late);
    }
    bool init() {
        if (!MenuLayer::init()) return false;
        apply(this, "menu");
        return true;
    }
};

class $modify(PaimonBeatCreatorHook, CreatorLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("CreatorLayer::init", Priority::Late);
    }
    bool init() {
        if (!CreatorLayer::init()) return false;
        apply(this, "creator");
        return true;
    }
};

class $modify(PaimonBeatLevelInfoHook, LevelInfoLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelInfoLayer::init", Priority::Late);
    }
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        apply(this, "levelinfo");
        return true;
    }
};

class $modify(PaimonBeatLevelSelectHook, LevelSelectLayer) {
    bool init(int p) {
        if (!LevelSelectLayer::init(p)) return false;
        apply(this, "levelselect");
        return true;
    }
};

class $modify(PaimonBeatBrowserHook, LevelBrowserLayer) {
    bool init(GJSearchObject* obj) {
        if (!LevelBrowserLayer::init(obj)) return false;
        apply(this, "browser");
        return true;
    }
};

class $modify(PaimonBeatSearchHook, LevelSearchLayer) {
    bool init(int p) {
        if (!LevelSearchLayer::init(p)) return false;
        apply(this, "search");
        return true;
    }
};

class $modify(PaimonBeatLeaderboardsHook, LeaderboardsLayer) {
    bool init(LeaderboardType type, LeaderboardStat stat) {
        if (!LeaderboardsLayer::init(type, stat)) return false;
        apply(this, "leaderboards");
        return true;
    }
};

class $modify(PaimonBeatProfileHook, ProfilePage) {
    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;
        apply(this, "profile");
        return true;
    }
};

class $modify(PaimonBeatGarageHook, GJGarageLayer) {
    bool init() {
        if (!GJGarageLayer::init()) return false;
        apply(this, "garage");
        return true;
    }
};
