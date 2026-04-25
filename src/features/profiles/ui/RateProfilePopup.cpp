#include "RateProfilePopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "ReportUserPopup.hpp"
#include "ProfileReviewsPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../emotes/ui/EmoteButton.hpp"
#include "../../emotes/ui/EmoteAutocomplete.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

RateProfilePopup* RateProfilePopup::create(int accountID, std::string const& targetUsername) {
    auto ret = new RateProfilePopup();
    if (ret && ret->init(accountID, targetUsername)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RateProfilePopup::init(int accountID, std::string const& targetUsername) {
    if (!Popup::init(360.f, 280.f)) return false;

    m_accountID = accountID;
    m_targetUsername = targetUsername;

    this->setTitle("Rate Profile");

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // Panel de promedio
    auto avgPanel = paimon::SpriteHelper::createDarkPanel(200.f, 50.f, 100, 8.f);
    avgPanel->setPosition({centerX - 100.f, contentSize.height - 96.f});
    m_mainLayer->addChild(avgPanel, 1);

    // Icono de estrella
    if (auto starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        starIcon->setScale(0.6f);
        starIcon->setPosition({centerX - 55.f, contentSize.height - 66.f});
        starIcon->setColor({255, 215, 0});
        m_mainLayer->addChild(starIcon, 2);
    }

    m_averageLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_averageLabel->setScale(0.55f);
    m_averageLabel->setPosition({centerX - 10.f, contentSize.height - 64.f});
    m_averageLabel->setColor({255, 215, 0});
    m_averageLabel->setID("average-label"_spr);
    m_mainLayer->addChild(m_averageLabel, 2);

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.55f);
    m_countLabel->setPosition({centerX, contentSize.height - 84.f});
    m_countLabel->setColor({180, 180, 180});
    m_countLabel->setID("count-label"_spr);
    m_mainLayer->addChild(m_countLabel, 2);

    // Spinner de carga
    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 20.f);
    m_loadingSpinner->show(m_mainLayer, 3);

    // Separador
    auto separator = paimon::SpriteHelper::createDarkPanel(280.f, 1.5f, 60, 0.f);
    separator->setPosition({centerX - 140.f, contentSize.height - 104.f});
    m_mainLayer->addChild(separator, 1);

    // Tu calificacion
    auto yourRatingLabel = CCLabelBMFont::create("Your Rating", "bigFont.fnt");
    yourRatingLabel->setScale(0.35f);
    yourRatingLabel->setPosition({centerX, contentSize.height - 116.f});
    yourRatingLabel->setColor({200, 200, 200});
    m_mainLayer->addChild(yourRatingLabel, 2);

    // Panel de botones de estrella
    auto starPanel = paimon::SpriteHelper::createDarkPanel(240.f, 44.f, 80, 8.f);
    starPanel->setPosition({centerX - 120.f, contentSize.height - 152.f});
    m_mainLayer->addChild(starPanel, 1);

    // Resalta la estrella seleccionada
    m_starHighlight = CCNode::create();
    m_starHighlight->setPosition({centerX, contentSize.height - 130.f});
    m_starHighlight->setVisible(false);
    m_mainLayer->addChild(m_starHighlight, 1);

    auto starMenu = CCMenu::create();
    starMenu->setID("stars-menu"_spr);
    starMenu->setPosition({centerX, contentSize.height - 130.f});
    m_mainLayer->addChild(starMenu, 2);

    for (int i = 1; i <= 5; i++) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starBtn_001.png");
        if (!spr) spr = CCSprite::create();
        spr->setColor({80, 80, 80});
        spr->setScale(0.85f);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RateProfilePopup::onStar));
        btn->setTag(i);

        float col = (i - 3.f);
        btn->setPosition({col * 40.f, 0.f});

        starMenu->addChild(btn);
        m_starBtns.push_back(btn);
    }

    // Etiqueta de calificacion seleccionada
    m_selectedLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_selectedLabel->setScale(0.3f);
    m_selectedLabel->setPosition({centerX, contentSize.height - 158.f});
    m_selectedLabel->setColor({255, 255, 255});
    m_selectedLabel->setID("selected-label"_spr);
    m_mainLayer->addChild(m_selectedLabel, 2);

    // Input de mensaje
    m_messageInput = TextInput::create(290.f, "Leave a comment (optional)", "chatFont.fnt");
    m_messageInput->setID("message-input"_spr);
    m_messageInput->setPosition({centerX, contentSize.height - 186.f});
    m_messageInput->setMaxCharCount(150);
    m_messageInput->setCommonFilter(CommonFilter::Any);
    m_messageInput->setScale(0.85f);
    m_mainLayer->addChild(m_messageInput, 2);

    // Boton de emote
    {
        paimon::emotes::EmoteInputContext ctx;
        ctx.getText = [this]() -> std::string {
            if (!m_messageInput) return "";
            return m_messageInput->getString();
        };
        ctx.setText = [this](std::string const& text) {
            if (!m_messageInput) return;
            m_messageInput->setString(text);
        };
        ctx.charLimit = 150;
        auto emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        auto emoteMenu = CCMenu::create();
        emoteMenu->setID("emote-menu"_spr);
        emoteMenu->setPosition({contentSize.width - 25.f, contentSize.height - 186.f});
        emoteBtn->setScale(0.5f);
        emoteMenu->addChild(emoteBtn);
        m_mainLayer->addChild(emoteMenu, 5);
    }

    // Autocompletar emotes
    {
        auto ac = paimon::emotes::EmoteAutocomplete::create(
            m_messageInput->getInputNode(),
            [this](std::string const& newText) {
                if (m_messageInput) m_messageInput->setString(newText);
            }
        );
        ac->setPosition({centerX - 60.f, contentSize.height - 160.f});
        m_mainLayer->addChild(ac, 100);
    }

    // Botones inferiores
    auto bottomMenu = CCMenu::create();
    bottomMenu->setID("bottom-menu"_spr);
    bottomMenu->setPosition({centerX, contentSize.height - 224.f});
    bottomMenu->setLayout(
        RowLayout::create()
            ->setGap(12.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_mainLayer->addChild(bottomMenu, 2);

    // Boton de enviar
    auto submitSpr = ButtonSprite::create("Submit", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    submitSpr->setScale(0.9f);
    auto submitBtn = CCMenuItemSpriteExtra::create(submitSpr, this, menu_selector(RateProfilePopup::onSubmit));
    submitBtn->setID("submit-btn"_spr);
    bottomMenu->addChild(submitBtn);

    // Boton de ver resenas
    auto reviewsSpr = ButtonSprite::create("Reviews", "bigFont.fnt", "GJ_button_04.png", 0.7f);
    reviewsSpr->setScale(0.7f);
    auto reviewsBtn = CCMenuItemSpriteExtra::create(reviewsSpr, this, menu_selector(RateProfilePopup::onViewReviews));
    reviewsBtn->setID("reviews-btn"_spr);
    bottomMenu->addChild(reviewsBtn);

    bottomMenu->updateLayout();

    // Boton de reportar
    auto reportMenu = CCMenu::create();
    reportMenu->setID("report-menu"_spr);
    reportMenu->setPosition({contentSize.width - 30.f, 22.f});
    m_mainLayer->addChild(reportMenu, 2);

    auto reportSpr = ButtonSprite::create("Report", "bigFont.fnt", "GJ_button_06.png", 0.5f);
    reportSpr->setScale(0.55f);
    auto reportBtn = CCMenuItemSpriteExtra::create(reportSpr, this, menu_selector(RateProfilePopup::onReport));
    reportBtn->setID("report-btn"_spr);
    reportBtn->setPosition({0.f, 0.f});
    reportMenu->addChild(reportBtn);

    // Carga la calificacion existente
    loadExistingRating();

    paimon::markDynamicPopup(this);
    return true;
}

void RateProfilePopup::onStar(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_rating = btn->getTag();
    updateStarVisuals();

    // Animacion de rebote en la estrella
    btn->setScale(1.3f);
    btn->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 1.0f)));
}

void RateProfilePopup::updateStarVisuals() {
    static const char* ratingTexts[] = {
        "", "Terrible", "Bad", "Okay", "Good", "Amazing!"
    };

    for (int i = 0; i < (int)m_starBtns.size(); i++) {
        auto b = m_starBtns[i];
        if (auto spr = typeinfo_cast<CCSprite*>(b->getNormalImage())) {
            if (i < m_rating) {
                spr->setColor({255, 255, 50});
                spr->setScale(0.85f);
            } else {
                spr->setColor({80, 80, 80});
                spr->setScale(0.85f);
            }
        }
    }
    if (m_selectedLabel) {
        if (m_rating > 0 && m_rating <= 5) {
            m_selectedLabel->setString(fmt::format("{}/5 - {}", m_rating, ratingTexts[m_rating]).c_str());
        } else {
            m_selectedLabel->setString("");
        }
    }
}

void RateProfilePopup::loadExistingRating() {
    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    std::string endpoint = fmt::format(
        "/api/profile-ratings/{}?username={}",
        m_accountID,
        HttpClient::encodeQueryParam(username)
    );

    WeakRef<RateProfilePopup> self = this;
    HttpClient::get().get(endpoint, [self](bool ok, std::string const& resp) {
        auto popup = self.lock();
        if (!popup) return;

        // Remove loading spinner
        if (popup->m_loadingSpinner) {
            popup->m_loadingSpinner->dismiss();
            popup->m_loadingSpinner = nullptr;
        }

        if (!ok) {
            if (popup->m_countLabel) popup->m_countLabel->setString("No ratings yet");
            if (popup->m_averageLabel) popup->m_averageLabel->setString("0.0");
            return;
        }

        auto parsed = matjson::parse(resp);
        if (!parsed.isOk()) return;
        auto root = parsed.unwrap();

        float avg = 0.f;
        int count = 0;

        if (root["average"].isNumber()) avg = static_cast<float>(root["average"].asDouble().unwrapOr(0.0));
        if (root["count"].isNumber()) count = static_cast<int>(root["count"].asInt().unwrapOr(0));

        popup->m_currentAverage = avg;
        popup->m_totalVotes = count;

        if (popup->m_averageLabel) {
            popup->m_averageLabel->setString(fmt::format("{:.1f}/5", avg).c_str());
            // Anima la aparicion del promedio
            popup->m_averageLabel->setScale(0.0f);
            popup->m_averageLabel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 0.55f)));
        }
        if (popup->m_countLabel) {
            if (count == 0) {
                popup->m_countLabel->setString("No ratings yet");
            } else {
                popup->m_countLabel->setString(fmt::format("{} rating{}", count, count == 1 ? "" : "s").c_str());
            }
        }

        // Restaura el voto existente del usuario
        if (root["userVote"].isObject()) {
            auto uv = root["userVote"];
            if (uv["stars"].isNumber()) {
                popup->m_rating = static_cast<int>(uv["stars"].asInt().unwrapOr(0));
                popup->updateStarVisuals();
            }
            if (uv["message"].isString() && popup->m_messageInput) {
                popup->m_messageInput->setString(uv["message"].asString().unwrapOr(""));
            }
        }
    });
}

void RateProfilePopup::onSubmit(CCObject* sender) {
    if (m_rating == 0) {
        PaimonNotify::create("Select a rating first", NotificationIcon::Error)->show();
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        PaimonNotify::create("You must be logged in to rate", NotificationIcon::Error)->show();
        return;
    }

    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    // Cargando
    auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    spinner->show(m_mainLayer, 100);

    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (btn) btn->setEnabled(false);

    std::string message = m_messageInput ? m_messageInput->getString() : "";

    // Construye el JSON del cuerpo
    matjson::Value bodyObj = matjson::makeObject({
        {"accountID", m_accountID},
        {"stars", m_rating},
        {"username", username},
        {"message", message}
    });
    auto body = bodyObj.dump();

    WeakRef<RateProfilePopup> self = this;
    Ref<PaimonLoadingOverlay> spinnerRef = spinner;
    HttpClient::get().post("/api/profile-ratings/vote", body, [self, spinnerRef, btn](bool success, std::string const& msg) {
        auto popup = self.lock();
        if (!popup) return;

        if (spinnerRef) spinnerRef->dismiss();

        if (success) {
            auto parsed = matjson::parse(msg);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();
                if (root["error"].isString()) {
                    if (btn) btn->setEnabled(true);
                    PaimonNotify::create(root["error"].asString().unwrapOr("Unknown error"), NotificationIcon::Error)->show();
                    return;
                }
                bool updated = false;
                if (root["updated"].isBool()) updated = root["updated"].asBool().unwrapOr(false);
                if (updated) {
                    PaimonNotify::create("Rating updated!", NotificationIcon::Success)->show();
                } else {
                    PaimonNotify::create("Rating submitted!", NotificationIcon::Success)->show();
                }
                popup->onClose(nullptr);
                return;
            }

            PaimonNotify::create("Rating submitted!", NotificationIcon::Success)->show();
            popup->onClose(nullptr);
        } else {
            if (btn) btn->setEnabled(true);
            std::string errorMsg = "Failed to submit rating";
            if (!msg.empty()) {
                auto parsed = matjson::parse(msg);
                if (parsed.isOk()) {
                    auto root = parsed.unwrap();
                    if (root["error"].isString()) {
                        errorMsg = root["error"].asString().unwrapOr(errorMsg);
                    }
                } else {
                    errorMsg += ": " + msg;
                }
            }
            PaimonNotify::create(errorMsg.c_str(), NotificationIcon::Error)->show();
        }
    });
}

void RateProfilePopup::onReport(CCObject* sender) {
    auto popup = ReportUserPopup::create(m_accountID, m_targetUsername);
    if (popup) popup->show();
}

void RateProfilePopup::onViewReviews(CCObject* sender) {
    auto popup = ProfileReviewsPopup::create(m_accountID);
    if (popup) popup->show();
}
