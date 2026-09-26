#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

class ButtonSprite;

// Right-click screenshot menu. Closes on capture and delegates to CaptureOverlay.
class CaptureMenuPopup : public geode::Popup {
public:
    // Toggle: close the open menu or open a new one; not show(), which clashes with FLAlertLayer::show().
    static void toggle();

protected:
    bool initContents();
    void onExit() override;

private:
    static CaptureMenuPopup* create();
    static CaptureMenuPopup* s_instance;

    geode::Ref<ButtonSprite> m_holdCtrlBtnSpr = nullptr;

    void onCapture(cocos2d::CCObject* sender);
    void onOpenShortcuts(cocos2d::CCObject* sender);
    void onToggleInvert(cocos2d::CCObject* sender);
    void onTogglePhysics(cocos2d::CCObject* sender);
    void onToggleSmoothScroll(cocos2d::CCObject* sender);
    void onOpenSmoothScrollConfig(cocos2d::CCObject* sender);
    void onToggleHoldCtrl(cocos2d::CCObject* sender);
    void refreshHoldCtrlButton();
};
