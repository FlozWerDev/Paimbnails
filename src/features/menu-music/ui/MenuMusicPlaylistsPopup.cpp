#include "MenuMusicPlaylistsPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

static constexpr float kCardHeight = 48.f;
static constexpr float kTrackHeight = 42.f;
static constexpr float kCardGap = 4.f;

MenuMusicPlaylistsPopup* MenuMusicPlaylistsPopup::create() {
    auto ret = new MenuMusicPlaylistsPopup();
    if (ret && ret->init(430.f, 290.f)) {
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

void MenuMusicPlaylistsPopup::buildHeader() {
    auto size = m_mainLayer->getContentSize();
    const float headerY = size.height - 40.f;

    m_nameInput = TextInput::create(260.f, "New playlist name");
    if (m_nameInput) {
        m_nameInput->setPosition({150.f, headerY});
        m_nameInput->setID("name-input"_spr);
        m_mainLayer->addChild(m_nameInput, 3);
    }

    auto createSpr = ButtonSprite::create(
        "Create", 78, true, "bigFont.fnt", "GJ_button_05.png", 20.f, 0.48f);
    if (createSpr) {
        auto btn = CCMenuItemSpriteExtra::create(createSpr, this,
            menu_selector(MenuMusicPlaylistsPopup::onCreatePlaylist));
        m_createMenu = CCMenu::create();
        m_createMenu->setPosition({356.f, headerY});
        m_createMenu->addChild(btn);
        m_createMenu->setID("create-menu"_spr);
        m_mainLayer->addChild(m_createMenu, 3);
    }

    auto backSpr = ButtonSprite::create(
        "Back", 64, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.45f);
    if (backSpr) {
        auto btn = CCMenuItemSpriteExtra::create(backSpr, this,
            menu_selector(MenuMusicPlaylistsPopup::onBackToGrid));
        m_backMenu = CCMenu::create();
        m_backMenu->setPosition({52.f, headerY});
        m_backMenu->addChild(btn);
        m_backMenu->setID("back-menu"_spr);
        m_backMenu->setVisible(false);
        m_mainLayer->addChild(m_backMenu, 3);
    }

    m_detailTitleLabel = CCLabelBMFont::create("", "bigFont.fnt");
    if (m_detailTitleLabel) {
        m_detailTitleLabel->setAnchorPoint({0.f, 0.5f});
        m_detailTitleLabel->setPosition({94.f, headerY});
        m_detailTitleLabel->setVisible(false);
        m_detailTitleLabel->setID("detail-title"_spr);
        m_mainLayer->addChild(m_detailTitleLabel, 3);
    }
}

void MenuMusicPlaylistsPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    const float scrollHeight = size.height - 84.f;
    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
        panel->setContentSize({size.width - 20.f, scrollHeight + 6.f});
        panel->setPosition({size.width / 2.f, 12.f + scrollHeight / 2.f});
        panel->setOpacity(220);
        panel->setID("playlists-list-bg"_spr);
        m_mainLayer->addChild(panel, 1);
    }

    m_scroll = ScrollLayer::create({size.width - 30.f, scrollHeight});
    if (!m_scroll) return;
    m_scroll->setPosition({15.f, 12.f});
    m_scroll->m_contentLayer->setID("playlists-scroll-content"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

void MenuMusicPlaylistsPopup::showGrid() {
    m_inDetail = false;
    m_detailPlaylistId.clear();
    if (m_backMenu) m_backMenu->setVisible(false);
    if (m_detailTitleLabel) m_detailTitleLabel->setVisible(false);
    if (m_nameInput) m_nameInput->setVisible(true);
    if (m_createMenu) m_createMenu->setVisible(true);
    if (!m_scroll) return;

    m_scroll->m_contentLayer->removeAllChildren();
    auto& lib = MenuMusicLibrary::get();
    auto& playlists = lib.playlists();

    const float cardW = m_scroll->getContentSize().width - 4.f;
    float contentHeight = static_cast<float>(playlists.size()) * (kCardHeight + kCardGap);
    if (contentHeight > 0.f) contentHeight -= kCardGap;
    contentHeight = std::max(contentHeight, m_scroll->getContentSize().height);
    float y = contentHeight - kCardHeight;

    for (const auto& pl : playlists) {
        const bool active = pl.id == lib.activePlaylistId();
        auto node = CCNode::create();
        node->setContentSize({cardW, kCardHeight});
        node->setAnchorPoint({0.f, 0.f});
        node->setID(fmt::format("playlist-card-{}", pl.id).c_str());

        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
            bg->setContentSize({cardW, kCardHeight});
            bg->setAnchorPoint({0.f, 0.f});
            bg->setPosition({0.f, 0.f});
            bg->setOpacity(active ? 245 : 215);
            if (active) bg->setColor({135, 215, 145});
            bg->setID("playlist-card-bg"_spr);
            node->addChild(bg, 0);
        }

        auto nameLbl = CCLabelBMFont::create(pl.name.c_str(), "bigFont.fnt");
        if (nameLbl) {
            nameLbl->setAnchorPoint({0.f, 0.5f});
            nameLbl->setPosition({12.f, kCardHeight * 0.66f});
            nameLbl->limitLabelWidth(cardW - 175.f, 0.5f, 0.3f);
            nameLbl->setColor(active ? ccColor3B{120, 245, 140}
                                     : ccColor3B{255, 255, 255});
            node->addChild(nameLbl, 1);
        }
        auto countText = fmt::format("{} track{}{}", pl.trackIds.size(),
            pl.trackIds.size() == 1 ? "" : "s", active ? " - active" : "");
        auto countLbl = CCLabelBMFont::create(countText.c_str(), "chatFont.fnt");
        if (countLbl) {
            countLbl->setScale(0.36f);
            countLbl->setAnchorPoint({0.f, 0.5f});
            countLbl->setPosition({12.f, kCardHeight * 0.28f});
            countLbl->setColor(active ? ccColor3B{180, 245, 185}
                                      : ccColor3B{220, 205, 175});
            node->addChild(countLbl, 1);
        }

        auto menu = CCMenu::create();
        menu->setContentSize({148.f, kCardHeight});
        menu->setAnchorPoint({1.f, 0.5f});
        menu->setPosition({cardW - 5.f, kCardHeight / 2.f});
        menu->ignoreAnchorPointForPosition(false);

        auto viewSpr = ButtonSprite::create(
            "Open", 48, true, "bigFont.fnt", "GJ_button_01.png", 16.f, 0.4f);
        if (viewSpr) {
            auto b = CCMenuItemSpriteExtra::create(viewSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onOpenPlaylist));
            b->setUserObject(CCString::create(pl.id.c_str()));
            b->setPosition({26.f, kCardHeight / 2.f});
            b->setID("open-playlist-btn"_spr);
            menu->addChild(b);
        }
        auto useSpr = ButtonSprite::create(
            active ? "Active" : "Use",
            52, true, "bigFont.fnt", "GJ_button_04.png", 16.f, 0.4f);
        if (useSpr) {
            auto b = CCMenuItemSpriteExtra::create(useSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onActivatePlaylist));
            b->setUserObject(CCString::create(pl.id.c_str()));
            b->setPosition({80.f, kCardHeight / 2.f});
            b->setID("activate-playlist-btn"_spr);
            menu->addChild(b);
        }
        auto delSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_trashBtn_001.png");
        if (!delSpr) {
            delSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteBtn_001.png");
        }
        if (delSpr) {
            delSpr->setScale(0.42f);
            auto b = CCMenuItemSpriteExtra::create(delSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onDeletePlaylist));
            b->setUserObject(CCString::create(pl.id.c_str()));
            b->setPosition({130.f, kCardHeight / 2.f});
            b->setID("delete-playlist-btn"_spr);
            menu->addChild(b);
        }
        node->addChild(menu, 2);
        node->setPosition({2.f, y});
        m_scroll->m_contentLayer->addChild(node);
        y -= kCardHeight + kCardGap;
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
    }

    m_scroll->m_contentLayer->setContentHeight(contentHeight);
    m_scroll->scrollToTop();
}

void MenuMusicPlaylistsPopup::showDetail(const std::string& playlistId) {
    m_inDetail = true;
    m_detailPlaylistId = playlistId;
    if (m_backMenu) m_backMenu->setVisible(true);
    if (m_nameInput) m_nameInput->setVisible(false);
    if (m_createMenu) m_createMenu->setVisible(false);
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto& lib = MenuMusicLibrary::get();
    auto* pl = lib.findPlaylist(playlistId);
    if (!pl) { showGrid(); return; }
    if (m_detailTitleLabel) {
        m_detailTitleLabel->setString(pl->name.c_str());
        m_detailTitleLabel->limitLabelWidth(310.f, 0.5f, 0.3f);
        m_detailTitleLabel->setVisible(true);
    }

    const float cardW = m_scroll->getContentSize().width - 4.f;
    std::size_t validTracks = 0;
    for (const auto& trackId : pl->trackIds) {
        if (lib.findTrack(trackId)) ++validTracks;
    }
    float contentHeight = static_cast<float>(validTracks) * (kTrackHeight + kCardGap);
    if (contentHeight > 0.f) contentHeight -= kCardGap;
    contentHeight = std::max(contentHeight, m_scroll->getContentSize().height);
    float y = contentHeight - kTrackHeight;

    for (const auto& tid : pl->trackIds) {
        auto* track = lib.findTrack(tid);
        if (!track) continue;

        auto node = CCNode::create();
        node->setContentSize({cardW, kTrackHeight});
        node->setAnchorPoint({0.f, 0.f});
        node->setID(fmt::format("playlist-track-{}", tid).c_str());

        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
            bg->setContentSize({cardW, kTrackHeight});
            bg->setAnchorPoint({0.f, 0.f});
            bg->setPosition({0.f, 0.f});
            bg->setOpacity(210);
            bg->setID("playlist-track-bg"_spr);
            node->addChild(bg, 0);
        }

        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(
                "GJ_musicOnBtn_001.png")) {
            icon->setScale(0.34f);
            icon->setPosition({22.f, kTrackHeight / 2.f});
            icon->setOpacity(210);
            node->addChild(icon, 1);
        }

        auto lbl = CCLabelBMFont::create(track->displayName.c_str(), "bigFont.fnt");
        if (lbl) {
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({44.f, kTrackHeight * 0.65f});
            lbl->limitLabelWidth(cardW - 92.f, 0.44f, 0.28f);
            node->addChild(lbl, 1);
        }

        std::string subtitle = track->artist.empty() ? "My Songs" : track->artist;
        auto subLbl = CCLabelBMFont::create(subtitle.c_str(), "chatFont.fnt");
        if (subLbl) {
            subLbl->setScale(0.34f);
            subLbl->setAnchorPoint({0.f, 0.5f});
            subLbl->setPosition({44.f, kTrackHeight * 0.25f});
            subLbl->setColor({220, 205, 175});
            node->addChild(subLbl, 1);
        }

        auto menu = CCMenu::create();
        menu->setContentSize({42.f, kTrackHeight});
        menu->setAnchorPoint({1.f, 0.5f});
        menu->setPosition({cardW - 4.f, kTrackHeight / 2.f});
        menu->ignoreAnchorPointForPosition(false);
        auto delSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_trashBtn_001.png");
        if (!delSpr) {
            delSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteBtn_001.png");
        }
        if (delSpr) {
            delSpr->setScale(0.42f);
            auto b = CCMenuItemSpriteExtra::create(delSpr, this,
                menu_selector(MenuMusicPlaylistsPopup::onRemoveFromPlaylist));
            b->setUserObject(CCString::create(tid.c_str()));
            b->setPosition({21.f, kTrackHeight / 2.f});
            b->setID("remove-track-btn"_spr);
            menu->addChild(b);
        }
        node->addChild(menu, 2);
        node->setPosition({2.f, y});
        m_scroll->m_contentLayer->addChild(node);
        y -= kTrackHeight + kCardGap;
    }

    if (validTracks == 0) {
        auto lbl = CCLabelBMFont::create(
            "Empty playlist. Open 'My Songs' and press + on a song to add it here.",
            "chatFont.fnt");
        if (lbl) {
            lbl->setScale(0.45f);
            lbl->setPosition({m_scroll->getContentSize().width / 2.f,
                              m_scroll->getContentSize().height / 2.f});
            lbl->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(lbl);
        }
    }

    m_scroll->m_contentLayer->setContentHeight(contentHeight);
    m_scroll->scrollToTop();
}

void MenuMusicPlaylistsPopup::onCreatePlaylist(CCObject*) {
    if (!m_nameInput) return;
    auto name = geode::utils::string::trim(m_nameInput->getString());
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
    // La primera playlist se vuelve la activa automaticamente para que el
    // modo "Playlist" funcione sin un paso extra de "Use".
    if (lib.activePlaylistId().empty()) {
        lib.setActivePlaylistId(pl.id);
    }
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
    PopupManager::get().quickPopup(
        "Delete Playlist",
        "This cannot be undone (tracks stay in your library).",
        "Cancel", "Delete",
        [id](FLAlertLayer*, bool confirm) {
            if (!confirm) return;
            MenuMusicLibrary::get().removePlaylist(id);
        }
    ).showInstant();
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
