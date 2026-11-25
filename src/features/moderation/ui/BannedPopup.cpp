#include "BannedPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SafeText.hpp"
#include "../../../ui/PaimonUI.hpp"
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/PopupManager.hpp>

using namespace geode::prelude;

namespace {
BannedPopup* g_activeBannedPopup = nullptr;
}

bool BannedPopup::init(std::string const& reason) {
    if (!PaimonPopup::init(340.f, 200.f)) return false;
    paimon::markDynamicPopup(this);
    g_activeBannedPopup = this;
    m_reason = paimon::text::sanitizeDisplayText(reason, 512);

    this->setTitle("Banned");

    // no way out except disabling the mod.
    if (m_closeBtn) m_closeBtn->setVisible(false);
    this->setKeypadEnabled(false);

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    CCSize insetSize = {content.width - 30.f, 118.f};
    auto inset = paimon::ui::makeInset(insetSize);
    inset->setPosition({cx - insetSize.width / 2.f, content.height - 32.f - insetSize.height});
    m_mainLayer->addChild(inset);

    std::string body = "You have been banned from using Paimbnails.";
    if (!m_reason.empty()) body += "\n\n<cy>Reason:</c> " + m_reason;
    body += "\n\nOnline features are blocked while this ban is active. "
        "Press the button below to disable the mod and restart the game.";

    auto desc = MDTextArea::create(body, {insetSize.width - 16.f, insetSize.height - 12.f});
    if (desc) {
        desc->setPosition({cx, content.height - 32.f - insetSize.height / 2.f});
        m_mainLayer->addChild(desc);
    }

    auto disableBtn = paimon::ui::makeButton("Disable Mod", [this] {
        this->onDisableMod(nullptr);
    }, paimon::ui::Btn::Red, 0.f, 0.8f);
    disableBtn->setPosition({cx, 32.f});
    m_buttonMenu->addChild(disableBtn);

    return true;
}

void BannedPopup::onExit() {
    if (g_activeBannedPopup == this) g_activeBannedPopup = nullptr;
    PaimonPopup::onExit();
}

void BannedPopup::onDisableMod(CCObject*) {
    auto result = Mod::get()->disable();
    if (!result) {
        PopupManager::get().alert("Error", result.unwrapErr()).showInstant();
        return;
    }
    geode::utils::game::restart(true);
}

BannedPopup* BannedPopup::create(std::string const& reason) {
    auto ret = new BannedPopup();
    if (ret && ret->init(reason)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

namespace paimon::ban {
    bool isBannedPopupActive() {
        return g_activeBannedPopup != nullptr;
    }

    void showBannedPopup(std::string const& reason) {
        if (isBannedPopupActive()) return;
        if (auto* popup = BannedPopup::create(reason)) {
            popup->show();
        }
    }
}
