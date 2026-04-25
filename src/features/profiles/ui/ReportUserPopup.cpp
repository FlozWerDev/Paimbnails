#include "ReportUserPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../emotes/ui/EmoteButton.hpp"
#include "../../emotes/ui/EmoteAutocomplete.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;
using namespace cocos2d;

bool ReportUserPopup::init(int accountID, std::string const& username) {
    if (!Popup::init(320.f, 160.f)) return false;

    m_reportedAccountID = accountID;
    m_reportedUsername = username;

    this->setTitle("Report User");

    auto contentSize = m_mainLayer->getContentSize();
    float cx = contentSize.width / 2.f;

    // Usuario reportado
    auto infoLabel = CCLabelBMFont::create(
        fmt::format("User: {}", username).c_str(),
        "goldFont.fnt"
    );
    infoLabel->setScale(0.4f);
    infoLabel->setPosition({cx, contentSize.height - 48.f});
    infoLabel->setColor({255, 120, 120});
    float maxW = 280.f;
    if (infoLabel->getScaledContentWidth() > maxW) {
        infoLabel->setScale(maxW / infoLabel->getContentWidth());
    }
    m_mainLayer->addChild(infoLabel);

    // Input de razon
    m_textInput = geode::TextInput::create(280.f, "Reason for reporting...", "chatFont.fnt");
    m_textInput->setCommonFilter(geode::CommonFilter::Any);
    m_textInput->setMaxCharCount(200);
    m_textInput->setPosition({cx, contentSize.height - 80.f});
    m_textInput->setScale(0.9f);
    m_mainLayer->addChild(m_textInput);

    // Boton de emote
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
        ctx.charLimit = 200;
        auto emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        auto emoteMenu = CCMenu::create();
        emoteMenu->setID("emote-menu"_spr);
        emoteMenu->setPosition({contentSize.width - 18.f, contentSize.height - 80.f});
        emoteBtn->setScale(0.45f);
        emoteMenu->addChild(emoteBtn);
        m_mainLayer->addChild(emoteMenu, 5);
    }

    // Autocompletar emotes
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

    // Boton de enviar
    auto sendSpr = ButtonSprite::create("Send", "goldFont.fnt", "GJ_button_06.png", 0.8f);
    sendSpr->setScale(0.85f);
    auto sendBtn = CCMenuItemSpriteExtra::create(sendSpr, this, menu_selector(ReportUserPopup::onSend));
    sendBtn->setID("send-user-report-btn"_spr);
    sendBtn->setPosition({cx, 26.f});
    m_buttonMenu->addChild(sendBtn);

    paimon::markDynamicPopup(this);
    return true;
}

void ReportUserPopup::onSend(CCObject*) {
    std::string reason = m_textInput ? m_textInput->getString() : "";
    if (reason.empty()) {
        PaimonNotify::create("Please provide a reason", NotificationIcon::Warning)->show();
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        PaimonNotify::create("You must be logged in", NotificationIcon::Error)->show();
        return;
    }

    std::string reporterUsername;
    if (auto gm = GameManager::get()) {
        reporterUsername = gm->m_playerName;
    }
    int reporterAccountID = accountManager->m_accountID;

    matjson::Value body = matjson::makeObject({
        {"reportedAccountID", m_reportedAccountID},
        {"reportedUsername", m_reportedUsername},
        {"note", reason},
        {"reporterUsername", reporterUsername},
        {"reporterAccountID", reporterAccountID}
    });

    WeakRef<ReportUserPopup> self = this;
    HttpClient::get().post("/api/report/user", body.dump(), [self](bool ok, std::string const& resp) {
        auto popup = self.lock();
        if (!popup) return;

        if (ok) {
            auto parsed = matjson::parse(resp);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();
                if (root["error"].isString()) {
                    PaimonNotify::create(root["error"].asString().unwrapOr("Error"), NotificationIcon::Error)->show();
                    return;
                }
            }
            PaimonNotify::create("Report submitted!", NotificationIcon::Success)->show();
            popup->onClose(nullptr);
        } else {
            std::string errorMsg = "Failed to send report";
            auto parsed = matjson::parse(resp);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();
                if (root["error"].isString()) {
                    errorMsg = root["error"].asString().unwrapOr(errorMsg);
                }
            }
            PaimonNotify::create(errorMsg.c_str(), NotificationIcon::Error)->show();
        }
    });
}

ReportUserPopup* ReportUserPopup::create(int accountID, std::string const& username) {
    auto ret = new ReportUserPopup();
    if (ret && ret->init(accountID, username)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
