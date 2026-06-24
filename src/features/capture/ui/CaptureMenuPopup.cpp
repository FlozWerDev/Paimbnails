#include "CaptureMenuPopup.hpp"
#include "CaptureOverlay.hpp"
#include "../../volume-scroll/ui/ScrollKeybindsPopup.hpp"
#include "../../quick-hub/services/QuickHubManager.hpp"
#include "../../menu-physics/services/MenuPhysicsManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../blur/PopupBlurService.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace cocos2d;
using namespace geode::prelude;
using paimon::quickhub::QuickHubManager;

namespace {
constexpr float kPopupW   = 220.f;
constexpr float kPopupH   = 224.f;
constexpr float kCenterX  = kPopupW / 2.f;
constexpr float kCenterY  = 104.f;
constexpr float kCaptureY  = 70.f;
constexpr float kShortcutsY = 28.f;
constexpr float kHoldCtrlY = -14.f;
constexpr float kInvertY   = -52.f;
constexpr float kPhysicsY  = -84.f;

ButtonSprite* makeHoldCtrlButtonSprite(bool enabled) {
    char const* bg = enabled ? "GJ_button_01.png" : "GJ_button_06.png";
    auto* spr = ButtonSprite::create(
        "Hold Ctrl", 64, 0, 0.34f, true, "bigFont.fnt", bg, 0.f);
    if (spr) {
        spr->setScale(0.88f);
    }
    return spr;
}
} // namespace

CaptureMenuPopup* CaptureMenuPopup::s_instance = nullptr;

void CaptureMenuPopup::toggle() {
    if (s_instance) {
        s_instance->onClose(nullptr);
        return;
    }
    if (auto* popup = CaptureMenuPopup::create()) {
        popup->show();
    }
}

CaptureMenuPopup* CaptureMenuPopup::create() {
    auto* ret = new CaptureMenuPopup();
    if (ret && ret->initContents()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CaptureMenuPopup::initContents() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    s_instance = this;

    this->setTitle("Captura de Pantalla");

    // Central menu with relative button offsets
    auto* menu = CCMenu::create();
    menu->setPosition({kCenterX, kCenterY});
    m_mainLayer->addChild(menu);

    // Capture button
    auto* captureBtnSpr = ButtonSprite::create("Capturar", "goldFont.fnt", "GJ_button_01.png", .65f);
    auto* captureBtn = CCMenuItemSpriteExtra::create(
        captureBtnSpr, this, menu_selector(CaptureMenuPopup::onCapture)
    );
    captureBtn->setPosition({0.f, kCaptureY});
    menu->addChild(captureBtn);

    // Shortcuts button
    auto* shortcutsBtnSpr = ButtonSprite::create("Atajos", "bigFont.fnt", "GJ_button_04.png", .52f);
    auto* shortcutsBtn = CCMenuItemSpriteExtra::create(
        shortcutsBtnSpr, this, menu_selector(CaptureMenuPopup::onOpenShortcuts)
    );
    shortcutsBtn->setPosition({0.f, kShortcutsY});
    menu->addChild(shortcutsBtn);

    // Hold Ctrl toggle
    bool const holdCtrlOn = QuickHubManager::isHoldCtrlEnabled();
    m_holdCtrlBtnSpr = makeHoldCtrlButtonSprite(holdCtrlOn);
    auto* holdCtrlBtn = CCMenuItemSpriteExtra::create(
        m_holdCtrlBtnSpr, this, menu_selector(CaptureMenuPopup::onToggleHoldCtrl)
    );
    holdCtrlBtn->setPosition({0.f, kHoldCtrlY});
    menu->addChild(holdCtrlBtn);

    // Invert Inputs toggle (right-click = jump)
    bool const invertOn = Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false);
    auto* invOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* invOn  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    invOff->setScale(.6f);
    invOn->setScale(.6f);
    auto* invertToggle = CCMenuItemToggler::create(
        invOff, invOn, this, menu_selector(CaptureMenuPopup::onToggleInvert)
    );
    invertToggle->toggle(invertOn);
    invertToggle->setPosition({-78.f, kInvertY});
    menu->addChild(invertToggle);

    auto* invLabel = CCLabelBMFont::create("Invertir Inputs", "bigFont.fnt");
    invLabel->setAnchorPoint({0.f, .5f});
    invLabel->setPosition({kCenterX - 62.f, kCenterY + kInvertY});
    invLabel->limitLabelWidth(120.f, .32f, .1f);
    m_mainLayer->addChild(invLabel);

    // Menu Physics toggle
    bool const physicsOn = Mod::get()->getSettingValue<bool>("menu-physics-enable");
    auto* phyOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* phyOn  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    phyOff->setScale(.6f);
    phyOn->setScale(.6f);
    auto* physicsToggle = CCMenuItemToggler::create(
        phyOff, phyOn, this, menu_selector(CaptureMenuPopup::onTogglePhysics)
    );
    physicsToggle->toggle(physicsOn);
    physicsToggle->setPosition({-78.f, kPhysicsY});
    menu->addChild(physicsToggle);

    auto* phyLabel = CCLabelBMFont::create("Menu Physics", "bigFont.fnt");
    phyLabel->setAnchorPoint({0.f, .5f});
    phyLabel->setPosition({kCenterX - 62.f, kCenterY + kPhysicsY});
    phyLabel->limitLabelWidth(120.f, .32f, .1f);
    m_mainLayer->addChild(phyLabel);

    // Opt-in al tema/animaciones/blur dinamico del mod (DynamicPopupHook).
    paimon::markDynamicPopup(this);

    return true;
}

void CaptureMenuPopup::onExit() {
    geode::Popup::onExit();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void CaptureMenuPopup::onCapture(CCObject*) {
    // Hide popup and blur, then delegate to CaptureOverlay.
    this->setVisible(false);
    paimon::popupblur::cleanupAllActive(0.f);
    CaptureOverlay::show();
    this->removeFromParent();
}

void CaptureMenuPopup::onOpenShortcuts(CCObject*) {
    // Queue on main thread to avoid modifying scene during touch dispatch.
    geode::Loader::get()->queueInMainThread([]() {
        if (auto* popup = paimon::volscroll::ScrollKeybindsPopup::create()) {
            popup->show();
        }
    });
    this->onClose(nullptr);
}

void CaptureMenuPopup::onToggleInvert(CCObject*) {
    bool const now = !Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false);
    Mod::get()->setSavedValue<bool>("invert-mouse-inputs", now);
    PaimonNotify::create(
        now ? "Inputs invertidos: clic derecho = saltar" : "Inputs normales restaurados",
        NotificationIcon::Info
    )->show();
}

void CaptureMenuPopup::onTogglePhysics(CCObject*) {
    bool const now = !Mod::get()->getSettingValue<bool>("menu-physics-enable");
    Mod::get()->setSettingValue<bool>("menu-physics-enable", now);
    // Apply live to scene visible after this popup closes.
    if (now) {
        paimon::menuphysics::MenuPhysicsManager::get().applyToCurrentScene();
    } else {
        paimon::menuphysics::MenuPhysicsManager::get().clearFromCurrentScene();
    }
    PaimonNotify::create(
        now ? "Menu Physics activado" : "Menu Physics desactivado",
        NotificationIcon::Info
    )->show();
}

void CaptureMenuPopup::refreshHoldCtrlButton() {
    if (!m_holdCtrlBtnSpr) return;
    bool const enabled = QuickHubManager::isHoldCtrlEnabled();
    m_holdCtrlBtnSpr->updateBGImage(enabled ? "GJ_button_01.png" : "GJ_button_06.png");
    m_holdCtrlBtnSpr->setScale(0.88f);
}

void CaptureMenuPopup::onToggleHoldCtrl(CCObject*) {
    bool const now = !QuickHubManager::isHoldCtrlEnabled();
    QuickHubManager::setHoldCtrlEnabled(now);
    if (!now) {
        QuickHubManager::abortActiveHold();
    }
    refreshHoldCtrlButton();
    PaimonNotify::create(
        now ? "Hold Ctrl activado (menu radial)" : "Hold Ctrl desactivado",
        NotificationIcon::Info
    )->show();
}
