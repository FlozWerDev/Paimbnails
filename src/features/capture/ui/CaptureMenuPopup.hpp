#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

class ButtonSprite;

// Right-click screenshot menu. Closes on capture and delegates to CaptureOverlay.
class CaptureMenuPopup : public geode::Popup {
public:
    // Toggle: si ya hay un menu abierto, lo cierra; si no, abre uno nuevo.
    // (No se llama show() para no chocar con FLAlertLayer::show() virtual.)
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
    void onToggleHoldCtrl(cocos2d::CCObject* sender);
    void refreshHoldCtrlButton();
};
