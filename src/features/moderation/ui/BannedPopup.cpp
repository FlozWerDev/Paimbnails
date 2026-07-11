#include "BannedPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/PopupManager.hpp>

using namespace geode::prelude;

bool BannedPopup::init(std::string const& reason) {
    if (!Popup::init(340.f, 200.f)) return false;
    paimon::markDynamicPopup(this);
    m_reason = reason;

    this->setTitle("Banned");

    // No way out except disabling the mod.
    if (m_closeBtn) m_closeBtn->setVisible(false);
    this->setKeypadEnabled(false);

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    std::string body = "You have been banned from using Paimbnails.";
    if (!reason.empty()) body += "\n\n<cy>Reason:</c> " + reason;
    body += "\n\nThe mod has been disabled. Press the button below to confirm and restart the game.";

    auto desc = MDTextArea::create(body, {300.f, 115.f});
    if (desc) {
        desc->setPosition({cx, content.height - 85.f});
        m_mainLayer->addChild(desc);
    }

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    auto disableBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Disable Mod", "goldFont.fnt", "GJ_button_06.png", 0.8f),
        this,
        menu_selector(BannedPopup::onDisableMod)
    );
    disableBtn->setPosition({cx, 32.f});
    menu->addChild(disableBtn);

    return true;
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
    void showBannedPopup(std::string const& reason) {
        if (auto* popup = BannedPopup::create(reason)) {
            popup->show();
        }
    }
}
