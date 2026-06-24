#include "MenuMusicPlaylistsPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <chrono>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

static constexpr float kCardHeight = 44.f;
static constexpr float kCardGap = 6.f;

MenuMusicPlaylistsPopup* MenuMusicPlaylistsPopup::create() {
    auto ret = new MenuMusicPlaylistsPopup();
    if (ret && ret->init(420.f, 300.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuMusicPlaylistsPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Playlists");

    MenuMusicLibrary::get().load();

    buildHeader();
    buildList();
    showGrid();

    m_libListenerToken = MenuMusicLibrary::get().addListener([this]() {
        if (m_inDetail) showDetail(m_detailPlaylistId);
        else showGrid();
    });

    return true;
}

void MenuMusicPlaylistsPopup::onExit() {
    if (m_libListenerToken) {
        MenuMusicLibrary::get().removeListener(m_libListenerToken);
        m_libListenerToken = 0;
    }
    Popup::onExit();
}

// Header

void MenuMusicPlaylistsPopup::buildHeader() {
    auto size = m_mainLayer->getContentSize();

    m_nameInput = TextInput::create(size.width * 0.55f, "New playlist name");
    if (m_nameInput) {
        m_nameInput->setPosition({size.width * 0.35f, size.height * 0.88f});
        m_nameInput->setID("name-input"_spr);
        m_mainLayer->addChild(m_nameInput, 3);
    }

    auto createSpr = ButtonSprite::create("Create", 70, true, "bigFont.fnt", "GJ_button_05.png", 22.f, 0.55f);
    if (createSpr) {
        auto btn = CCMenuItemSpriteExtra::create(createSpr, this,
            menu_selector(MenuMusicPlaylistsPopup::onCreatePlaylist));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.8f, size.height * 0.88f});
        menu->addChild(btn);
        menu->setID("create-menu"_spr);
        m_mainLayer->addChild(menu, 3);
    }

    // Back button (solo visible en modo detail).
    auto backSpr = ButtonSprite::create("Back", 60, true, "bigFont.fnt", "GJ_button_02.png", 20.f, 0.55f);
    if (backSpr) {
        auto btn = CCMenuItemSpriteExtra::create(backSpr, this,
            menu_selector(MenuMusicPlaylistsPopup::onBackToGrid));
        m_backMenu = CCMenu::create();
        m_backMenu->setPosition({size.width * 0.12f, size.height * 0.88f});
        m_backMenu->addChild(btn);
        m_backMenu->setID("back-menu"_spr);
        m_backMenu->setVisible(false);
        m_mainLayer->addChild(m_backMenu, 3);
    }
}

void MenuMusicPlaylistsPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    m_scroll = ScrollLayer::create({size.width - 30.f, size.height * 0.7f});
    if (!m_scroll) return;
    m_scroll->setPosition({15.f, size.height * 0.08f});
    m_scroll->m_contentLayer->setID("playlists-scroll-content"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

// Grid

void MenuMusicPlaylistsPopup::showGrid() {
    m_inDetail = false;
    m_detailPlaylistId.clear();
    if (m_backMenu) m_backMenu->setVisible(false);
    if (!m_scroll) return;

    m_scroll->m_contentLayer->removeAllChildren();
    auto& lib = MenuMusicLibrary::get();
    auto& playlists = lib.playlists();

    float cardW = m_scroll->getContentSize().width - 4.f;
    float y = 0.f;

    for (const auto& pl : playlists) {
        auto node = CCNode::create();
        node->setContentSize({cardW, kCardHeight});

        auto bg = CCLayerColor::create(
            pl.id == lib.activePlaylistId()
                ? ccc4(70, 110, 180, 220)
                : ccc4(40, 40, 60, 200)
        );
        bg->setContentSize(node->getContentSize());
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition(node->getContentSize() / 2);
        bg->ignoreAnchorPointForPosition(false);
        node->addChild(bg, -1);

        auto nameLbl = CCLabelBMFont::create(pl.name.c_str(), "bigFont.fnt");
        if (nameLbl) {
            nameLbl->setScale(0.5f);
            nameLbl->setAnchorPoint({0.f, 0.5f});
            nameLbl->setPosition({10.f, kCardHeight * 0.7f});
            nameLbl->limitLabelWidth(cardW * 0.55f, 0.5f, 0.3f);
            node->addChild(nameLbl);
        }
        auto countLbl = CCLabelBMFont::create(
            fmt::format("{} track{}", pl.trackIds.size(),
                pl.trackIds.size() == 1 ? "" : "s").c_str(),
            "chatFont.fnt");
        if (countLbl) {
            countLbl->setScale(0.4f);
            countLbl->setAnchorPoint({0.f, 0.5f});
            countLbl->setPosition({10.f, kCardHeight * 0.3f});
            countLbl->setColor({200, 200, 220});
            node->addChild(countLbl);
        }

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});

        auto viewSpr = ButtonSprite::create("View", 50, true, "bigFont.fnt", "GJ_button_01.png", 18.f, 0.5f);
        if (viewSpr) {
            auto b = CCMenuItemSpriteExtra::create(viewSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onOpenPlaylist));
            b->setUserObject(CCString::create(pl.id.c_str()));
            b->setPosition({cardW - 140.f, kCardHeight / 2.f});
            menu->addChild(b);
        }
        auto useSpr = ButtonSprite::create(
            pl.id == lib.activePlaylistId() ? "Active" : "Use",
            50, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.5f);
        if (useSpr) {
            auto b = CCMenuItemSpriteExtra::create(useSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onActivatePlaylist));
            b->setUserObject(CCString::create(pl.id.c_str()));
            b->setPosition({cardW - 88.f, kCardHeight / 2.f});
            menu->addChild(b);
        }
        auto delSpr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
        if (delSpr) {
            delSpr->setScale(0.55f);
            auto b = CCMenuItemSpriteExtra::create(delSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onDeletePlaylist));
            b->setUserObject(CCString::create(pl.id.c_str()));
            b->setPosition({cardW - 22.f, kCardHeight / 2.f});
            menu->addChild(b);
        }
        node->addChild(menu, 2);
        node->setPosition({2.f, y});
        m_scroll->m_contentLayer->addChild(node);
        y += kCardHeight + kCardGap;
    }

    if (playlists.empty()) {
        auto lbl = CCLabelBMFont::create(
            "No playlists yet. Type a name and hit Create.",
            "chatFont.fnt");
        if (lbl) {
            lbl->setScale(0.5f);
            lbl->setPosition({m_scroll->getContentSize().width / 2.f,
                              m_scroll->getContentSize().height / 2.f});
            lbl->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(lbl);
        }
        y = std::max(y, m_scroll->getContentSize().height);
    }

    m_scroll->m_contentLayer->setContentHeight(std::max(y, m_scroll->getContentSize().height));
    m_scroll->scrollToTop();
}

// Detail

void MenuMusicPlaylistsPopup::showDetail(const std::string& playlistId) {
    m_inDetail = true;
    m_detailPlaylistId = playlistId;
    if (m_backMenu) m_backMenu->setVisible(true);
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto& lib = MenuMusicLibrary::get();
    auto* pl = lib.findPlaylist(playlistId);
    if (!pl) { showGrid(); return; }

    float cardW = m_scroll->getContentSize().width - 4.f;
    float y = 0.f;

    for (const auto& tid : pl->trackIds) {
        auto* track = lib.findTrack(tid);
        if (!track) continue;

        auto node = CCNode::create();
        node->setContentSize({cardW, kCardHeight});

        auto bg = CCLayerColor::create(ccc4(40, 50, 70, 200));
        bg->setContentSize(node->getContentSize());
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition(node->getContentSize() / 2);
        bg->ignoreAnchorPointForPosition(false);
        node->addChild(bg, -1);

        auto lbl = CCLabelBMFont::create(track->displayName.c_str(), "bigFont.fnt");
        if (lbl) {
            lbl->setScale(0.45f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({10.f, kCardHeight / 2.f});
            lbl->limitLabelWidth(cardW * 0.7f, 0.45f, 0.3f);
            node->addChild(lbl);
        }

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        auto delSpr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
        if (delSpr) {
            delSpr->setScale(0.55f);
            auto b = CCMenuItemSpriteExtra::create(delSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onRemoveFromPlaylist));
            b->setUserObject(CCString::create(tid.c_str()));
            b->setPosition({cardW - 20.f, kCardHeight / 2.f});
            menu->addChild(b);
        }
        node->addChild(menu, 2);
        node->setPosition({2.f, y});
        m_scroll->m_contentLayer->addChild(node);
        y += kCardHeight + kCardGap;
    }

    if (pl->trackIds.empty()) {
        auto lbl = CCLabelBMFont::create(
            "Empty playlist. Use the + icon from the Library to add tracks.",
            "chatFont.fnt");
        if (lbl) {
            lbl->setScale(0.45f);
            lbl->setPosition({m_scroll->getContentSize().width / 2.f,
                              m_scroll->getContentSize().height / 2.f});
            lbl->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(lbl);
        }
        y = std::max(y, m_scroll->getContentSize().height);
    }

    m_scroll->m_contentLayer->setContentHeight(std::max(y, m_scroll->getContentSize().height));
    m_scroll->scrollToTop();
}

// Callbacks

void MenuMusicPlaylistsPopup::onCreatePlaylist(CCObject*) {
    if (!m_nameInput) return;
    auto name = m_nameInput->getString();
    if (name.empty()) {
        Notification::create("Enter a name first.", NotificationIcon::Warning)->show();
        return;
    }
    auto& lib = MenuMusicLibrary::get();
    MusicPlaylist pl;
    pl.id = lib.generateId("pl");
    pl.name = name;
    pl.createdUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    lib.addPlaylist(pl);
    m_nameInput->setString("");
    Notification::create("Playlist created.", NotificationIcon::Success)->show();
}

void MenuMusicPlaylistsPopup::onActivatePlaylist(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    auto& lib = MenuMusicLibrary::get();
    lib.setActivePlaylistId(idStr->getCString());
    MenuMusicPlayer::get().setMode(PlaybackMode::Playlist, true);
    Notification::create("Playlist activated.", NotificationIcon::Success)->show();
}

void MenuMusicPlaylistsPopup::onOpenPlaylist(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    showDetail(idStr->getCString());
}

void MenuMusicPlaylistsPopup::onDeletePlaylist(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    std::string id = idStr->getCString();
    geode::createQuickPopup(
        "Delete Playlist",
        "This cannot be undone (tracks stay in your library).",
        "Cancel", "Delete",
        [id](FLAlertLayer*, bool confirm) {
            if (!confirm) return;
            MenuMusicLibrary::get().removePlaylist(id);
        }
    );
}

void MenuMusicPlaylistsPopup::onRemoveFromPlaylist(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    if (m_detailPlaylistId.empty()) return;
    MenuMusicLibrary::get().removeTrackFromPlaylist(m_detailPlaylistId, idStr->getCString());
}

void MenuMusicPlaylistsPopup::onBackToGrid(CCObject*) {
    showGrid();
}

} // namespace paimon::menumusic
