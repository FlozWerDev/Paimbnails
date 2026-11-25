#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../services/AmbientDecorator.hpp"
#include "../services/AmbientRuntime.hpp"

#include <Geode/modify/AchievementBar.hpp>
#include <Geode/modify/ButtonSprite.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCLayer.hpp>
#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/GJDropDownLayer.hpp>
#include <Geode/modify/TextAlertPopup.hpp>
#include <Geode/ui/SceneEvent.hpp>

using namespace geode::prelude;

// Dynamic Ambient decorates all UI scenes, transitions, popups, dropdowns,
// toasts and buttons before Frame 0 draws, while staying completely inactive
// whenever Liquid Glass is enabled or on buttons customized by Button Skins.

$execute {
    geode::SceneEvent().listen(+[](cocos2d::CCScene* scene) -> bool {
        if (!paimon::isRuntimeShuttingDown() && paimon::ambient::AmbientRuntime::get().active()) {
            paimon::ambient::decorateScene(scene);
        }
        return false;
    }).leak();
}

class $modify(PaimonAmbientDirector, CCDirector) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "cocos2d::CCDirector::setNextScene");
    }

    $override
    void setNextScene() {
        CCDirector::setNextScene();
        if (paimon::isRuntimeShuttingDown() || !paimon::ambient::AmbientRuntime::get().active()) return;
        paimon::ambient::watchPopups();
    }
};

class $modify(PaimonAmbientLayerEnter, CCLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "cocos2d::CCLayer::onEnter");
    }

    $override
    void onEnter() {
        CCLayer::onEnter();
        if (paimon::isRuntimeShuttingDown() || !paimon::ambient::AmbientRuntime::get().active()) return;
        paimon::ambient::decorateEnteredLayer(this);
    }
};

class $modify(PaimonAmbientAlertLayer, FLAlertLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "FLAlertLayer::show");
    }

    $override
    void show() {
        FLAlertLayer::show();
        if (paimon::isRuntimeShuttingDown() || !paimon::ambient::AmbientRuntime::get().active()) return;
        paimon::ambient::decorateTree(this);
    }
};

class $modify(PaimonAmbientDropDownLayer, GJDropDownLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "GJDropDownLayer::showLayer");
    }

    $override
    void showLayer(bool instant) {
        GJDropDownLayer::showLayer(instant);
        if (paimon::isRuntimeShuttingDown() || !paimon::ambient::AmbientRuntime::get().active()) return;
        paimon::ambient::decorateDropDown(this);
    }
};

class $modify(PaimonAmbientAchievementBar, AchievementBar) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "AchievementBar::show");
    }

    $override
    bool init(char const* title, char const* desc, char const* icon, bool quest) {
        if (!AchievementBar::init(title, desc, icon, quest)) return false;
        if (!paimon::isRuntimeShuttingDown() && paimon::ambient::AmbientRuntime::get().active()) {
            paimon::ambient::decorateTree(this);
        }
        return true;
    }

    $override
    void show() {
        AchievementBar::show();
        if (paimon::isRuntimeShuttingDown() || !paimon::ambient::AmbientRuntime::get().active()) return;
        paimon::ambient::decorateTree(this);
    }
};

class $modify(PaimonAmbientTextAlert, TextAlertPopup) {
    $override
    bool init(std::string text, float delay, float scale, int opacity, std::string font) {
        if (!TextAlertPopup::init(std::move(text), delay, scale, opacity, std::move(font))) return false;
        if (!paimon::isRuntimeShuttingDown() && paimon::ambient::AmbientRuntime::get().active()) {
            paimon::ambient::decorateTree(this);
        }
        return true;
    }
};

class $modify(PaimonAmbientButtonSprite, ButtonSprite) {
    $override
    bool init(char const* caption, int width, int minWidth, float scale, bool absolute,
              char const* font, char const* bg, float height) {
        if (!ButtonSprite::init(caption, width, minWidth, scale, absolute, font, bg, height)) return false;
        paimon::ambient::decorateButtonSprite(this);
        return true;
    }

    $override
    void updateBGImage(char const* filename) {
        bool const alreadyStyled = this->getUserObject("paimon-ambient-decorated"_spr) != nullptr;
        ButtonSprite::updateBGImage(filename);
        paimon::ambient::decorateButtonSprite(this, alreadyStyled);
    }
};

class $modify(PaimonAmbientSpriteButtonSprite, ButtonSprite) {
    $override
    bool init(CCSprite* topSprite, int width, int minWidth, float height, float scale, bool absolute,
              char const* bgSprite, bool noScaleSpriteForBG, float unk) {
        if (!ButtonSprite::init(topSprite, width, minWidth, height, scale, absolute, bgSprite,
                                noScaleSpriteForBG, unk)) {
            return false;
        }
        paimon::ambient::decorateButtonSprite(this);
        return true;
    }
};
