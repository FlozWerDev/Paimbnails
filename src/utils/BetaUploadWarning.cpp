#include "BetaUploadWarning.hpp"
#include "../core/Settings.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/MDTextArea.hpp>

using namespace geode::prelude;

bool BetaUploadWarningPopup::init(std::function<void()> onProceed) {
    if (!Popup::init(320.f, 180.f)) return false;
    m_onProceed = std::move(onProceed);

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    this->setTitle("Beta Notice");

    auto desc = MDTextArea::create(
        "Our servers are currently in beta. Thumbnails, profile backgrounds, and banners may take some time to be verified and appear publicly.",
        {280.f, 90.f}
    );
    if (desc) {
        desc->setPosition({cx, content.height - 70.f});
        m_mainLayer->addChild(desc);
    }

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    auto acceptBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Accept", "goldFont.fnt", "GJ_button_01.png", 0.7f),
        this,
        menu_selector(BetaUploadWarningPopup::onAccept)
    );
    acceptBtn->setPosition({cx - 80.f, 35.f});
    menu->addChild(acceptBtn);

    auto dismissBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Don't show again", "goldFont.fnt", "GJ_button_06.png", 0.55f),
        this,
        menu_selector(BetaUploadWarningPopup::onDismissForever)
    );
    dismissBtn->setPosition({cx + 80.f, 35.f});
    menu->addChild(dismissBtn);

    return true;
}

void BetaUploadWarningPopup::onAccept(CCObject*) {
    if (m_onProceed) m_onProceed();
    this->onClose(nullptr);
}

void BetaUploadWarningPopup::onDismissForever(CCObject*) {
    Mod::get()->setSavedValue<bool>("beta-upload-warning-dismissed", true);
    if (m_onProceed) m_onProceed();
    this->onClose(nullptr);
}

BetaUploadWarningPopup* BetaUploadWarningPopup::create(std::function<void()> onProceed) {
    auto ret = new BetaUploadWarningPopup();
    if (ret && ret->init(std::move(onProceed))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

namespace paimon {
    void showBetaUploadWarningIfNeeded(std::function<void()> onProceed) {
        if (paimon::settings::moderation::isVerifiedModerator() ||
            paimon::settings::moderation::isVerifiedAdmin() ||
            paimon::settings::moderation::isVerifiedVip() ||
            Mod::get()->getSavedValue<bool>("beta-upload-warning-dismissed", false)) {
            if (onProceed) onProceed();
            return;
        }
        if (auto popup = BetaUploadWarningPopup::create(std::move(onProceed))) {
            popup->show();
        } else {
            if (onProceed) onProceed();
        }
    }
}
