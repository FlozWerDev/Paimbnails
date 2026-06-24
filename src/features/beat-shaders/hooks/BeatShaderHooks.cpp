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
#include "../../../framework/HookConventions.hpp"

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
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "MenuLayer::init");
    }
    bool init() override {
        if (!MenuLayer::init()) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatMenuHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "menu");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatMenuHook::deferredApply));
        MenuLayer::onExit();
    }
};

class $modify(PaimonBeatCreatorHook, CreatorLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "CreatorLayer::init");
    }
    bool init() override {
        if (!CreatorLayer::init()) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatCreatorHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "creator");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatCreatorHook::deferredApply));
        CreatorLayer::onExit();
    }
};

class $modify(PaimonBeatLevelInfoHook, LevelInfoLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelInfoLayer::init");
    }
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatLevelInfoHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "levelinfo");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatLevelInfoHook::deferredApply));
        LevelInfoLayer::onExit();
    }
};

class $modify(PaimonBeatLevelSelectHook, LevelSelectLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelSelectLayer::init");
    }
    bool init(int p) {
        if (!LevelSelectLayer::init(p)) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatLevelSelectHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "levelselect");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatLevelSelectHook::deferredApply));
        LevelSelectLayer::onExit();
    }
};

class $modify(PaimonBeatBrowserHook, LevelBrowserLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelBrowserLayer::init");
    }
    bool init(GJSearchObject* obj) {
        if (!LevelBrowserLayer::init(obj)) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatBrowserHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "browser");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatBrowserHook::deferredApply));
        LevelBrowserLayer::onExit();
    }
};

class $modify(PaimonBeatSearchHook, LevelSearchLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelSearchLayer::init");
    }
    bool init(int p) {
        if (!LevelSearchLayer::init(p)) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatSearchHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "search");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatSearchHook::deferredApply));
        LevelSearchLayer::onExit();
    }
};

class $modify(PaimonBeatLeaderboardsHook, LeaderboardsLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LeaderboardsLayer::init");
    }
    bool init(LeaderboardType type, LeaderboardStat stat) {
        if (!LeaderboardsLayer::init(type, stat)) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatLeaderboardsHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "leaderboards");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatLeaderboardsHook::deferredApply));
        LeaderboardsLayer::onExit();
    }
};

class $modify(PaimonBeatProfileHook, ProfilePage) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ProfilePage::loadPageFromUserInfo");
    }
    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatProfileHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "profile");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatProfileHook::deferredApply));
        ProfilePage::onExit();
    }
};

class $modify(PaimonBeatGarageHook, GJGarageLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJGarageLayer::init");
    }
    bool init() override {
        if (!GJGarageLayer::init()) return false;
        this->scheduleOnce(schedule_selector(PaimonBeatGarageHook::deferredApply), 0.f);
        return true;
    }
    void deferredApply(float) {
        apply(this, "garage");
    }
    void onExit() override {
        this->unschedule(schedule_selector(PaimonBeatGarageHook::deferredApply));
        GJGarageLayer::onExit();
    }
};