#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../services/GlassDecorator.hpp"

#include <Geode/modify/AchievementBar.hpp>
#include <Geode/modify/ButtonSprite.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCLayer.hpp>
#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/GJDropDownLayer.hpp>
#include <Geode/modify/TextAlertPopup.hpp>
#include <Geode/ui/SceneEvent.hpp>

using namespace geode::prelude;

// Liquid Glass reaches every UI scene, transition, popup, dropdown, toast and
// UI button synchronously before Frame 0 draws, avoiding any flash of vanilla art.
// The hooks only attach a plate; whether it draws is decided per frame by the
// current settings, so turning the material off needs no cleanup pass.

$execute {
    // Fires before a scene switch (including transitions), so Frame 0 enters
    // already decorated.
    geode::SceneEvent().listen(+[](cocos2d::CCScene* scene) -> bool {
        if (!paimon::isRuntimeShuttingDown() && paimon::glass::GlassRuntime::get().active()) {
            paimon::glass::decorateScene(scene);
        }
        return false;
    }).leak();
}

// Runs right as CCDirector commits the next scene (after scheduler::update,
// before m_pRunningScene->visit()), ensuring both scenes and overlays enter Frame 0
// with Liquid Glass plates already attached.
class $modify(PaimonGlassDirector, CCDirector) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "cocos2d::CCDirector::setNextScene");
    }

    $override
    void setNextScene() {
        CCDirector::setNextScene();
        if (paimon::isRuntimeShuttingDown() || !paimon::glass::GlassRuntime::get().active()) return;
        paimon::glass::watchPopups();
    }
};

// Catches any UI layer, list, dialog or popup entering the scene tree between
// frames (before its first visit/draw).
class $modify(PaimonGlassLayerEnter, CCLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "cocos2d::CCLayer::onEnter");
    }

    $override
    void onEnter() {
        CCLayer::onEnter();
        if (paimon::isRuntimeShuttingDown() || !paimon::glass::GlassRuntime::get().active()) return;
        paimon::glass::decorateEnteredLayer(this);
    }
};

class $modify(PaimonGlassAlertLayer, FLAlertLayer) {
    static void onModify(auto& self) {
        // after every other show() hook has finished building the popup.
        paimon::hooks::veryLatePost(self, "FLAlertLayer::show");
    }

    $override
    void show() {
        FLAlertLayer::show();
        if (paimon::isRuntimeShuttingDown() || !paimon::glass::GlassRuntime::get().active()) return;
        paimon::glass::decorateTree(this);
    }
};

// OptionsLayer, EndLevelLayer, SongsLayer... skip FLAlertLayer::show.
class $modify(PaimonGlassDropDownLayer, GJDropDownLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "GJDropDownLayer::showLayer");
    }

    $override
    void showLayer(bool instant) {
        GJDropDownLayer::showLayer(instant);
        if (paimon::isRuntimeShuttingDown() || !paimon::glass::GlassRuntime::get().active()) return;
        paimon::glass::decorateDropDown(this);
    }
};

class $modify(PaimonGlassAchievementBar, AchievementBar) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "AchievementBar::show");
    }

    $override
    void show() {
        AchievementBar::show();
        if (paimon::isRuntimeShuttingDown() || !paimon::glass::GlassRuntime::get().active()) return;
        paimon::glass::decorateTree(this);
    }
};

class $modify(PaimonGlassTextAlert, TextAlertPopup) {
    $override
    bool init(std::string text, float delay, float scale, int opacity, std::string font) {
        if (!TextAlertPopup::init(std::move(text), delay, scale, opacity, std::move(font))) return false;
        if (!paimon::isRuntimeShuttingDown() && paimon::glass::GlassRuntime::get().active()) {
            paimon::glass::decorateTree(this);
        }
        return true;
    }
};

// Geode stores a modify's hooks by "Class::function", so two overloads in
// one $modify overwrite each other and only the last one is installed.
// each ButtonSprite::init overload therefore lives in its own modify.

// text buttons: "OK", "View", "Pick"... (ButtonSprite::create(caption, ...)).
class $modify(PaimonGlassButtonSprite, ButtonSprite) {
    $override
    bool init(char const* caption, int width, int minWidth, float scale, bool absolute,
              char const* font, char const* bg, float height) {
        if (!ButtonSprite::init(caption, width, minWidth, scale, absolute, font, bg, height)) return false;
        paimon::glass::decorateButtonSprite(this);
        return true;
    }

    // skin changes (tabs, toggles) replace m_BGSprite and its plate.
    $override
    void updateBGImage(char const* filename) {
        ButtonSprite::updateBGImage(filename);
        paimon::glass::decorateButtonSprite(this);
    }
};

// icon buttons: ButtonSprite::create(topSprite, ...).
class $modify(PaimonGlassSpriteButtonSprite, ButtonSprite) {
    $override
    bool init(CCSprite* topSprite, int width, int minWidth, float height, float scale, bool absolute,
              char const* bgSprite, bool noScaleSpriteForBG, float unk) {
        if (!ButtonSprite::init(topSprite, width, minWidth, height, scale, absolute, bgSprite,
                                noScaleSpriteForBG, unk)) {
            return false;
        }
        paimon::glass::decorateButtonSprite(this);
        return true;
    }
};
