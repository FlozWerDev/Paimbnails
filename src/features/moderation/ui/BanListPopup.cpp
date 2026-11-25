#include "BanListPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>

#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SafeText.hpp"
#include "../../../core/ModAuthFlow.hpp"

#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
// the server can return an unbounded list; keep the scroll layer and labels
// within a sane budget.
constexpr std::size_t kMaxBanListEntries = 500;
}

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
    if (!paimon::modauth::isVerified()) {
        PaimonNotify::show(Localization::get().getString("ban.verify_required"), NotificationIcon::Warning);
        return false;
    }
    if (!PaimonPopup::init(360.f, 260.f)) return false;

    this->setTitle(Localization::get().getString("ban.list.title"));
    this->addInfoButton(Localization::get().getString("ban.list.title"),
        "Everyone currently <cr>banned</c> from Paimbnails. Tap the <cy>info</c> icon "
        "to see why a user was banned, or <cg>Unban</c> to restore their access.");

    auto content = this->m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    float panelW = content.width - 20.f;
    float panelH = content.height - 60.f;
    float panelY = content.height / 2.f - 10.f;

    auto panel = paimon::ui::makeInset({panelW, panelH});
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
        std::unordered_set<std::string> seen;
        bool parsedOk = false;

        if (success) {
            auto parsed = matjson::parse(jsonData);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();
                if (root.isObject()) {
                    parsedOk = true;

                    auto bannedArr = root["banned"].asArray();
                    if (bannedArr.isOk()) {
                        for (auto const& v : bannedArr.unwrap()) {
                            if (users.size() >= kMaxBanListEntries) break;
                            auto name = v.asString().unwrapOr("");
                            if (!paimon::text::isSafeAccountName(name)) continue;
                            if (!seen.insert(name).second) continue;
                            users.push_back(std::move(name));
                        }
                    }

                    // matjson iterators throw on non-containers, so guard 'details':
                    // a missing/scalar value would crash here.
                    if (root["details"].isObject()) {
                        std::size_t detailCount = 0;
                        for (auto const& val : root["details"]) {
                            if (detailCount++ >= kMaxBanListEntries) break;
                            if (!val.isObject()) continue;
                            auto keyOpt = val.getKey();
                            if (!keyOpt) continue;

                            auto key = *keyOpt;
                            if (!paimon::text::isSafeAccountName(key)) continue;

                            BanDetail d;
                            d.reason = paimon::text::sanitizeDisplayText(
                                val["reason"].asString().unwrapOr(""), 200);
                            d.bannedBy = paimon::text::sanitizeDisplayText(
                                val["bannedBy"].asString().unwrapOr(""), 64);
                            d.date = paimon::text::sanitizeDisplayText(
                                val["date"].asString().unwrapOr(""), 32);

                            popup->m_banDetails[std::move(key)] = std::move(d);
                        }
                    }
                }
            }
        }

        if (!success || !parsedOk) {
            PaimonNotify::show(Localization::get().getString("ban.list.error"), NotificationIcon::Error);
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

        auto bg = paimon::ui::makeInset({cellW, cellH}, 70);
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

        auto unbanSpr = paimon::ui::makeButtonSprite(
            Localization::get().getString("ban.list.unban_btn").c_str(),
            paimon::ui::Btn::Green, 68.f, 0.5f, "bigFont.fnt");
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
    
    std::string const username = strObj->getCString();
    if (!paimon::text::isSafeAccountName(username)) return;
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
    
    PopupManager::get().alert(Localization::get().getString("ban.info.title"), body).showInstant();
}

void BanListPopup::onUnban(CCObject* sender) {
    if (m_unbanPending) return;
    if (!paimon::modauth::isVerified()) {
        PaimonNotify::show(Localization::get().getString("ban.verify_required"), NotificationIcon::Warning);
        return;
    }

    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;

    std::string const username = strObj->getCString();
    if (!paimon::text::isSafeAccountName(username)) {
        PaimonNotify::show(Localization::get().getString("ban.popup.invalid_user"), NotificationIcon::Error);
        return;
    }

    Ref<BanListPopup> self = this;
    PopupManager::get().quickPopup(
        Localization::get().getString("ban.unban.title").c_str(),
        fmt::format(fmt::runtime(Localization::get().getString("ban.unban.confirm")), username),
        "Cancel", Localization::get().getString("ban.list.unban_btn").c_str(),
        [self, username](auto, bool btn2) {
            if (!btn2 || self->m_unbanPending) return;
            self->m_unbanPending = true;
            HttpClient::get().unbanUser(username, [self](bool success, std::string const&) {
                self->m_unbanPending = false;
                if (success) {
                    PaimonNotify::show(Localization::get().getString("ban.unban.success"), NotificationIcon::Success);
                    self->onClose(nullptr);
                } else {
                    PaimonNotify::show(Localization::get().getString("ban.unban.error"), NotificationIcon::Error);
                }
            });
        }
    ).showInstant();
}
