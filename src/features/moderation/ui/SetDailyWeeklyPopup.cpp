#include "SetDailyWeeklyPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

bool SetDailyWeeklyPopup::init(int levelID) {
    m_levelID = levelID;
    
    // inicia popup con tamano
    if (!Popup::init(300.f, 220.f)) return false;

    this->setTitle("Set Daily/Weekly");

    // no tocar m_buttonMenu (tiene X). menu separado para contenido.
    
    auto actionMenu = CCMenu::create();
    actionMenu->setPosition(this->getContentSize() / 2);
    actionMenu->setContentSize({ 200.f, 160.f }); // ancho, alto contenedor
    actionMenu->ignoreAnchorPointForPosition(false);
    
    // columnlayout
    actionMenu->setLayout(
        ColumnLayout::create()
            ->setGap(10.f)
            ->setAxisReverse(true) // de arriba a abajo
    );
    
    this->addChild(actionMenu);

    // btn daily
    // cast explicito a ccobject* por si acaso, aunque innecesario usualmente
    auto dailyBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set Daily", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onSetDaily)
    );
    dailyBtn->setID("set-daily-btn"_spr);
    actionMenu->addChild(dailyBtn);

    // btn weekly
    auto weeklyBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set Weekly", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onSetWeekly)
    );
    weeklyBtn->setID("set-weekly-btn"_spr);
    actionMenu->addChild(weeklyBtn);
    
    // btn unset
    auto unsetBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Unset", 0, false, "goldFont.fnt", "GJ_button_06.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onUnset)
    );
    unsetBtn->setID("unset-btn"_spr);
    unsetBtn->setScale(0.8f);
    actionMenu->addChild(unsetBtn);

    // layout menu
    actionMenu->updateLayout();

    paimon::markDynamicPopup(this);
    return true;
}

SetDailyWeeklyPopup* SetDailyWeeklyPopup::create(int levelID) {
    auto ret = new SetDailyWeeklyPopup();
    if (ret && ret->init(levelID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void SetDailyWeeklyPopup::onSetDaily(CCObject* sender) {
    WeakRef<SetDailyWeeklyPopup> self = this;
    createQuickPopup(
        "Confirm",
        "Set this level as <cy>Daily</c>?",
        "Cancel", "Set",
        [self](FLAlertLayer*, bool btn2) {
            if (btn2) {
                auto popup = self.lock();
                if (!popup) return;
                auto gm = GameManager::get();
                auto* accountManager = GJAccountManager::get();
                if (!gm || !accountManager) {
                    PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
                    return;
                }
                std::string username = gm->m_playerName;
                int accountID = accountManager->m_accountID;

                matjson::Value json = matjson::makeObject({
                    {"levelID", popup->m_levelID},
                    {"username", username},
                    {"accountID", accountID}
                });
                
                auto overlay = PaimonLoadingOverlay::create("Setting daily...");
                overlay->show(popup->m_mainLayer, 200);
                Ref<PaimonLoadingOverlay> loadingRef = overlay;
                
                HttpClient::get().post("/api/daily/set", json.dump(), [self, loadingRef](bool success, std::string const& msg) {
                    if (loadingRef) loadingRef->dismiss();
                    if (auto popup = self.lock()) {
                        if (success) {
                            PaimonNotify::create("Daily set successfully", NotificationIcon::Success)->show();
                            popup->onClose(nullptr);
                        } else {
                            PaimonNotify::create("Failed to set daily: " + msg, NotificationIcon::Error)->show();
                        }
                    }
                });
            }
        }
    );
}

void SetDailyWeeklyPopup::onSetWeekly(CCObject* sender) {
    WeakRef<SetDailyWeeklyPopup> self = this;
    createQuickPopup(
        "Confirm",
        "Set this level as <cy>Weekly</c>?",
        "Cancel", "Set",
        [self](FLAlertLayer*, bool btn2) {
            if (btn2) {
                auto popup = self.lock();
                if (!popup) return;
                auto gm = GameManager::get();
                auto* accountManager = GJAccountManager::get();
                if (!gm || !accountManager) {
                    PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
                    return;
                }
                std::string username = gm->m_playerName;
                int accountID = accountManager->m_accountID;

                matjson::Value json = matjson::makeObject({
                    {"levelID", popup->m_levelID},
                    {"username", username},
                    {"accountID", accountID}
                });
                
                auto overlay = PaimonLoadingOverlay::create("Setting weekly...");
                overlay->show(popup->m_mainLayer, 200);
                Ref<PaimonLoadingOverlay> loadingRef = overlay;

                HttpClient::get().post("/api/weekly/set", json.dump(), [self, loadingRef](bool success, std::string const& msg) {
                    if (loadingRef) loadingRef->dismiss();
                    if (auto popup = self.lock()) {
                        if (success) {
                            PaimonNotify::create("Weekly set successfully", NotificationIcon::Success)->show();
                            popup->onClose(nullptr);
                        } else {
                            PaimonNotify::create("Failed to set weekly: " + msg, NotificationIcon::Error)->show();
                        }
                    }
                });
            }
        }
    );
}

void SetDailyWeeklyPopup::onUnset(CCObject* sender) {
    WeakRef<SetDailyWeeklyPopup> self = this;
    createQuickPopup(
        "Confirm",
        "Unset this level from Daily/Weekly?",
        "Cancel", "Unset",
        [self](FLAlertLayer*, bool btn2) {
             if (btn2) {
                auto popup = self.lock();
                if (!popup) return;
                 auto gm = GameManager::get();
                 std::string username = gm->m_playerName;

                 matjson::Value json = matjson::makeObject({
                    {"levelID", popup->m_levelID},
                    {"type", "unset"},
                    {"username", username}
                });
                
                auto overlay = PaimonLoadingOverlay::create("Unsetting...");
                overlay->show(popup->m_mainLayer, 200);
                Ref<PaimonLoadingOverlay> loadingRef = overlay;

                HttpClient::get().post("/api/admin/set-daily", json.dump(), [self, loadingRef](bool success, std::string const& msg) {
                    if (loadingRef) loadingRef->dismiss();
                    if (auto popup = self.lock()) {
                        if (success) {
                            PaimonNotify::create("Unset successfully", NotificationIcon::Success)->show();
                            popup->onClose(nullptr);
                        } else {
                            PaimonNotify::create("Failed to unset: " + msg, NotificationIcon::Error)->show();
                        }
                    }
                });
            }
        }
    );
}
