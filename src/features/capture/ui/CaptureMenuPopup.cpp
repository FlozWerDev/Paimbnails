#include "CaptureMenuPopup.hpp"
#include "CaptureOverlay.hpp"
#include "../../volume-scroll/ui/ScrollKeybindsPopup.hpp"
#include "../../quick-hub/services/QuickHubManager.hpp"
#include "../../menu-physics/services/MenuPhysicsManager.hpp"
#include "../../smooth-scroll/ui/SmoothScrollConfigPopup.hpp"
#include "../../../utils/ActivePauseLayer.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../blur/PopupBlurService.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include "../../../ui/PaimonUI.hpp"

using namespace cocos2d;
using namespace geode::prelude;
using paimon::quickhub::QuickHubManager;

namespace {
constexpr float kPopupW   = 360.f;
constexpr float kPopupH   = 190.f;

// flag for popups opened from this menu: only those block right-click toggle.
std::string const& captureChildFlag() {
    static const std::string flag = Mod::get()->getID() + "/capture-menu-child";
    return flag;
}

void markCaptureChild(CCNode* node) {
    if (node) node->setUserFlag(captureChildFlag(), true);
}

ButtonSprite* makeHoldCtrlButtonSprite(bool enabled) {
    char const* bg = enabled ? "GJ_button_01.png" : "GJ_button_06.png";
    auto* spr = ButtonSprite::create(
        "Hold Ctrl", 72, true, "bigFont.fnt", bg, 18.f, 0.40f);
    return spr;
}
} // namespace

CaptureMenuPopup* CaptureMenuPopup::s_instance = nullptr;

void CaptureMenuPopup::toggle() {
    // only this menu's child popups block right-click toggle.
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            auto* alert = typeinfo_cast<FLAlertLayer*>(child);
            if (alert && alert != s_instance && alert->isVisible()
                && alert->getUserFlag(captureChildFlag())) return;
        }
    }
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
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    s_instance = this;

    this->setTitle("Captura de Pantalla");
    this->addInfoButton("Captura de Pantalla",
        "<cg>Capturar</c> toma una foto del nivel. <cy>Atajos</c> configura las teclas.\n"
        "<co>Hold Ctrl</c> abre el menu radial manteniendo Ctrl.\n"
        "<cr>Invertir Inputs</c>: clic derecho pasa a ser saltar.");

    auto content = m_mainLayer->getContentSize();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);

    const float panelTop = content.height - 34.f;
    const float panelBot = 14.f;
    const float panelH = panelTop - panelBot;
    const float gap = 10.f;
    const float sideMargin = 12.f;
    const float leftW = 150.f;
    const float rightW = content.width - sideMargin * 2 - leftW - gap;

    auto* actionPanel = paimon::ui::makePanel({leftW, panelH}, "Captura");
    actionPanel->setPosition({sideMargin, panelBot});
    m_mainLayer->addChild(actionPanel);

    auto* optionsPanel = paimon::ui::makePanel({rightW, panelH}, "Opciones");
    optionsPanel->setPosition({sideMargin + leftW + gap, panelBot});
    m_mainLayer->addChild(optionsPanel);

    const float leftCX = sideMargin + leftW * 0.5f;

    auto* captureBtnSpr = ButtonSprite::create("Capturar", "goldFont.fnt", "GJ_button_01.png", .65f);
    auto* captureBtn = CCMenuItemSpriteExtra::create(
        captureBtnSpr, this, menu_selector(CaptureMenuPopup::onCapture)
    );
    captureBtn->setPosition({leftCX, panelBot + panelH - 46.f});
    menu->addChild(captureBtn);

    auto* shortcutsBtnSpr = ButtonSprite::create("Atajos", "bigFont.fnt", "GJ_button_04.png", .52f);
    auto* shortcutsBtn = CCMenuItemSpriteExtra::create(
        shortcutsBtnSpr, this, menu_selector(CaptureMenuPopup::onOpenShortcuts)
    );
    shortcutsBtn->setPosition({leftCX, panelBot + panelH - 80.f});
    menu->addChild(shortcutsBtn);

    bool const holdCtrlOn = QuickHubManager::isHoldCtrlEnabled();
    m_holdCtrlBtnSpr = makeHoldCtrlButtonSprite(holdCtrlOn);
    auto* holdCtrlBtn = CCMenuItemSpriteExtra::create(
        m_holdCtrlBtnSpr, this, menu_selector(CaptureMenuPopup::onToggleHoldCtrl)
    );
    holdCtrlBtn->setPosition({leftCX, panelBot + panelH - 112.f});
    menu->addChild(holdCtrlBtn);

    const float rowX0 = sideMargin + leftW + gap;
    const float toggleX = rowX0 + rightW - 20.f;
    const float labelX = rowX0 + 12.f;
    float rowY = panelBot + panelH - 40.f;
    const float rowStep = 32.f;

    auto addLabel = [&](char const* text, float y) {
        auto* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setAnchorPoint({0.f, .5f});
        lbl->setPosition({labelX, y});
        lbl->limitLabelWidth(rightW - 56.f, .34f, .1f);
        m_mainLayer->addChild(lbl);
    };

    bool const invertOn = Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false);
    auto* invOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* invOn  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    invOff->setScale(.6f);
    invOn->setScale(.6f);
    auto* invertToggle = CCMenuItemToggler::create(
        invOff, invOn, this, menu_selector(CaptureMenuPopup::onToggleInvert)
    );
    invertToggle->toggle(invertOn);
    invertToggle->setPosition({toggleX, rowY});
    menu->addChild(invertToggle);
    addLabel("Invertir Inputs", rowY);
    rowY -= rowStep;

    bool const physicsOn = Mod::get()->getSettingValue<bool>("menu-physics-enable");
    auto* phyOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* phyOn  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    phyOff->setScale(.6f);
    phyOn->setScale(.6f);
    auto* physicsToggle = CCMenuItemToggler::create(
        phyOff, phyOn, this, menu_selector(CaptureMenuPopup::onTogglePhysics)
    );
    physicsToggle->toggle(physicsOn);
    physicsToggle->setPosition({toggleX, rowY});
    menu->addChild(physicsToggle);
    addLabel("Menu Physics", rowY);
    rowY -= rowStep;

    bool const smoothOn = Mod::get()->getSettingValue<bool>("smooth-scroll");
    auto* smoothOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* smoothOnSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    smoothOff->setScale(.6f);
    smoothOnSpr->setScale(.6f);
    auto* smoothToggle = CCMenuItemToggler::create(
        smoothOff, smoothOnSpr, this, menu_selector(CaptureMenuPopup::onToggleSmoothScroll)
    );
    smoothToggle->toggle(smoothOn);
    smoothToggle->setPosition({toggleX, rowY});
    menu->addChild(smoothToggle);
    addLabel("Smooth Scroll", rowY);

    auto* gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    if (gearSpr) {
        gearSpr->setScale(0.38f);
        auto* gearBtn = CCMenuItemSpriteExtra::create(
            gearSpr, this, menu_selector(CaptureMenuPopup::onOpenSmoothScrollConfig));
        gearBtn->setPosition({toggleX - 26.f, rowY});
        menu->addChild(gearBtn);
    }

    // opt-in to the mod's theme/animations/dynamic blur (dynamicpopuphook).
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
    if (paimon::isCaptureInProgress()) {
        PaimonNotify::create(
            Localization::get().getString("pause.capture_busy").c_str(),
            NotificationIcon::Info)->show();
        return;
    }
    // hide popup and drop only its blur; underneath popups keep theirs for the shot.
    this->setVisible(false);
    paimon::popupblur::cleanup(this);
    CaptureOverlay::show();
    this->removeFromParent();
}

void CaptureMenuPopup::onOpenShortcuts(CCObject*) {
    // queue on main thread to avoid modifying scene during touch dispatch.
    geode::Loader::get()->queueInMainThread([]() {
        if (auto* popup = paimon::volscroll::ScrollKeybindsPopup::create()) {
            markCaptureChild(popup);
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
    // apply live to scene visible after this popup closes.
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

void CaptureMenuPopup::onToggleSmoothScroll(CCObject*) {
    bool const now = !Mod::get()->getSettingValue<bool>("smooth-scroll");
    Mod::get()->setSettingValue<bool>("smooth-scroll", now);
    PaimonNotify::create(
        now ? "Smooth Scroll activado" : "Smooth Scroll desactivado",
        NotificationIcon::Info
    )->show();
}

void CaptureMenuPopup::onOpenSmoothScrollConfig(CCObject*) {
    geode::Loader::get()->queueInMainThread([]() {
        if (auto* popup = paimon::smoothscroll::SmoothScrollConfigPopup::create()) {
            markCaptureChild(popup);
            popup->show();
        }
    });
}

void CaptureMenuPopup::refreshHoldCtrlButton() {
    if (!m_holdCtrlBtnSpr) return;
    bool const enabled = QuickHubManager::isHoldCtrlEnabled();
    m_holdCtrlBtnSpr->updateBGImage(enabled ? "GJ_button_01.png" : "GJ_button_06.png");
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
