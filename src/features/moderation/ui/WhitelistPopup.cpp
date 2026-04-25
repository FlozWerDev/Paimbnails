#include "WhitelistPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;
using namespace cocos2d;

WhitelistPopup* WhitelistPopup::create() {
    auto ret = new WhitelistPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool WhitelistPopup::init() {
    if (!Popup::init(360.f, 280.f)) return false;

    this->setTitle("Whitelist");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // ── input + add button ───────────────────────────────
    m_input = TextInput::create(180.f, "Username...");
    m_input->setPosition({cx - 30.f, content.height - 50.f});
    m_input->setCommonFilter(geode::CommonFilter::ID);
    m_input->setMaxCharCount(20);
    m_input->setID("wl-input"_spr);
    m_mainLayer->addChild(m_input);

    auto addSpr = ButtonSprite::create("+", 30, true, "bigFont.fnt", "GJ_button_01.png", 25.f, 0.7f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(WhitelistPopup::onAdd));
    addBtn->setID("wl-add-btn"_spr);

    auto addMenu = CCMenu::create();
    addMenu->setPosition({cx + 100.f, content.height - 50.f});
    addMenu->addChild(addBtn);
    m_mainLayer->addChild(addMenu);

    // ── scroll panel ─────────────────────────────────────
    float panelW = content.width - 20.f;
    float panelH = content.height - 90.f;
    float panelY = (content.height - 60.f) / 2.f - 5.f;

    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 70);
    panel->setPosition({cx - panelW / 2, panelY - panelH / 2});
    m_mainLayer->addChild(panel);

    m_scrollViewSize = CCSize{panelW, panelH};

    m_scroll = ScrollLayer::create(m_scrollViewSize);
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({cx - panelW / 2.f, panelY - panelH / 2.f});
    m_scroll->setID("wl-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 5);

    m_listContainer = CCNode::create();
    m_listContainer->setAnchorPoint({0.f, 0.f});
    m_listContainer->setContentSize(m_scrollViewSize);
    m_scroll->m_contentLayer->addChild(m_listContainer);

    fetchWhitelist();
    paimon::markDynamicPopup(this);
    return true;
}

void WhitelistPopup::fetchWhitelist() {
    auto overlay = PaimonLoadingOverlay::create("Loading whitelist...");
    overlay->show(m_mainLayer, 200);
    Ref<PaimonLoadingOverlay> loadingRef = overlay;

    auto self = WeakRef<WhitelistPopup>(this);
    HttpClient::get().getWhitelist("profilebackground", [self, loadingRef](bool success, std::string const& resp) {
        if (loadingRef) loadingRef->dismiss();
        auto popup = self.lock();
        if (!popup) return;

        popup->m_users.clear();
        if (success) {
            auto parsed = matjson::parse(resp);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();
                if (root.contains("users") && root["users"].isArray()) {
                    auto arrRes = root["users"].asArray();
                    if (arrRes.isOk()) {
                        for (auto const& v : arrRes.unwrap()) {
                            if (v.isString()) {
                                popup->m_users.push_back(v.asString().unwrapOr(""));
                            }
                        }
                    }
                }
            }
        }
        popup->rebuildList();
    });
}

void WhitelistPopup::rebuildList() {
    m_listContainer->removeAllChildren();

    float viewW = m_scrollViewSize.width;
    float viewH = m_scrollViewSize.height;

    if (m_users.empty()) {
        auto lbl = CCLabelBMFont::create("No users in whitelist", "goldFont.fnt");
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->setPosition({viewW / 2.f, viewH / 2.f});
        m_listContainer->addChild(lbl);
        m_listContainer->setContentSize(m_scrollViewSize);
        m_scroll->m_contentLayer->setContentSize(m_scrollViewSize);
        m_scroll->scrollToTop();
        return;
    }

    constexpr float cellH = 30.f;
    constexpr float cellGap = 4.f;
    constexpr float cellPad = 8.f;
    float cellW = viewW - cellPad * 2.f;

    float totalH = cellH * m_users.size() + cellGap * (m_users.size() - 1);
    if (totalH < viewH) totalH = viewH;

    m_listContainer->setContentSize({viewW, totalH});

    float yPos = totalH - cellH / 2.f;
    for (auto const& user : m_users) {
        auto cell = CCNode::create();
        cell->setContentSize({cellW, cellH});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({viewW / 2.f, yPos});

        auto bg = paimon::SpriteHelper::createDarkPanel(cellW, cellH, 55);
        bg->setPosition({0, 0});
        cell->addChild(bg);

        auto name = CCLabelBMFont::create(user.c_str(), "chatFont.fnt");
        name->setScale(0.5f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({10.f, cellH / 2.f});
        cell->addChild(name);

        auto btnMenu = CCMenu::create();
        btnMenu->setPosition({cellW - 35.f, cellH / 2.f});
        btnMenu->setContentSize({70.f, cellH});
        cell->addChild(btnMenu);

        auto rmSpr = ButtonSprite::create("X", 30, true, "bigFont.fnt", "GJ_button_06.png", 25.f, 0.6f);
        rmSpr->setScale(0.7f);
        auto rmBtn = CCMenuItemSpriteExtra::create(rmSpr, this, menu_selector(WhitelistPopup::onRemove));
        rmBtn->setID("wl-rm-btn"_spr);
        rmBtn->setUserObject(CCString::create(user));
        rmBtn->setPosition({0.f, 0.f});
        btnMenu->addChild(rmBtn);

        m_listContainer->addChild(cell);
        yPos -= (cellH + cellGap);
    }

    m_listContainer->setPosition({0.f, 0.f});
    m_scroll->m_contentLayer->setContentSize({viewW, totalH});
    m_scroll->scrollToTop();
}

void WhitelistPopup::onAdd(CCObject*) {
    std::string target = m_input->getString();
    if (target.empty()) {
        PaimonNotify::create("Enter a username", NotificationIcon::Error)->show();
        return;
    }

    // Verify we have Mod Code
    if (HttpClient::get().getModCode().empty()) {
        PaimonNotify::create("Mod Code required", NotificationIcon::Error)->show();
        return;
    }

    auto self = WeakRef<WhitelistPopup>(this);
    HttpClient::get().addToWhitelist(target, "profilebackground", [self, target](bool success, std::string const& resp) {
        auto popup = self.lock();
        if (!popup) return;

        if (success) {
            PaimonNotify::create(
                fmt::format("Added '{}' to whitelist", target).c_str(),
                NotificationIcon::Success)->show();
            popup->m_input->setString("");
            popup->fetchWhitelist();
        } else {
            PaimonNotify::create("Failed to add user", NotificationIcon::Error)->show();
        }
    });
}

void WhitelistPopup::onRemove(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;

    std::string target = strObj->getCString();

    if (HttpClient::get().getModCode().empty()) {
        PaimonNotify::create("Mod Code required", NotificationIcon::Error)->show();
        return;
    }

    auto self = WeakRef<WhitelistPopup>(this);
    HttpClient::get().removeFromWhitelist(target, "profilebackground", [self, target](bool success, std::string const& resp) {
        auto popup = self.lock();
        if (!popup) return;

        if (success) {
            PaimonNotify::create(
                fmt::format("Removed '{}' from whitelist", target).c_str(),
                NotificationIcon::Warning)->show();
            popup->fetchWhitelist();
        } else {
            PaimonNotify::create("Failed to remove user", NotificationIcon::Error)->show();
        }
    });
}
