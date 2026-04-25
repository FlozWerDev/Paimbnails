#include "BanListPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>

#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

BanListPopup* BanListPopup::create() {
    auto ret = new BanListPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BanListPopup::init() {
    if (!Popup::init(360.f, 260.f)) return false;

    this->setTitle(Localization::get().getString("ban.list.title"));

    auto content = this->m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    float panelW = content.width - 20.f;
    float panelH = content.height - 60.f;
    float panelY = content.height / 2.f - 10.f;

    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 70);
    panel->setPosition({cx - panelW / 2, panelY - panelH / 2});
    panel->setID("list-panel"_spr);
    this->m_mainLayer->addChild(panel);

    m_scrollViewSize = CCSizeMake(panelW, panelH);

    m_scroll = geode::ScrollLayer::create(m_scrollViewSize);
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({cx - panelW / 2.f, panelY - panelH / 2.f});
    m_scroll->setID("ban-scroll"_spr);
    this->m_mainLayer->addChild(m_scroll, 5);

    m_listContainer = CCNode::create();
    m_listContainer->setAnchorPoint({0.f, 0.f});
    m_listContainer->setContentSize(m_scrollViewSize);
    m_scroll->m_contentLayer->addChild(m_listContainer);

    auto overlay = PaimonLoadingOverlay::create("Loading ban list...");
    overlay->show(m_mainLayer, 200);
    Ref<PaimonLoadingOverlay> loadingRef = overlay;

    auto self = WeakRef<BanListPopup>(this);
    HttpClient::get().getBanList([self, loadingRef](bool success, std::string const& jsonData) {
        if (loadingRef) loadingRef->dismiss();
        auto popup = self.lock();
        if (!popup) return;

        std::vector<std::string> users;

        if (success) {
            auto parsed = matjson::parse(jsonData);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();

                auto bannedArr = root["banned"].asArray();
                if (bannedArr.isOk()) {
                    for (auto const& v : bannedArr.unwrap()) {
                        auto s = v.asString().unwrapOr("");
                        if (!s.empty()) users.push_back(s);
                    }
                }

                for (auto const& val : root["details"]) {
                    if (!val.isObject()) continue;
                    auto keyOpt = val.getKey();
                    if (!keyOpt) continue;

                    BanDetail d;
                    d.reason   = val["reason"].asString().unwrapOr("");
                    d.bannedBy = val["bannedBy"].asString().unwrapOr("");
                    d.date     = val["date"].asString().unwrapOr("");

                    popup->m_banDetails[*keyOpt] = d;
                }
            }
        }

        if (popup->getParent()) {
            popup->rebuildList(users);
        }
    });

    paimon::markDynamicPopup(this);
    return true;
}

void BanListPopup::rebuildList(std::vector<std::string> const& users) {
    m_listContainer->removeAllChildren();
    
    float viewW = m_scrollViewSize.width;
    float viewH = m_scrollViewSize.height;

    if (users.empty()) {
        auto lbl = CCLabelBMFont::create(Localization::get().getString("ban.list.empty").c_str(), "goldFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->setPosition({viewW / 2.f, viewH / 2.f});
        m_listContainer->addChild(lbl);
        m_listContainer->setContentSize(m_scrollViewSize);
        m_scroll->m_contentLayer->setContentSize(m_scrollViewSize);
        m_scroll->scrollToTop();
        return;
    }

    constexpr float cellH = 30.f;
    constexpr float cellGap = 5.f;
    constexpr float cellPad = 10.f;
    float cellW = viewW - cellPad * 2.f;

    float totalH = cellH * users.size() + cellGap * (users.size() - 1);
    if (totalH < viewH) totalH = viewH;

    m_listContainer->setContentSize({viewW, totalH});

    float yPos = totalH - cellH / 2.f;
    for (auto const& user : users) {
        auto cell = CCNode::create();
        cell->setContentSize({cellW, cellH});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({viewW / 2.f, yPos});
        cell->setID("user-cell"_spr);

        auto bg = paimon::SpriteHelper::createDarkPanel(cellW, cellH, 55);
        bg->setPosition({0, 0});
        cell->addChild(bg);

        auto name = CCLabelBMFont::create(user.c_str(), "chatFont.fnt");
        name->setScale(0.5f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({10.f, cellH / 2.f});
        cell->addChild(name);

        auto btnMenu = CCMenu::create();
        btnMenu->setPosition({cellW - 60.f, cellH / 2.f});
        btnMenu->setContentSize({100.f, cellH});
        cell->addChild(btnMenu);

        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.6f);
        auto infoBtn = CCMenuItemSpriteExtra::create(infoSpr, this, menu_selector(BanListPopup::onInfo));
        infoBtn->setID("ban-info-btn"_spr);
        infoBtn->setUserObject(CCString::create(user));
        infoBtn->setPosition({-20.f, 0.f});
        btnMenu->addChild(infoBtn);

        auto unbanSpr = ButtonSprite::create(Localization::get().getString("ban.list.unban_btn").c_str(), 50, true, "goldFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
        unbanSpr->setScale(0.7f);
        auto unbanBtn = CCMenuItemSpriteExtra::create(unbanSpr, this, menu_selector(BanListPopup::onUnban));
        unbanBtn->setID("unban-btn"_spr);
        unbanBtn->setUserObject(CCString::create(user));
        unbanBtn->setPosition({25.f, 0.f});
        btnMenu->addChild(unbanBtn);

        m_listContainer->addChild(cell);
        yPos -= (cellH + cellGap);
    }

    m_listContainer->setPosition({0.f, 0.f});
    m_scroll->m_contentLayer->setContentSize({viewW, totalH});
    m_scroll->scrollToTop();
}

void BanListPopup::onInfo(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();
    std::string body = Localization::get().getString("ban.info.no_info");
    
    if (m_banDetails.count(username)) {
        auto& d = m_banDetails[username];
        body = fmt::format(
            "{}: <cy>{}</c>\n"
            "{}: <cg>{}</c>\n"
            "{}: <cl>{}</c>",
            Localization::get().getString("ban.info.reason"), d.reason.empty() ? "N/A" : d.reason,
            Localization::get().getString("ban.info.by"), d.bannedBy.empty() ? "N/A" : d.bannedBy,
            Localization::get().getString("ban.info.date"), d.date.empty() ? "N/A" : d.date
        );
    }
    
    geode::createQuickPopup(Localization::get().getString("ban.info.title").c_str(), body, "OK", nullptr, nullptr);
}

void BanListPopup::onUnban(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();

    Ref<BanListPopup> self = this;
    geode::createQuickPopup(
        Localization::get().getString("ban.unban.title").c_str(),
        fmt::format(fmt::runtime(Localization::get().getString("ban.unban.confirm")), username),
        "Cancel", Localization::get().getString("ban.list.unban_btn").c_str(),
        [self, username](auto, bool btn2) {
            if (btn2) {
                HttpClient::get().unbanUser(username, [self](bool success, std::string const& msg) {
                    if (success) {
                        PaimonNotify::create(Localization::get().getString("ban.unban.success"), NotificationIcon::Success)->show();
                        self->onClose(nullptr);
                    } else {
                        PaimonNotify::create(Localization::get().getString("ban.unban.error"), NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}
