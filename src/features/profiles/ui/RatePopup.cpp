#include "RatePopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../foryou/services/ForYouTracker.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;

RatePopup* RatePopup::create(int levelID, std::string thumbnailId) {
    auto ret = new RatePopup();
    if (ret && ret->init(levelID, thumbnailId)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RatePopup::init(int levelID, std::string thumbnailId) {
    if (!Popup::init(320.f, 220.f)) return false;

    m_levelID = levelID;
    m_thumbnailId = thumbnailId;

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // Titulo
    this->setTitle("Rate Thumbnail");

    // Panel de promedio
    auto avgPanel = paimon::SpriteHelper::createDarkPanel(160.f, 44.f, 140, 6.f);
    avgPanel->setPosition({centerX - 80.f, contentSize.height - 62.f - 22.f});
    m_mainLayer->addChild(avgPanel, 1);

    if (auto starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        starIcon->setScale(0.5f);
        starIcon->setPosition({centerX - 50.f, contentSize.height - 62.f});
        m_mainLayer->addChild(starIcon, 2);
    }

    m_averageLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_averageLabel->setScale(0.42f);
    m_averageLabel->setPosition({centerX - 10.f, contentSize.height - 59.f});
    m_averageLabel->setColor({255, 215, 0});
    m_averageLabel->setID("average-label"_spr);
    m_mainLayer->addChild(m_averageLabel, 2);

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.5f);
    m_countLabel->setPosition({centerX + 40.f, contentSize.height - 63.f});
    m_countLabel->setColor({180, 180, 180});
    m_countLabel->setID("count-label"_spr);
    m_mainLayer->addChild(m_countLabel, 2);

    // Separador
    auto separator = paimon::SpriteHelper::createDarkPanel(240.f, 1.5f, 80, 0.f);
    separator->setPosition({centerX - 120.f, contentSize.height - 90.f - 0.75f});
    m_mainLayer->addChild(separator, 1);

    // Fondo de estrellas
    auto starPanel = paimon::SpriteHelper::createDarkPanel(220.f, 50.f, 100, 6.f);
    starPanel->setPosition({centerX - 110.f, contentSize.height - 120.f - 25.f});
    m_mainLayer->addChild(starPanel, 1);

    // Botones de estrellas
    auto starMenu = CCMenu::create();
    starMenu->setID("stars-menu"_spr);
    starMenu->setPosition({centerX, contentSize.height - 120.f});
    m_mainLayer->addChild(starMenu, 2);

    for (int i = 1; i <= 5; i++) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        if (!spr) spr = CCSprite::create();
        spr->setColor({100, 100, 100});
        spr->setScale(0.8f);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RatePopup::onStar));
        btn->setTag(i);
        float col = (i - 3.f);  // -2, -1, 0, 1, 2
        btn->setPosition({col * 38.f, 0.f});
        starMenu->addChild(btn);
        m_starBtns.push_back(btn);
    }

    // Etiqueta de calificacion
    m_ratingLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_ratingLabel->setScale(0.3f);
    m_ratingLabel->setPosition({centerX, contentSize.height - 152.f});
    m_ratingLabel->setColor({255, 255, 255});
    m_ratingLabel->setID("rating-label"_spr);
    m_mainLayer->addChild(m_ratingLabel, 2);

    // Boton de enviar
    auto submitMenu = CCMenu::create();
    submitMenu->setID("submit-menu"_spr);
    submitMenu->setPosition({centerX, contentSize.height - 185.f});
    m_mainLayer->addChild(submitMenu, 2);

    auto submitSpr = ButtonSprite::create("Submit", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto submitBtn = CCMenuItemSpriteExtra::create(submitSpr, this, menu_selector(RatePopup::onSubmit));
    submitBtn->setID("submit-btn"_spr);
    submitBtn->setPosition({0, 0});
    submitMenu->addChild(submitBtn);

    loadExistingRating();

    paimon::markDynamicPopup(this);
    return true;
}

void RatePopup::onStar(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_rating = btn->getTag();
    updateStarVisuals();
}

void RatePopup::updateStarVisuals() {
    for (int i = 0; i < (int)m_starBtns.size(); i++) {
        auto b = m_starBtns[i];
        if (auto spr = typeinfo_cast<CCSprite*>(b->getNormalImage())) {
            if (i < m_rating) {
                spr->setColor({255, 255, 50});
                spr->setScale(0.85f);
            } else {
                spr->setColor({100, 100, 100});
                spr->setScale(0.8f);
            }
        }
    }
    if (m_ratingLabel) {
        if (m_rating > 0) {
            m_ratingLabel->setString(fmt::format("{}/5", m_rating).c_str());
        } else {
            m_ratingLabel->setString("");
        }
    }
}

void RatePopup::loadExistingRating() {
    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    WeakRef<RatePopup> self = this;
    ThumbnailAPI::get().getRating(m_levelID, username, m_thumbnailId, [self](bool ok, float average, int count, int userVote) {
        auto popup = self.lock();
        if (!popup) return;

        if (ok) {
            if (popup->m_averageLabel) {
                popup->m_averageLabel->setString(fmt::format("{:.1f}", average).c_str());
            }
            if (popup->m_countLabel) {
                if (count == 0) {
                    popup->m_countLabel->setString("No ratings");
                } else {
                    popup->m_countLabel->setString(fmt::format("({} vote{})", count, count == 1 ? "" : "s").c_str());
                }
            }
            if (userVote > 0 && userVote <= 5) {
                popup->m_rating = userVote;
                popup->updateStarVisuals();
            }
        } else {
            if (popup->m_averageLabel) popup->m_averageLabel->setString("0.0");
            if (popup->m_countLabel) popup->m_countLabel->setString("No ratings");
        }
    });
}

void RatePopup::onSubmit(CCObject* sender) {
    if (m_rating == 0) {
        PaimonNotify::create("Please select a rating", NotificationIcon::Error)->show();
        return;
    }
    
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        PaimonNotify::create("You must be logged in to vote", NotificationIcon::Error)->show();
        return;
    }

    std::string username = "";
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }
    
    // Cargando
    auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    spinner->show(m_mainLayer, 100);

    // Desactiva el boton para evitar spam
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (btn) btn->setEnabled(false);
    
    // Referencia debil para async
    WeakRef<RatePopup> self = this;
    Ref<PaimonLoadingOverlay> spinnerRef = spinner;
    int trackedLevelID = m_levelID;
    int trackedRating = m_rating;
    ThumbnailAPI::get().submitVote(m_levelID, m_rating, username, m_thumbnailId, [self, spinnerRef, btn, trackedLevelID, trackedRating](bool success, std::string const& msg) {
        if (auto popup = self.lock()) {
            if (spinnerRef) spinnerRef->dismiss();
            
            if (success) {
                paimon::foryou::ForYouTracker::get().onThumbnailRated(trackedLevelID, trackedRating);
                PaimonNotify::create("Rating submitted!", NotificationIcon::Success)->show();
                if (popup->m_onRateCallback) {
                    popup->m_onRateCallback();
                }
                popup->onClose(nullptr);
            } else {
                if (btn) btn->setEnabled(true);
                // Mensaje del servidor si existe
                std::string errorMsg = "Failed to submit rating";
                if (!msg.empty()) {
                    errorMsg += ": " + msg;
                }
                PaimonNotify::create(errorMsg.c_str(), NotificationIcon::Error)->show();
            }
        }
    });
}
