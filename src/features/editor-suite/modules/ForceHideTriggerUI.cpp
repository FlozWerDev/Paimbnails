// Dim trigger popup chrome while dragging a slider so the level stays readable.

#include "../EditorModule.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/modify/SliderTouchLogic.hpp>

using namespace geode::prelude;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-hide-trigger-ui"); }

FLAlertLayer* findOpenPopup() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return nullptr;
    return scene->getChildByType<FLAlertLayer>(0);
}

void setPopupDim(bool dim) {
    auto* popup = findOpenPopup();
    if (!popup || !popup->m_mainLayer) return;
    GLubyte op = dim ? 55 : 255;
    // CCLayer has no setOpacity — dim visual children only
    for (auto* c : CCArrayExt<CCNode*>(popup->m_mainLayer->getChildren())) {
        if (auto* spr = typeinfo_cast<CCSprite*>(c)) spr->setOpacity(op);
        if (auto* s9 = typeinfo_cast<CCScale9Sprite*>(c)) s9->setOpacity(op);
        if (auto* col = typeinfo_cast<CCLayerColor*>(c)) col->setOpacity(op);
    }
}
}

class $modify(PaimonHideTriggerSlider, SliderTouchLogic) {
    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        auto r = SliderTouchLogic::ccTouchBegan(touch, event);
        if (on() && r) setPopupDim(true);
        return r;
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchEnded(touch, event);
        if (on()) setPopupDim(false);
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        SliderTouchLogic::ccTouchCancelled(touch, event);
        if (on()) setPopupDim(false);
    }
};
