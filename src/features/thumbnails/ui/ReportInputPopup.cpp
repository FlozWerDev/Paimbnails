#include "ReportInputPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/Localization.hpp"
#include "../../emotes/ui/EmoteButton.hpp"
#include "../../emotes/ui/EmoteAutocomplete.hpp"

using namespace geode::prelude;
using namespace cocos2d;

bool ReportInputPopup::init(int levelID, geode::CopyableFunction<void(std::string)> callback) {
    if (!Popup::init(320.f, 160.f)) return false;

    m_levelID = levelID;
    m_callback = callback;

    this->setTitle(Localization::get().getString("report.title").c_str());

    auto contentSize = m_mainLayer->getContentSize();
    float cx = contentSize.width / 2.f;

    // level ID context
    auto idLabel = CCLabelBMFont::create(
        fmt::format("Level ID: {}", levelID).c_str(),
        "goldFont.fnt"
    );
    idLabel->setScale(0.35f);
    idLabel->setPosition({cx, contentSize.height - 48.f});
    idLabel->setColor({180, 180, 180});
    m_mainLayer->addChild(idLabel);

    // reason input
    m_textInput = geode::TextInput::create(280.f, Localization::get().getString("report.placeholder").c_str(), "chatFont.fnt");
    m_textInput->setCommonFilter(geode::CommonFilter::Any);
    m_textInput->setMaxCharCount(120);
    m_textInput->setPosition({cx, contentSize.height - 80.f});
    m_textInput->setScale(0.9f);
    m_mainLayer->addChild(m_textInput);

    // emote button
    {
        paimon::emotes::EmoteInputContext ctx;
        ctx.getText = [this]() -> std::string {
            if (!m_textInput) return "";
            return m_textInput->getString();
        };
        ctx.setText = [this](std::string const& text) {
            if (!m_textInput) return;
            m_textInput->setString(text);
        };
        ctx.charLimit = 120;
        auto emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        auto emoteMenu = CCMenu::create();
        emoteMenu->setID("emote-menu"_spr);
        emoteMenu->setPosition({contentSize.width - 18.f, contentSize.height - 80.f});
        emoteBtn->setScale(0.45f);
        emoteMenu->addChild(emoteBtn);
        m_mainLayer->addChild(emoteMenu, 5);
    }

    // Autocomplete
    {
        auto ac = paimon::emotes::EmoteAutocomplete::create(
            m_textInput->getInputNode(),
            [this](std::string const& newText) {
                if (m_textInput) m_textInput->setString(newText);
            }
        );
        ac->setPosition({cx - 60.f, contentSize.height - 54.f});
        m_mainLayer->addChild(ac, 100);
    }

    // send button
    auto sendSpr = ButtonSprite::create(
        Localization::get().getString("report.send").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    sendSpr->setScale(0.85f);
    auto sendBtn = CCMenuItemSpriteExtra::create(
        sendSpr, this, menu_selector(ReportInputPopup::onSend)
    );
    sendBtn->setID("send-report-btn"_spr);
    sendBtn->setPosition({cx, 26.f});
    m_buttonMenu->addChild(sendBtn);

    paimon::markDynamicPopup(this);
    return true;
}

void ReportInputPopup::onSend(CCObject*) {
    std::string reason = m_textInput->getString();
    if (reason.empty()) {
        PaimonNotify::create(Localization::get().getString("report.empty_reason").c_str(), NotificationIcon::Warning)->show();
        return;
    }

    if (m_callback) {
        m_callback(reason);
    }
    this->onClose(nullptr);
}

ReportInputPopup* ReportInputPopup::create(int levelID, geode::CopyableFunction<void(std::string)> callback) {
    auto ret = new ReportInputPopup();
    if (ret && ret->init(levelID, callback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
