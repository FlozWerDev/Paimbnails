#include "AddModeratorPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../services/GdUserResolver.hpp"
#include "../../badges/services/RoleService.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <matjson.hpp>
#include <array>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
constexpr std::array<char const*, 4> kRoles = {"mod", "vip", "helper", "idea"};
}

AddModeratorPopup* AddModeratorPopup::create(geode::CopyableFunction<void(bool, std::string const&)> callback) {
    auto ret = new AddModeratorPopup();
    if (ret && ret->init(std::move(callback))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

std::string AddModeratorPopup::roleDisplayName(std::string const& role) const {
    return Localization::get().getString("rolemgr.role." + role);
}

bool AddModeratorPopup::init(geode::CopyableFunction<void(bool, std::string const&)> callback) {
    if (!Popup::init(400.f, 320.f)) return false;

    m_callback = callback;
    this->setTitle(Localization::get().getString("rolemgr.title").c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    buildRoleTabs();

    // Member list panel
    float panelW = content.width - 30.f;
    float panelH = 120.f;
    float panelY = content.height / 2.f + 8.f;

    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 70);
    panel->setPosition({cx - panelW / 2, panelY - panelH / 2});
    panel->setID("role-list-panel"_spr);
    m_mainLayer->addChild(panel);

    m_scrollViewSize = CCSizeMake(panelW, panelH);
    m_scroll = ScrollLayer::create(m_scrollViewSize);
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({cx - panelW / 2.f, panelY - panelH / 2.f});
    m_scroll->setID("role-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 5);

    m_listContainer = CCNode::create();
    m_listContainer->setAnchorPoint({0.f, 0.f});
    m_listContainer->setContentSize(m_scrollViewSize);
    m_scroll->m_contentLayer->addChild(m_listContainer);

    m_addLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_addLabel->setScale(0.4f);
    m_addLabel->setAnchorPoint({0.f, 0.5f});
    m_addLabel->setPosition({cx - panelW / 2.f, content.height / 2.f - 62.f});
    m_addLabel->setID("add-label"_spr);
    m_mainLayer->addChild(m_addLabel);

    m_usernameInput = TextInput::create(content.width - 130.f, Localization::get().getString("rolemgr.enter_username"));
    m_usernameInput->setPosition({cx - 28.f, content.height / 2.f - 86.f});
    m_usernameInput->setCommonFilter(CommonFilter::ID);
    m_usernameInput->setMaxCharCount(20);
    m_usernameInput->setID("username-input"_spr);
    m_mainLayer->addChild(m_usernameInput, 11);

    auto addSpr = ButtonSprite::create(
        Localization::get().getString("addmod.add_btn").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    addSpr->setScale(0.7f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(AddModeratorPopup::onAdd));
    addBtn->setPosition({cx + panelW / 2.f - 30.f, content.height / 2.f - 86.f});
    addBtn->setID("add-btn"_spr);
    m_buttonMenu->addChild(addBtn);

    auto closeSpr = ButtonSprite::create(
        Localization::get().getString("general.cancel").c_str(),
        "goldFont.fnt", "GJ_button_06.png", 0.8f
    );
    closeSpr->setScale(0.7f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(AddModeratorPopup::onClose));
    closeBtn->setPosition({cx, 24.f});
    closeBtn->setID("close-btn"_spr);
    m_buttonMenu->addChild(closeBtn);

    selectRole(m_activeRole);

    paimon::markDynamicPopup(this);
    return true;
}

void AddModeratorPopup::buildRoleTabs() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({cx, content.height - 38.f});
    m_tabMenu->setContentSize({content.width - 30.f, 30.f});
    m_tabMenu->setID("role-tabs"_spr);
    m_mainLayer->addChild(m_tabMenu);

    float gap = 6.f;
    int n = static_cast<int>(kRoles.size());
    float btnW = ((content.width - 30.f) - gap * (n - 1)) / n;

    float x = -(content.width - 30.f) / 2.f + btnW / 2.f;
    for (auto const& role : kRoles) {
        auto spr = ButtonSprite::create(
            roleDisplayName(role).c_str(), 80, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f
        );
        spr->setScale(std::min(0.85f, btnW / std::max(1.f, spr->getContentSize().width)));
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AddModeratorPopup::onRoleTab));
        btn->setUserObject(CCString::create(role));
        btn->setPosition({x, 0.f});
        btn->setID(fmt::format("tab-{}", role));
        m_tabMenu->addChild(btn);
        x += btnW + gap;
    }
}

void AddModeratorPopup::onRoleTab(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto str = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!str) return;
    selectRole(str->getCString());
}

void AddModeratorPopup::selectRole(std::string const& role) {
    m_activeRole = role;

    // Highlight the active tab.
    for (auto* child : CCArrayExt<CCNode*>(m_tabMenu->getChildren())) {
        auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(child);
        if (!btn) continue;
        auto str = typeinfo_cast<CCString*>(btn->getUserObject());
        bool active = str && role == str->getCString();
        if (auto* bs = typeinfo_cast<ButtonSprite*>(btn->getChildren()->objectAtIndex(0))) {
            bs->setColor(active ? ccColor3B{255, 255, 255} : ccColor3B{150, 150, 150});
        }
    }

    refreshAddLabel();
    fetchMembers();
}

void AddModeratorPopup::refreshAddLabel() {
    if (!m_addLabel) return;
    m_addLabel->setString(fmt::format(
        fmt::runtime(Localization::get().getString("rolemgr.add_label")),
        roleDisplayName(m_activeRole)
    ).c_str());
}

void AddModeratorPopup::fetchMembers() {
    if (!m_listContainer) return;
    m_listContainer->removeAllChildren();
    m_members.clear();

    auto overlay = PaimonLoadingOverlay::create(Localization::get().getString("rolemgr.loading_members"));
    overlay->show(m_mainLayer, 200);
    Ref<PaimonLoadingOverlay> loadingRef = overlay;

    WeakRef<AddModeratorPopup> self = this;
    std::string role = m_activeRole;

    auto finish = [self, loadingRef, role](std::vector<std::string> names) {
        if (loadingRef) loadingRef->dismiss();
        auto popup = self.lock();
        if (!popup || popup->m_activeRole != role) return;
        popup->m_members = std::move(names);
        if (popup->getParent()) popup->rebuildList();
    };

    if (role == "mod") {
        HttpClient::get().getModerators([finish](bool success, std::vector<std::string> const& mods) {
            finish(success ? mods : std::vector<std::string>{});
        });
    } else {
        HttpClient::get().getRoleMembers(role, [finish](bool success, std::string const& response) {
            std::vector<std::string> names;
            if (success) {
                auto res = matjson::parse(response);
                if (res.isOk()) {
                    auto json = res.unwrap();
                    if (json.contains("members") && json["members"].isArray()) {
                        auto arrRes = json["members"].asArray();
                        if (arrRes.isOk()) {
                            for (auto const& item : arrRes.unwrap()) {
                                if (item.isString()) names.push_back(item.asString().unwrapOr(""));
                                else if (item.contains("username")) names.push_back(item["username"].asString().unwrapOr(""));
                            }
                        }
                    }
                }
            }
            finish(std::move(names));
        });
    }
}

void AddModeratorPopup::rebuildList() {
    m_listContainer->removeAllChildren();

    float viewW = m_scrollViewSize.width;
    float viewH = m_scrollViewSize.height;

    if (m_members.empty()) {
        auto lbl = CCLabelBMFont::create(Localization::get().getString("rolemgr.no_members").c_str(), "goldFont.fnt");
        lbl->setScale(0.4f);
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->setPosition({viewW / 2.f, viewH / 2.f});
        m_listContainer->addChild(lbl);
        m_listContainer->setContentSize(m_scrollViewSize);
        m_scroll->m_contentLayer->setContentSize(m_scrollViewSize);
        m_scroll->scrollToTop();
        return;
    }

    constexpr float cellH = 38.f;
    constexpr float cellGap = 4.f;
    constexpr float cellPad = 6.f;
    float cellW = viewW - cellPad * 2.f;

    float totalH = cellH * m_members.size() + cellGap * (m_members.size() - 1);
    if (totalH < viewH) totalH = viewH;
    m_listContainer->setContentSize({viewW, totalH});

    float yPos = totalH - cellH / 2.f;
    for (auto const& name : m_members) {
        auto cell = CCNode::create();
        cell->setContentSize({cellW, cellH});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({viewW / 2.f, yPos});

        auto bg = paimon::SpriteHelper::createDarkPanel(cellW, cellH, 55);
        bg->setPosition({0, 0});
        cell->addChild(bg);

        auto label = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        label->setScale(0.6f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({12.f, cellH / 2.f});
        cell->addChild(label);

        auto btnMenu = CCMenu::create();
        btnMenu->setPosition({cellW - 45.f, cellH / 2.f});
        btnMenu->setContentSize({80.f, cellH});
        cell->addChild(btnMenu);

        auto removeSpr = ButtonSprite::create(
            Localization::get().getString("addmod.remove_btn").c_str(),
            50, true, "goldFont.fnt", "GJ_button_06.png", 28.f, 0.5f
        );
        removeSpr->setScale(0.75f);
        auto removeBtn = CCMenuItemSpriteExtra::create(removeSpr, this, menu_selector(AddModeratorPopup::onRemove));
        removeBtn->setUserObject(CCString::create(name));
        removeBtn->setID("remove-btn"_spr);
        btnMenu->addChild(removeBtn);

        m_listContainer->addChild(cell);
        yPos -= (cellH + cellGap);
    }

    m_listContainer->setPosition({0.f, 0.f});
    m_scroll->m_contentLayer->setContentSize({viewW, totalH});
    m_scroll->scrollToTop();
}

void AddModeratorPopup::onAdd(CCObject*) {
    std::string username = m_usernameInput->getString();
    if (username.empty()) {
        PaimonNotify::create(Localization::get().getString("rolemgr.enter_username").c_str(), NotificationIcon::Warning)->show();
        return;
    }

    m_loadingSpinner = PaimonLoadingOverlay::create(Localization::get().getString("rolemgr.searching"), 30.f);
    m_loadingSpinner->show(m_mainLayer, 100);

    WeakRef<AddModeratorPopup> self = this;
    std::string role = m_activeRole;

    // 1) Resolve the user on the GD servers to bind a real accountID.
    paimon::moderation::resolveUsername(username,
        [self, role, username](bool ok, int accountID, std::string const& exactName) {
            auto popup = self.lock();
            if (!popup) return;

            if (!ok) {
                if (popup->m_loadingSpinner) { popup->m_loadingSpinner->dismiss(); popup->m_loadingSpinner = nullptr; }
                PopupManager::get().alert(Localization::get().getString("rolemgr.error_title"), fmt::format(fmt::runtime(Localization::get().getString("rolemgr.user_not_found")), username)).showInstant();
                return;
            }

            // 2) Upload the resolved user to the Paimbnails server.
            std::string targetName = exactName.empty() ? username : exactName;
            HttpClient::get().addRoleMember(role, targetName, accountID,
                [self, role, targetName](bool success, std::string const& message) {
                    auto popup = self.lock();
                    if (!popup) return;
                    if (popup->m_loadingSpinner) { popup->m_loadingSpinner->dismiss(); popup->m_loadingSpinner = nullptr; }

                    if (success) {
                        PaimonNotify::create(
                            fmt::format(fmt::runtime(Localization::get().getString("rolemgr.add_success")),
                                        targetName, popup->roleDisplayName(role)).c_str(),
                            NotificationIcon::Success
                        )->show();
                        popup->m_usernameInput->setString("");
                        paimon::roles::RoleService::get().clear();
                        if (popup->m_callback) popup->m_callback(true, targetName);
                        popup->fetchMembers();
                    } else {
                        PopupManager::get().alert(Localization::get().getString("rolemgr.error_title"), message.empty() ? Localization::get().getString("addmod.error_msg").c_str() : message).showInstant();
                    }
                });
        });
}

void AddModeratorPopup::onRemove(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    std::string username = strObj->getCString();
    std::string role = m_activeRole;

    WeakRef<AddModeratorPopup> self = this;
    PopupManager::get().quickPopup(
        Localization::get().getString("rolemgr.remove_confirm_title").c_str(),
        fmt::format(fmt::runtime(Localization::get().getString("rolemgr.remove_confirm_msg")),
                    username, roleDisplayName(role)),
        Localization::get().getString("general.cancel").c_str(),
        Localization::get().getString("addmod.remove_btn").c_str(),
        [self, role, username](auto, bool confirm) {
            if (!confirm) return;
            auto popup = self.lock();
            if (!popup) return;

            popup->m_loadingSpinner = PaimonLoadingOverlay::create("...", 30.f);
            popup->m_loadingSpinner->show(popup->m_mainLayer, 100);

            HttpClient::get().removeRoleMember(role, username, 0,
                [self, role, username](bool success, std::string const& message) {
                    auto popup = self.lock();
                    if (!popup) return;
                    if (popup->m_loadingSpinner) { popup->m_loadingSpinner->dismiss(); popup->m_loadingSpinner = nullptr; }

                    if (success) {
                        PaimonNotify::create(
                            fmt::format(fmt::runtime(Localization::get().getString("rolemgr.remove_success")),
                                        username, popup->roleDisplayName(role)).c_str(),
                            NotificationIcon::Success
                        )->show();
                        paimon::roles::RoleService::get().clear();
                        popup->fetchMembers();
                    } else {
                        PopupManager::get().alert(
                            Localization::get().getString("rolemgr.error_title"),
                            message.empty() ? Localization::get().getString("addmod.remove_error") : message
                        ).showInstant();
                    }
                });
        }
    ).showInstant();
}
