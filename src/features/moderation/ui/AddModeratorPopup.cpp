#include "AddModeratorPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

using namespace geode::prelude;
using namespace cocos2d;

AddModeratorPopup* AddModeratorPopup::create(geode::CopyableFunction<void(bool, std::string const&)> callback) {
    auto ret = new AddModeratorPopup();
    if (ret && ret->init(std::move(callback))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool AddModeratorPopup::init(geode::CopyableFunction<void(bool, std::string const&)> callback) {
    if (!Popup::init(380.f, 290.f)) return false;
    
    m_callback = callback;
    
    this->setTitle(Localization::get().getString("addmod.title").c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // panel oscuro para la lista
    float panelW = content.width - 30.f;
    float panelH = 140.f;
    float panelY = content.height / 2.f + 30.f;

    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 70);
    panel->setPosition({cx - panelW / 2, panelY - panelH / 2});
    panel->setID("moderator-list-panel"_spr);
    m_mainLayer->addChild(panel);

    // scroll view centrado exactamente sobre el panel
    m_scrollViewSize = CCSizeMake(panelW, panelH);
    m_scroll = geode::ScrollLayer::create(m_scrollViewSize);
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({cx - panelW / 2.f, panelY - panelH / 2.f});
    m_scroll->setID("moderator-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 5);

    // contenedor de celdas (hijo directo del contentLayer del scroll)
    m_listContainer = CCNode::create();
    m_listContainer->setAnchorPoint({0.f, 0.f});
    m_listContainer->setContentSize(m_scrollViewSize);
    m_scroll->m_contentLayer->addChild(m_listContainer);

    // etiqueta "nuevo moderador"
    auto addLabel = CCLabelBMFont::create(
        Localization::get().getString("addmod.enter_username_label").c_str(), 
        "bigFont.fnt"
    );
    addLabel->setScale(0.4f);
    addLabel->setPosition({cx, content.height / 2.f - 55.f});
    addLabel->setID("add-label"_spr);
    m_mainLayer->addChild(addLabel);

    // input de username
    m_usernameInput = TextInput::create(content.width - 40.f, Localization::get().getString("addmod.enter_username"));
    m_usernameInput->setPosition({cx, content.height / 2.f - 80.f});
    m_usernameInput->setCommonFilter(geode::CommonFilter::ID);
    m_usernameInput->setMaxCharCount(20);
    m_usernameInput->setID("username-input"_spr);
    m_mainLayer->addChild(m_usernameInput, 11);

    // botones cancelar y agregar
    auto cancelSpr = ButtonSprite::create(
        Localization::get().getString("general.cancel").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    cancelSpr->setScale(0.8f);
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(AddModeratorPopup::onClose)
    );
    cancelBtn->setPosition({cx - 80.f, 28.f});
    cancelBtn->setID("cancel-btn"_spr);
    m_buttonMenu->addChild(cancelBtn);

    auto addSpr = ButtonSprite::create(
        Localization::get().getString("addmod.add_btn").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    addSpr->setScale(0.8f);
    auto addBtn = CCMenuItemSpriteExtra::create(
        addSpr, this, menu_selector(AddModeratorPopup::onAdd)
    );
    addBtn->setPosition({cx + 80.f, 28.f});
    addBtn->setID("add-btn"_spr);
    m_buttonMenu->addChild(addBtn);
    
    this->fetchAndShowModerators();

    paimon::markDynamicPopup(this);
    return true;
}

void AddModeratorPopup::fetchAndShowModerators() {
    m_listContainer->removeAllChildren();

    auto overlay = PaimonLoadingOverlay::create(
        Localization::get().getString("addmod.loading_mods")
    );
    overlay->show(m_mainLayer, 200);
    Ref<PaimonLoadingOverlay> loadingRef = overlay;

    WeakRef<AddModeratorPopup> self = this;
    HttpClient::get().getModerators([self, loadingRef](bool success, std::vector<std::string> const& moderators) {
        if (loadingRef) loadingRef->dismiss();
        auto popup = self.lock();
        if (!popup) return;

        if (success) {
            popup->m_moderatorNames = moderators;
        } else {
            popup->m_moderatorNames.clear();
        }

        if (popup->getParent()) {
            popup->rebuildList();
        }
    });
}

void AddModeratorPopup::rebuildList() {
    m_listContainer->removeAllChildren();

    float viewW = m_scrollViewSize.width;
    float viewH = m_scrollViewSize.height;

    if (m_moderatorNames.empty()) {
        auto lbl = CCLabelBMFont::create(
            Localization::get().getString("addmod.no_mods").c_str(), 
            "goldFont.fnt"
        );
        lbl->setScale(0.4f);
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->setPosition({viewW / 2.f, viewH / 2.f});
        m_listContainer->addChild(lbl);
        m_listContainer->setContentSize(m_scrollViewSize);
        m_scroll->m_contentLayer->setContentSize(m_scrollViewSize);
        m_scroll->scrollToTop();
        return;
    }

    constexpr float cellH = 40.f;
    constexpr float cellGap = 4.f;
    constexpr float cellPad = 6.f; // padding horizontal interno
    float cellW = viewW - cellPad * 2.f;

    float totalH = cellH * m_moderatorNames.size() + cellGap * (m_moderatorNames.size() - 1);
    if (totalH < viewH) totalH = viewH;

    m_listContainer->setContentSize({viewW, totalH});

    // celdas de arriba a abajo
    float yPos = totalH - cellH / 2.f;
    for (auto const& modName : m_moderatorNames) {
        auto cell = CCNode::create();
        cell->setContentSize({cellW, cellH});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({viewW / 2.f, yPos});
        cell->setID("mod-cell"_spr);

        // fondo
        auto bg = paimon::SpriteHelper::createDarkPanel(cellW, cellH, 55);
        bg->setPosition({0, 0});
        cell->addChild(bg);

        // nombre
        auto name = CCLabelBMFont::create(modName.c_str(), "chatFont.fnt");
        name->setScale(0.6f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({12.f, cellH / 2.f});
        cell->addChild(name);

        // boton quitar (menu dentro de la celda)
        auto btnMenu = CCMenu::create();
        btnMenu->setPosition({cellW - 45.f, cellH / 2.f});
        btnMenu->setContentSize({80.f, cellH});
        cell->addChild(btnMenu);

        auto removeSpr = ButtonSprite::create(
            Localization::get().getString("addmod.remove_btn").c_str(), 
            50, true, "goldFont.fnt", "GJ_button_06.png", 28.f, 0.5f
        );
        removeSpr->setScale(0.75f);
        auto removeBtn = CCMenuItemSpriteExtra::create(
            removeSpr, this, menu_selector(AddModeratorPopup::onRemove)
        );
        removeBtn->setUserObject(CCString::create(modName));
        removeBtn->setID("remove-btn"_spr);
        btnMenu->addChild(removeBtn);

        m_listContainer->addChild(cell);
        yPos -= (cellH + cellGap);
    }

    // posicionar contenedor y scroll
    m_listContainer->setPosition({0.f, 0.f});
    m_scroll->m_contentLayer->setContentSize({viewW, totalH});
    m_scroll->scrollToTop();
}

void AddModeratorPopup::onAdd(CCObject*) {
    std::string username = m_usernameInput->getString();
    
    if (username.empty()) {
        PaimonNotify::create(
            Localization::get().getString("addmod.enter_username").c_str(), 
            NotificationIcon::Warning
        )->show();
        return;
    }
    
    auto gm = GameManager::get();
    std::string adminUser = gm->m_playerName;
    
    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    m_loadingSpinner->show(m_mainLayer, 100);
    
    Ref<AddModeratorPopup> safeRef = this;

    ThumbnailAPI::get().addModerator(username, adminUser, [safeRef, username](bool success, std::string const& message) {
        if (safeRef->m_loadingSpinner) {
            safeRef->m_loadingSpinner->dismiss();
        }
        safeRef->m_loadingSpinner = nullptr;

        if (success) {
            PaimonNotify::create(
                Localization::get().getString("addmod.success_msg").c_str(),
                NotificationIcon::Success
            )->show();
            
            safeRef->m_usernameInput->setString("");

            if (safeRef->m_callback) safeRef->m_callback(true, username);

            safeRef->fetchAndShowModerators();
        } else {
            createQuickPopup(
                Localization::get().getString("addmod.error_title").c_str(),
                message.empty() ? Localization::get().getString("addmod.error_msg") : message,
                "OK", nullptr, nullptr
            );
        }
    });
}

void AddModeratorPopup::onRemove(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();

    Ref<AddModeratorPopup> self = this;
    createQuickPopup(
        Localization::get().getString("addmod.remove_confirm_title").c_str(),
        fmt::format(
            fmt::runtime(Localization::get().getString("addmod.remove_confirm_msg")), 
            username
        ),
        Localization::get().getString("general.cancel").c_str(),
        Localization::get().getString("addmod.remove_btn").c_str(),
        [self, username](auto, bool btn2) {
            if (!btn2) return;

            auto gm = GameManager::get();
            std::string adminUser = gm->m_playerName;

            self->m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 30.f);
            self->m_loadingSpinner->show(self->m_mainLayer, 100);

            ThumbnailAPI::get().removeModerator(username, adminUser, [self, username](bool success, std::string const& message) {
                if (self->m_loadingSpinner) {
                    self->m_loadingSpinner->dismiss();
                }
                self->m_loadingSpinner = nullptr;

                if (success) {
                    PaimonNotify::create(
                        Localization::get().getString("addmod.remove_success").c_str(),
                        NotificationIcon::Success
                    )->show();

                    if (self->m_callback) self->m_callback(true, username);

                    self->fetchAndShowModerators();
                } else {
                    PaimonNotify::create(
                        message.empty() 
                            ? Localization::get().getString("addmod.remove_error").c_str() 
                            : message.c_str(),
                        NotificationIcon::Error
                    )->show();
                }
            });
        }
    );
}
