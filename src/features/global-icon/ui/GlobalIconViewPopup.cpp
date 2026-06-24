#include "GlobalIconViewPopup.hpp"
#include "../services/GlobalIconStorage.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::globalicon {

GlobalIconViewPopup* GlobalIconViewPopup::create(int accountID, std::string const& username, GlobalIconSlot const& slot) {
    auto ret = new GlobalIconViewPopup();
    if (ret && ret->init(accountID, username, slot)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GlobalIconViewPopup::init(int accountID, std::string const& username, GlobalIconSlot const& slot) {
    if (!Popup::init(280.f, 180.f)) return false;

    m_accountID = accountID;
    m_username = username;
    m_slot = slot;

    std::string title = Localization::get().getString("globalicon.view_title");
    if (auto pos = title.find("{}"); pos != std::string::npos) {
        title.replace(pos, 2, username.empty() ? "?" : username);
    }
    this->setTitle(title.c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    m_preview = SimplePlayer::create(0);
    if (m_preview) {
        m_preview->setPosition({cx, content.height - 58.f});
        m_preview->setScale(1.6f);
        m_mainLayer->addChild(m_preview);
    }

    {
        auto spr = ButtonSprite::create(
            Localization::get().getString("globalicon.download").c_str(),
            "bigFont.fnt", "GJ_button_04.png", 0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GlobalIconViewPopup::onDownload));
        btn->setPosition({cx, 60.f});
        btn->setID("globalicon-download"_spr);
        menu->addChild(btn);
    }
    {
        auto spr = ButtonSprite::create(
            Localization::get().getString("globalicon.download_use").c_str(),
            "bigFont.fnt", "GJ_button_01.png", 0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GlobalIconViewPopup::onDownloadUse));
        btn->setPosition({cx, 30.f});
        btn->setID("globalicon-download-use"_spr);
        menu->addChild(btn);
    }

    this->setID("global-icon-view-popup"_spr);
    paimon::markDynamicPopup(this);

    refreshPreview();
    return true;
}

void GlobalIconViewPopup::refreshPreview() {
    if (!GlobalIconStorage::available()) return;
    WeakRef<GlobalIconViewPopup> self = this;
    GlobalIconStorage::get().ensureIcon(m_accountID, m_slot,
        [self](bool ok, std::string const& iconName) {
            if (!ok || iconName.empty()) return;
            auto popup = self.lock();
            if (!popup || !popup->m_preview) return;
            if (auto* info = more_icons::getIcon(iconName, IconType::Cube)) {
                more_icons::updateSimplePlayer(popup->m_preview, info);
            }
        });
}

void GlobalIconViewPopup::onDownload(CCObject*) {
    if (m_busy) return;
    m_busy = true;
    WeakRef<GlobalIconViewPopup> self = this;
    GlobalIconStorage::get().ensureIcon(m_accountID, m_slot,
        [self](bool ok, std::string const&) {
            if (auto popup = self.lock()) popup->m_busy = false;
            PaimonNotify::create(
                Localization::get().getString(ok ? "globalicon.downloaded" : "globalicon.download_failed").c_str(),
                ok ? NotificationIcon::Success : NotificationIcon::Error)->show();
        });
}

void GlobalIconViewPopup::onDownloadUse(CCObject*) {
    if (m_busy) return;
    m_busy = true;
    auto typeOpt = iconTypeFromString(m_slot.type);
    IconType type = typeOpt.value_or(IconType::Cube);
    WeakRef<GlobalIconViewPopup> self = this;
    GlobalIconStorage::get().ensureIcon(m_accountID, m_slot,
        [self, type](bool ok, std::string const& iconName) {
            if (auto popup = self.lock()) popup->m_busy = false;
            if (!ok || iconName.empty()) {
                PaimonNotify::create(
                    Localization::get().getString("globalicon.download_failed").c_str(),
                    NotificationIcon::Error)->show();
                return;
            }
            auto* info = more_icons::getIcon(iconName, type);
            if (info) {
                more_icons::setIcon(info, type);
                PaimonNotify::create(
                    Localization::get().getString("globalicon.applied").c_str(),
                    NotificationIcon::Success)->show();
            } else {
                PaimonNotify::create(
                    Localization::get().getString("globalicon.download_failed").c_str(),
                    NotificationIcon::Error)->show();
            }
        });
}

} // namespace paimon::globalicon
