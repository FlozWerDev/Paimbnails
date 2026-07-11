#include "SetDailyWeeklyPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

bool SetDailyWeeklyPopup::init(int levelID) {
    m_levelID = levelID;

    if (!Popup::init(320.f, 240.f)) return false;

    this->setTitle("Set Daily / Weekly");
    auto size = m_mainLayer->getContentSize();

    // Subtitle with the target level ID.
    auto subtitle = CCLabelBMFont::create(
        fmt::format("Level ID: {}", levelID).c_str(), "chatFont.fnt"
    );
    subtitle->setScale(0.5f);
    subtitle->setOpacity(160);
    subtitle->setPosition({ size.width / 2.f, size.height - 38.f });
    m_mainLayer->addChild(subtitle);

    constexpr float cardW = 260.f;
    constexpr float cardH = 46.f;

    // Builds a full-width card button: tinted panel, icon badge, title + hint.
    auto makeCard = [&](
        const char* iconFrame, const char* fallbackFrame,
        const char* label, const char* hint,
        ccColor3B tint, SEL_MenuHandler sel, std::string const& id
    ) {
        auto content = CCNode::create();
        content->setContentSize({ cardW, cardH });
        content->setAnchorPoint({ 0.5f, 0.5f });

        auto bg = CCScale9Sprite::create("GJ_square02.png");
        if (!bg) bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({ cardW, cardH });
        bg->setPosition({ cardW / 2.f, cardH / 2.f });
        bg->setColor(tint);
        content->addChild(bg);

        // Dark badge holding the action icon.
        auto badge = CCScale9Sprite::create("GJ_square05.png");
        if (badge) {
            badge->setContentSize({ 34.f, 34.f });
            badge->setPosition({ 8.f + 17.f, cardH / 2.f });
            badge->setColor({ 0, 0, 0 });
            badge->setOpacity(110);
            content->addChild(badge);
        }

        auto icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame);
        if (!icon && fallbackFrame)
            icon = paimon::SpriteHelper::safeCreateWithFrameName(fallbackFrame);
        if (icon) {
            float maxSide = std::max(icon->getContentWidth(), icon->getContentHeight());
            if (maxSide > 0.f) icon->setScale(26.f / maxSide);
            icon->setPosition({ 8.f + 17.f, cardH / 2.f });
            content->addChild(icon);
        }

        auto titleLbl = CCLabelBMFont::create(label, "goldFont.fnt");
        titleLbl->setScale(0.6f);
        titleLbl->setAnchorPoint({ 0.f, 0.5f });
        titleLbl->setPosition({ 52.f, cardH / 2.f + 9.f });
        content->addChild(titleLbl);

        auto hintLbl = CCLabelBMFont::create(hint, "chatFont.fnt");
        hintLbl->setScale(0.45f);
        hintLbl->setOpacity(180);
        hintLbl->setAnchorPoint({ 0.f, 0.5f });
        hintLbl->setPosition({ 52.f, cardH / 2.f - 9.f });
        content->addChild(hintLbl);

        auto btn = CCMenuItemSpriteExtra::create(content, this, sel);
        btn->setID(id);
        return btn;
    };

    auto actionMenu = CCMenu::create();
    actionMenu->setPosition({ size.width / 2.f, size.height / 2.f - 20.f });
    actionMenu->setContentSize({ cardW, 3 * cardH + 2 * 10.f });
    actionMenu->ignoreAnchorPointForPosition(false);
    actionMenu->setLayout(
        ColumnLayout::create()
            ->setGap(10.f)
            ->setAxisReverse(true)
    );
    m_mainLayer->addChild(actionMenu);

    actionMenu->addChild(makeCard(
        "GJ_timeIcon_001.png", "GJ_starsIcon_001.png",
        "Set Daily", "Feature this level as the Daily",
        { 110, 200, 110 },
        menu_selector(SetDailyWeeklyPopup::onSetDaily),
        "set-daily-btn"_spr
    ));

    actionMenu->addChild(makeCard(
        "gj_dailyCrown_001.png", "GJ_starsIcon_001.png",
        "Set Weekly", "Feature this level as the Weekly",
        { 120, 170, 255 },
        menu_selector(SetDailyWeeklyPopup::onSetWeekly),
        "set-weekly-btn"_spr
    ));

    actionMenu->addChild(makeCard(
        "GJ_deleteIcon_001.png", nullptr,
        "Unset", "Remove this level from Daily/Weekly",
        { 235, 110, 110 },
        menu_selector(SetDailyWeeklyPopup::onUnset),
        "unset-btn"_spr
    ));

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
    PopupManager::get().quickPopup(
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
    ).showInstant();
}

void SetDailyWeeklyPopup::onSetWeekly(CCObject* sender) {
    WeakRef<SetDailyWeeklyPopup> self = this;
    PopupManager::get().quickPopup(
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
    ).showInstant();
}

void SetDailyWeeklyPopup::onUnset(CCObject* sender) {
    WeakRef<SetDailyWeeklyPopup> self = this;
    PopupManager::get().quickPopup(
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
    ).showInstant();
}
