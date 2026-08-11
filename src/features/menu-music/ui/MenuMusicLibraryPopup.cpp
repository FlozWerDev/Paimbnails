#include "MenuMusicLibraryPopup.hpp"
#include "MenuMusicAddPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../services/NewgroundsCatalog.hpp"
#include "../services/SongCoverCache.hpp"
#include "../services/YtDlpDownloader.hpp"
#include "../../menu-loop/services/MenuLoopControl.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/TextureBudget.hpp"
#include "../../../utils/FileDialog.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fmt/format.h>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace paimon::menumusic {

static constexpr float kPopupWidth  = 430.f;
static constexpr float kPopupHeight = 290.f;

static constexpr float kCardHeight = 48.f;
static constexpr float kCompactCardHeight = 38.f;
static constexpr float kCardGap = 4.f;

namespace {
    bool isTrackAvailable(const MusicTrack& track) {
        std::error_code ec;
        return !track.audioPath.empty()
            && std::filesystem::is_regular_file(track.audioPath, ec) && !ec;
    }

    int geometryDashSongId(const MusicTrack& track) {
        if (track.source != TrackSource::GeometryDash) return 0;

        auto stem = geode::utils::string::pathToString(
            std::filesystem::path(track.audioPath).stem());
        if (auto songId = parseNewgroundsSongId(stem); songId > 0) return songId;

        constexpr std::string_view prefix = "gd_";
        if (track.id.starts_with(prefix)) {
            return parseNewgroundsSongId(track.id.substr(prefix.size()));
        }
        return 0;
    }

    bool canRedownload(const MusicTrack& track) {
        return (track.source == TrackSource::Downloaded && !track.sourceUrl.empty())
            || geometryDashSongId(track) > 0;
    }

    std::string coverPathForTrack(const MusicTrack& track) {
        std::error_code ec;
        if (!track.coverPath.empty()
            && std::filesystem::is_regular_file(track.coverPath, ec) && !ec) {
            return track.coverPath;
        }

        const int songId = geometryDashSongId(track);
        if (songId <= 0) return {};

        auto covers = SongCoverCache::get().getCachedCoverPaths(songId);
        return covers.empty() ? std::string{} : covers.front();
    }

    const char* sourceName(TrackSource source) {
        switch (source) {
            case TrackSource::Downloaded:   return "downloaded";
            case TrackSource::GeometryDash: return "Geometry Dash";
            case TrackSource::Vanilla:      return "vanilla";
            default:                        return "local";
        }
    }
}

MenuMusicLibraryPopup* MenuMusicLibraryPopup::create() {
    auto ret = new MenuMusicLibraryPopup();
    if (ret && ret->init(kPopupWidth, kPopupHeight)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuMusicLibraryPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("My Songs");

    MenuMusicLibrary::get().load();
    MenuMusicLibrary::get().syncDownloadedSongs();
    m_sortMode = Mod::get()->getSavedValue<std::string>(
        "menuLoopSortMode", "alphabetical");
    m_localOnly = Mod::get()->getSavedValue<bool>(
        "menuLoopLocalOnlyFilter", false);
    m_favoritesOnly = Mod::get()->getSavedValue<bool>(
        "menuLoopFavoritesOnlyFilter", false);
    m_blacklistedOnly = Mod::get()->getSavedValue<bool>(
        "menuLoopShowBlacklisted", false);
    m_sortReverse = Mod::get()->getSavedValue<bool>("menuLoopSortReverse", false);
    m_compact = Mod::get()->getSavedValue<bool>("menuLoopCompactSongList", false);

    buildHeader();
    buildList();
    rebuildList();

    m_libListenerToken = MenuMusicLibrary::get().addListener([this]() {
        this->rebuildList();
    });
    // Refrescar el marcador "Playing" cuando cambia el track.
    m_playerListenerToken = MenuMusicPlayer::get().addListener(
        [this](const std::string&) { this->rebuildList(); });

    return true;
}

void MenuMusicLibraryPopup::onExit() {
    if (m_libListenerToken) {
        MenuMusicLibrary::get().removeListener(m_libListenerToken);
        m_libListenerToken = 0;
    }
    if (m_playerListenerToken) {
        MenuMusicPlayer::get().removeListener(m_playerListenerToken);
        m_playerListenerToken = 0;
    }
    if (m_searchBar) {
        m_searchBar->setCallback(nullptr);
    }
    Popup::onExit();
}

void MenuMusicLibraryPopup::buildHeader() {
    auto size = m_mainLayer->getContentSize();

    const float headerRowY = size.height - 40.f;

    m_searchBar = TextInput::create(195.f, "Search songs...");
    if (m_searchBar) {
        m_searchBar->setPosition({112.f, headerRowY});
        m_searchBar->setCallback([this](const std::string& s) {
            this->onSearchChanged(s);
        });
        m_searchBar->setID("search-input"_spr);
        m_mainLayer->addChild(m_searchBar, 2);
    }

    auto actionMenu = CCMenu::create();
    actionMenu->setContentSize({164.f, 24.f});
    actionMenu->setPosition({size.width - 94.f, headerRowY});
    auto addAction = [&](const char* text, int width, SEL_MenuHandler handler,
                         const char* bg, const char* id) {
        auto* spr = ButtonSprite::create(
            text, width, true, "bigFont.fnt", bg, 18.f, 0.4f);
        if (!spr) return;
        if (auto* btn = CCMenuItemSpriteExtra::create(spr, this, handler)) {
            btn->setID(id);
            actionMenu->addChild(btn);
        }
    };
    addAction("Add", 46, menu_selector(MenuMusicLibraryPopup::onAddMusic),
        "GJ_button_01.png", "library-add-btn");
    addAction("Folder", 58, menu_selector(MenuMusicLibraryPopup::onImportFolder),
        "GJ_button_05.png", "library-folder-btn");
    addAction("Sync", 46, menu_selector(MenuMusicLibraryPopup::onSyncGeometryDash),
        "GJ_button_02.png", "library-sync-btn");
    actionMenu->setLayout(RowLayout::create()->setGap(5.f));
    actionMenu->updateLayout();
    actionMenu->setID("library-actions"_spr);
    m_mainLayer->addChild(actionMenu, 2);

    auto filterMenu = CCMenu::create();
    filterMenu->setContentSize({286.f, 22.f});
    filterMenu->setPosition({158.f, size.height - 67.f});
    auto addFilter = [&](const char* text, int width, SEL_MenuHandler handler,
                         ButtonSprite** output) {
        auto* spr = ButtonSprite::create(
            text, width, true, "bigFont.fnt", "GJ_button_04.png", 15.f, 0.34f);
        if (!spr) return;
        if (auto* btn = CCMenuItemSpriteExtra::create(spr, this, handler)) {
            filterMenu->addChild(btn);
            *output = spr;
        }
    };
    addFilter("A-Z", 44, menu_selector(MenuMusicLibraryPopup::onCycleSort), &m_sortSprite);
    addFilter("Local", 44, menu_selector(MenuMusicLibraryPopup::onToggleLocalOnly),
        &m_localSprite);
    addFilter("Favs", 40, menu_selector(MenuMusicLibraryPopup::onToggleFavoritesOnly),
        &m_favoritesSprite);
    addFilter("Blocked", 52, menu_selector(MenuMusicLibraryPopup::onToggleBlacklistedOnly),
        &m_blacklistedSprite);
    addFilter("Rev", 36, menu_selector(MenuMusicLibraryPopup::onToggleSortReverse),
        &m_reverseSprite);
    addFilter("Compact", 50, menu_selector(MenuMusicLibraryPopup::onToggleCompact),
        &m_compactSprite);
    filterMenu->setLayout(RowLayout::create()->setGap(4.f));
    filterMenu->updateLayout();
    filterMenu->setID("library-filters"_spr);
    m_mainLayer->addChild(filterMenu, 2);

    auto navMenu = CCMenu::create();
    navMenu->setContentSize({116.f, 22.f});
    navMenu->setPosition({size.width - 68.f, size.height - 67.f});
    auto addNav = [&](const char* text, int width, SEL_MenuHandler handler) {
        auto* spr = ButtonSprite::create(
            text, width, true, "bigFont.fnt", "GJ_button_05.png", 15.f, 0.32f);
        if (!spr) return;
        if (auto* btn = CCMenuItemSpriteExtra::create(spr, this, handler)) {
            navMenu->addChild(btn);
        }
    };
    addNav("Top", 34, menu_selector(MenuMusicLibraryPopup::onScrollTop));
    addNav("Now", 40, menu_selector(MenuMusicLibraryPopup::onScrollCurrent));
    addNav("End", 34, menu_selector(MenuMusicLibraryPopup::onScrollBottom));
    navMenu->setLayout(RowLayout::create()->setGap(4.f));
    navMenu->updateLayout();
    navMenu->setID("library-navigation"_spr);
    m_mainLayer->addChild(navMenu, 2);
    refreshFilterButtons();
}

void MenuMusicLibraryPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    const float scrollH = size.height - 94.f;
    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
        panel->setContentSize({size.width - 20.f, scrollH + 6.f});
        panel->setPosition({size.width / 2.f, 12.f + scrollH / 2.f});
        panel->setOpacity(220);
        panel->setID("library-list-bg"_spr);
        m_mainLayer->addChild(panel, 1);
    }

    m_scroll = ScrollLayer::create({size.width - 30.f, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({15.f, 12.f});
    m_scroll->m_contentLayer->setID("library-scroll-content"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

static bool matchesQuery(const MusicTrack& t, const std::string& qLower) {
    if (qLower.empty()) return true;
    auto nameLower = geode::utils::string::toLower(t.displayName);
    if (nameLower.find(qLower) != std::string::npos) return true;
    auto artistLower = geode::utils::string::toLower(t.artist);
    if (artistLower.find(qLower) != std::string::npos) return true;
    return false;
}

void MenuMusicLibraryPopup::rebuildList() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto& lib = MenuMusicLibrary::get();
    auto& tracks = lib.tracks();

    std::vector<std::string> orderedIds;
    orderedIds.reserve(tracks.size());
    for (const auto& t : tracks) orderedIds.push_back(t.id);
    auto alphaLess = [&](const MusicTrack& a, const MusicTrack& b) {
        return geode::utils::string::toLower(a.displayName) <
               geode::utils::string::toLower(b.displayName);
    };
    auto fileSize = [](const MusicTrack& track) {
        std::error_code ec;
        auto size = std::filesystem::file_size(track.audioPath, ec);
        return ec ? std::uintmax_t{} : size;
    };
    auto trackLess = [&](const MusicTrack& a, const MusicTrack& b) {
        if (m_sortMode == "date") {
            if (a.addedUnixMs != b.addedUnixMs) return a.addedUnixMs < b.addedUnixMs;
        } else if (m_sortMode == "length") {
            if (a.durationMs != b.durationMs) return a.durationMs < b.durationMs;
        } else if (m_sortMode == "size") {
            const auto aSize = fileSize(a);
            const auto bSize = fileSize(b);
            if (aSize != bSize) return aSize < bSize;
        } else if (m_sortMode == "extension") {
            auto aExt = geode::utils::string::toLower(
                std::filesystem::path(a.audioPath).extension().string());
            auto bExt = geode::utils::string::toLower(
                std::filesystem::path(b.audioPath).extension().string());
            if (aExt != bExt) return aExt < bExt;
        }
        return alphaLess(a, b);
    };
    std::sort(orderedIds.begin(), orderedIds.end(), [&](const auto& a, const auto& b) {
        auto* ta = lib.findTrack(a);
        auto* tb = lib.findTrack(b);
        if (!ta || !tb) return false;
        return m_sortReverse ? trackLess(*tb, *ta) : trackLess(*ta, *tb);
    });

    auto qLower = geode::utils::string::toLower(m_query);

    // ScrollLayer children grow in +Y, so accumulate the total height first
    // and place each card at (totalH - y) to render newest-on-top.
    std::vector<std::string> visibleIds;
    for (const auto& tid : orderedIds) {
        auto* track = lib.findTrack(tid);
        if (!track) continue;
        if (!matchesQuery(*track, qLower)) continue;
        if (m_localOnly && track->source != TrackSource::Local) continue;
        if (m_favoritesOnly && !track->favorite) continue;
        if (m_blacklistedOnly != track->blacklisted) {
            if (m_blacklistedOnly || track->blacklisted) continue;
        }
        visibleIds.push_back(tid);
    }

    const float cardHeight = m_compact ? kCompactCardHeight : kCardHeight;
    const float cardW = m_scroll->getContentSize().width - 4.f;
    float totalH = static_cast<float>(visibleIds.size()) * (cardHeight + kCardGap);
    if (totalH > 0.f) totalH -= kCardGap;
    totalH = std::max(totalH, m_scroll->getContentSize().height);

    if (visibleIds.empty()) {
        const bool hasLocalTracks = std::any_of(
            tracks.begin(), tracks.end(), [](const MusicTrack& track) {
                return track.source == TrackSource::Local;
            });
        const char* message = tracks.empty()
            ? "Library empty - tap 'Add' to import or download a song."
            : m_localOnly && !hasLocalTracks
                ? "No local songs - tap 'Folder' to import music."
                : "No tracks match your filters.";
        auto label = CCLabelBMFont::create(message, "chatFont.fnt");
        if (label) {
            label->setScale(0.5f);
            label->setPosition({m_scroll->getContentSize().width / 2.f,
                                m_scroll->getContentSize().height / 2.f});
            label->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(label);
        }
    } else {
        float y = totalH - cardHeight;
        for (const auto& tid : visibleIds) {
            auto card = buildTrackCard(tid, cardW, cardHeight);
            if (!card) continue;
            card->setPosition({2.f, y});
            m_scroll->m_contentLayer->addChild(card);
            y -= (cardHeight + kCardGap);
        }
    }

    m_scroll->m_contentLayer->setContentHeight(totalH);
    if (Mod::get()->getSavedValue<bool>("menuLoopAutoScrollCurrent", true)) {
        onScrollCurrent(nullptr);
    } else {
        m_scroll->scrollToTop();
    }
}

cocos2d::CCNode* MenuMusicLibraryPopup::buildTrackCard(
    const std::string& trackId, float widthOverride, float cardHeight
) {
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(trackId);
    if (!track) return nullptr;

    const bool available = isTrackAvailable(*track);
    const bool downloadable = canRedownload(*track);
    const int gdSongId = geometryDashSongId(*track);
    const bool downloading = m_downloadingTrackIds.contains(trackId)
        || (gdSongId > 0 && isNewgroundsSongDownloading(gdSongId));
    const bool isPlayingNow = available
        && MenuMusicPlayer::get().state().currentTrackId == trackId
        && !trackId.empty();

    auto node = CCNode::create();
    node->setContentSize({widthOverride, cardHeight});
    node->setAnchorPoint({0, 0});
    node->setID(fmt::format("track-card-{}", trackId).c_str());

    if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
        bg->setContentSize({widthOverride, cardHeight});
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        bg->setOpacity(available ? 235 : 180);
        if (track->blacklisted) {
            bg->setColor({210, 115, 105});
        } else if (isPlayingNow) {
            bg->setColor({145, 220, 145});
        }
        bg->setID("card-bg"_spr);
        node->addChild(bg, 0);
    }

    const float bannerHeight = cardHeight - 10.f;
    const float bannerWidth = cardHeight * 1.42f;
    const float bannerX = 7.f;
    const float bannerY = (cardHeight - bannerHeight) / 2.f;

    if (auto* thumbBg = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
        thumbBg->setContentSize({bannerWidth, bannerHeight});
        thumbBg->setAnchorPoint({0.f, 0.f});
        thumbBg->setPosition({bannerX, bannerY});
        thumbBg->setOpacity(available ? 255 : 155);
        thumbBg->setID("card-thumb-bg"_spr);
        node->addChild(thumbBg, 1);
    }

    bool hasCover = false;
    auto coverPath = coverPathForTrack(*track);
    if (!coverPath.empty()) {
        if (auto* tex = paimon::image::loadBudgeted(coverPath)) {
            if (auto* cover = CCSprite::createWithTexture(tex)) {
                const float inset = 3.f;
                const float coverWidth = bannerWidth - inset * 2.f;
                const float coverHeight = bannerHeight - inset * 2.f;
                auto coverSize = cover->getContentSize();
                const float scale = std::max(
                    coverWidth / std::max(coverSize.width, 1.f),
                    coverHeight / std::max(coverSize.height, 1.f));
                cover->setScale(scale);
                cover->setPosition({coverWidth / 2.f, coverHeight / 2.f});
                cover->setOpacity(available ? 255 : 145);
                cover->setID("card-cover"_spr);

                auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(
                    coverWidth, coverHeight, 3.f);
                if (auto* clip = stencil ? CCClippingNode::create(stencil) : nullptr) {
                    clip->setContentSize({coverWidth, coverHeight});
                    clip->setAnchorPoint({0.f, 0.f});
                    clip->setPosition({bannerX + inset, bannerY + inset});
                    clip->setAlphaThreshold(0.05f);
                    clip->setID("card-cover-clip"_spr);
                    clip->addChild(cover);
                    node->addChild(clip, 2);
                    hasCover = true;
                }
            }
        }
    }
    if (!hasCover) {
        if (auto* music = paimon::SpriteHelper::safeCreateWithFrameName(
                "GJ_musicOnBtn_001.png")) {
            const float scale = bannerHeight * 0.52f
                / std::max(music->getContentSize().width, 1.f);
            music->setScale(scale);
            music->setPosition({bannerX + bannerWidth / 2.f, cardHeight / 2.f});
            music->setOpacity(available ? 255 : 140);
            node->addChild(music, 2);
        }
    }

    const float textX = bannerX + bannerWidth + 9.f;
    const float textAreaW = widthOverride - textX - 150.f;
    auto nameLbl = CCLabelBMFont::create(track->displayName.c_str(), "bigFont.fnt");
    if (nameLbl) {
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setColor(track->blacklisted
            ? cocos2d::ccColor3B{255, 120, 120}
            : track->favorite
                ? cocos2d::ccColor3B{255, 220, 90}
                : isPlayingNow
            ? cocos2d::ccColor3B{120, 240, 140}
            : cocos2d::ccColor3B{255, 255, 255});
        nameLbl->limitLabelWidth(textAreaW, 0.48f, 0.26f);
        nameLbl->setPosition({textX, cardHeight * 0.66f});
        nameLbl->setID("card-title"_spr);
        node->addChild(nameLbl, 4);
    }

    std::string durStr = track->durationMs > 0
        ? fmt::format("{}:{:02}", track->durationMs / 60000, (track->durationMs / 1000) % 60)
        : "-";
    std::string subStr;
    if (!available) {
        subStr = downloadable ? "Missing file - tap Download" : "Missing local file";
    } else {
        subStr = fmt::format("{} - {}", sourceName(track->source), durStr);
        if (!track->artist.empty()) {
            subStr = fmt::format("{} - {}", track->artist, subStr);
        }
    }

    auto subLbl = CCLabelBMFont::create(subStr.c_str(), "chatFont.fnt");
    if (subLbl) {
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setColor(available ? ccColor3B{235, 210, 175} : ccColor3B{255, 205, 95});
        subLbl->limitLabelWidth(textAreaW, 0.38f, 0.23f);
        subLbl->setPosition({textX, cardHeight * 0.30f});
        subLbl->setID("card-subtitle"_spr);
        node->addChild(subLbl, 4);
    }

    auto menu = CCMenu::create();
    menu->setContentSize({146.f, cardHeight});
    menu->setAnchorPoint({1.f, 0.5f});
    menu->setPosition({widthOverride - 5.f, cardHeight / 2.f});
    menu->ignoreAnchorPointForPosition(false);

    auto makeIconBtn = [&](const char* frame, const char* fallback,
                           SEL_MenuHandler handler, float scale,
                           const char* id, ccColor3B tint,
                           GLubyte opacity) -> CCMenuItemSpriteExtra* {
        auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (!sprite && fallback) {
            sprite = paimon::SpriteHelper::safeCreateWithFrameName(fallback);
        }
        if (!sprite) return nullptr;
        sprite->setScale(scale);
        sprite->setColor(tint);
        sprite->setOpacity(opacity);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, handler);
        if (button) {
            button->setUserObject(CCString::create(trackId.c_str()));
            button->setID(id);
        }
        return button;
    };

    if (downloading) {
        if (auto* spinner = CCSprite::create("loadingCircle.png")) {
            spinner->setScale(0.28f);
            spinner->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            spinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.f, 360.f)));
            menu->addChild(spinner);
        }
    } else if (available) {
        if (auto* button = makeIconBtn(
                "GJ_playMusicBtn_001.png", "GJ_playBtn2_001.png",
                menu_selector(MenuMusicLibraryPopup::onPlayTrack), 0.58f,
                "track-play-btn", {255, 255, 255}, 255)) {
            menu->addChild(button);
        }
    } else if (downloadable) {
        if (auto* button = makeIconBtn(
                "GJ_downloadBtn_001.png", "GJ_downloadsIcon_001.png",
                menu_selector(MenuMusicLibraryPopup::onDownloadTrack), 0.54f,
                "track-download-btn", {255, 255, 255}, 255)) {
            menu->addChild(button);
        }
    }

    if (auto* button = makeIconBtn(
            "GJ_plusBtn_001.png", "GJ_plus2Btn_001.png",
            menu_selector(MenuMusicLibraryPopup::onAddToPlaylist), 0.46f,
            "track-playlist-btn", {255, 255, 255}, 255)) {
        menu->addChild(button);
    }

    if (auto* button = makeIconBtn(
            "GJ_starBtn_001.png", "GJ_starsIcon_001.png",
            menu_selector(MenuMusicLibraryPopup::onToggleFavorite), 0.48f,
            "track-favorite-btn",
            track->favorite ? ccColor3B{255, 235, 95} : ccColor3B{255, 255, 255},
            track->favorite ? 255 : 165)) {
        menu->addChild(button);
    }

    if (auto* button = makeIconBtn(
            "GJ_reportBtn_001.png", "GJ_dislikeBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onToggleBlacklist), 0.44f,
            "track-blacklist-btn",
            track->blacklisted ? ccColor3B{255, 110, 110} : ccColor3B{255, 255, 255},
            track->blacklisted ? 255 : 175)) {
        menu->addChild(button);
    }

    if (auto* button = makeIconBtn(
            "GJ_trashBtn_001.png", "GJ_deleteIcon_001.png",
            menu_selector(MenuMusicLibraryPopup::onRemoveTrack), 0.44f,
            "track-remove-btn", {255, 255, 255}, 255)) {
        menu->addChild(button);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(4.f)
        ->setAxisAlignment(AxisAlignment::End)
        ->setCrossAxisAlignment(AxisAlignment::Center));
    menu->setID("card-actions"_spr);
    menu->updateLayout();
    node->addChild(menu, 5);

    return node;
}

void MenuMusicLibraryPopup::onSearchChanged(const std::string& query) {
    m_query = query;
    rebuildList();
}

void MenuMusicLibraryPopup::onPlayTrack(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    MenuMusicPlayer::get().playSpecific(idStr->getCString());
}

void MenuMusicLibraryPopup::onDownloadTrack(CCObject* sender) {
    auto* button = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto* idObject = button ? typeinfo_cast<CCString*>(button->getUserObject()) : nullptr;
    auto* track = idObject
        ? MenuMusicLibrary::get().findTrack(idObject->getCString())
        : nullptr;
    if (!track || isTrackAvailable(*track) || !canRedownload(*track)) return;

    const std::string id = track->id;
    auto weakThis = WeakRef<CCNode>(this);

    if (track->source == TrackSource::GeometryDash) {
        const int songId = geometryDashSongId(*track);
        if (songId <= 0 || isNewgroundsSongDownloading(songId)) return;

        downloadNewgroundsSong(songId,
            [weakThis, id](NewgroundsDownloadResult result) {
                if (result.success) {
                    auto& library = MenuMusicLibrary::get();
                    if (auto* current = library.findTrack(id)) {
                        auto updated = *current;
                        updated.audioPath = result.path;
                        library.updateTrack(updated);
                    }
                    Notification::create(
                        "Song downloaded again!", NotificationIcon::Success)->show();
                } else {
                    Notification::create(
                        result.error.empty() ? "Could not download this song." : result.error,
                        NotificationIcon::Error, 4.f)->show();
                }

                auto ref = weakThis.lock();
                auto* self = ref
                    ? typeinfo_cast<MenuMusicLibraryPopup*>(ref.data())
                    : nullptr;
                if (self) self->rebuildList();
            });
        rebuildList();
        return;
    }

    if (!m_downloadingTrackIds.insert(id).second) return;
    const std::string sourceUrl = track->sourceUrl;
    YtDlpDownloader::get().download(sourceUrl, id, {},
        [weakThis, id, sourceUrl](YtDlpResult result) {
            if (result.success) {
                auto& library = MenuMusicLibrary::get();
                if (auto* current = library.findTrack(id)) {
                    auto updated = *current;
                    updated.audioPath = result.audioPath;
                    if (!result.coverPath.empty()) updated.coverPath = result.coverPath;
                    if (!result.displayName.empty()) updated.displayName = result.displayName;
                    if (!result.artist.empty()) updated.artist = result.artist;
                    updated.sourceUrl = sourceUrl;
                    updated.source = TrackSource::Downloaded;
                    library.updateTrack(updated);
                }
                Notification::create(
                    "Song downloaded again!", NotificationIcon::Success)->show();
            } else {
                Notification::create(
                    result.error.empty() ? "Could not download this song." : result.error,
                    NotificationIcon::Error, 4.f)->show();
            }

            auto ref = weakThis.lock();
            auto* self = ref
                ? typeinfo_cast<MenuMusicLibraryPopup*>(ref.data())
                : nullptr;
            if (self) {
                self->m_downloadingTrackIds.erase(id);
                self->rebuildList();
            }
        });
    rebuildList();
}

void MenuMusicLibraryPopup::onRemoveTrack(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    std::string id = idStr->getCString();

    auto* track = MenuMusicLibrary::get().findTrack(id);
    if (!track) return;
    const bool keepFavorite = track->favorite
        && isTrackAvailable(*track) && canRedownload(*track);

    const char* title = keepFavorite ? "Delete Download" : "Remove Track";
    const char* message = keepFavorite
        ? "Delete the local audio file?\n<cg>The song stays in Favorites and can be downloaded again.</c>"
        : track->source == TrackSource::Downloaded
            ? "Remove this track from your library?\n<cy>Its downloaded file will also be deleted.</c>"
            : "Remove this track from your library?\n<cy>The original audio file will stay on disk.</c>";

    PopupManager::get().quickPopup(
        title, message, "Cancel", keepFavorite ? "Delete" : "Remove",
        [id, keepFavorite](FLAlertLayer*, bool confirm) {
            if (!confirm) return;
            auto& library = MenuMusicLibrary::get();
            if (!keepFavorite) {
                library.removeTrack(id, /*deleteFiles=*/true);
                return;
            }
            if (library.deleteLocalAudio(id)) {
                Notification::create(
                    "Download deleted. It is still in Favorites.",
                    NotificationIcon::Success)->show();
            } else {
                Notification::create(
                    "Could not delete the audio file. Stop playback and try again.",
                    NotificationIcon::Error, 4.f)->show();
            }
        }
    ).showInstant();
}

void MenuMusicLibraryPopup::onAddToPlaylist(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;

    auto& lib = MenuMusicLibrary::get();
    if (lib.playlists().empty()) {
        Notification::create("Create a playlist first (Playlists button).",
            NotificationIcon::Info)->show();
        return;
    }
    std::string plid = lib.activePlaylistId().empty()
        ? lib.playlists().front().id
        : lib.activePlaylistId();
    lib.addTrackToPlaylist(plid, idStr->getCString());
    auto* pl = lib.findPlaylist(plid);
    Notification::create(
        fmt::format("Added to playlist '{}'.", pl ? pl->name : "?"),
        NotificationIcon::Success)->show();
}

void MenuMusicLibraryPopup::onToggleFavorite(CCObject* sender) {
    auto* button = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto* id = button ? typeinfo_cast<CCString*>(button->getUserObject()) : nullptr;
    auto* track = id ? MenuMusicLibrary::get().findTrack(id->getCString()) : nullptr;
    if (track) MenuMusicLibrary::get().setFavorite(track->id, !track->favorite);
}

void MenuMusicLibraryPopup::onToggleBlacklist(CCObject* sender) {
    auto* button = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto* id = button ? typeinfo_cast<CCString*>(button->getUserObject()) : nullptr;
    auto* track = id ? MenuMusicLibrary::get().findTrack(id->getCString()) : nullptr;
    if (track) MenuMusicLibrary::get().setBlacklisted(track->id, !track->blacklisted);
}

void MenuMusicLibraryPopup::onAddMusic(CCObject*) {
    if (auto p = MenuMusicAddPopup::create()) p->show();
}

void MenuMusicLibraryPopup::onImportFolder(CCObject*) {
    pt::pickFolder([](Result<std::optional<std::filesystem::path>> result) {
        auto selected = std::move(result).unwrapOr(std::nullopt);
        if (!selected) return;
        const auto added = MenuMusicLibrary::get().importFolder(*selected, true);
        Notification::create(
            fmt::format("Imported {} song{}.", added, added == 1 ? "" : "s"),
            added ? NotificationIcon::Success : NotificationIcon::Info)->show();
    });
}

void MenuMusicLibraryPopup::onSyncGeometryDash(CCObject*) {
    const auto added = MenuMusicLibrary::get().syncDownloadedSongs();
    Notification::create(
        fmt::format("Synced {} Geometry Dash song{}.", added, added == 1 ? "" : "s"),
        added ? NotificationIcon::Success : NotificationIcon::Info)->show();
}

void MenuMusicLibraryPopup::onCycleSort(CCObject*) {
    static constexpr std::array modes = {
        "alphabetical", "date", "length", "size", "extension"
    };
    auto it = std::find(modes.begin(), modes.end(), m_sortMode);
    m_sortMode = it == modes.end() || ++it == modes.end() ? modes.front() : *it;
    Mod::get()->setSavedValue("menuLoopSortMode", m_sortMode);
    refreshFilterButtons();
    rebuildList();
}

void MenuMusicLibraryPopup::onToggleLocalOnly(CCObject*) {
    m_localOnly = !m_localOnly;
    Mod::get()->setSavedValue("menuLoopLocalOnlyFilter", m_localOnly);
    refreshFilterButtons();
    rebuildList();
}

void MenuMusicLibraryPopup::onToggleFavoritesOnly(CCObject*) {
    m_favoritesOnly = !m_favoritesOnly;
    if (m_favoritesOnly) m_blacklistedOnly = false;
    Mod::get()->setSavedValue("menuLoopFavoritesOnlyFilter", m_favoritesOnly);
    Mod::get()->setSavedValue("menuLoopShowBlacklisted", m_blacklistedOnly);
    refreshFilterButtons();
    rebuildList();
}

void MenuMusicLibraryPopup::onToggleBlacklistedOnly(CCObject*) {
    m_blacklistedOnly = !m_blacklistedOnly;
    if (m_blacklistedOnly) m_favoritesOnly = false;
    Mod::get()->setSavedValue("menuLoopShowBlacklisted", m_blacklistedOnly);
    Mod::get()->setSavedValue("menuLoopFavoritesOnlyFilter", m_favoritesOnly);
    refreshFilterButtons();
    rebuildList();
}

void MenuMusicLibraryPopup::onToggleSortReverse(CCObject*) {
    m_sortReverse = !m_sortReverse;
    Mod::get()->setSavedValue("menuLoopSortReverse", m_sortReverse);
    refreshFilterButtons();
    rebuildList();
}

void MenuMusicLibraryPopup::onToggleCompact(CCObject*) {
    m_compact = !m_compact;
    Mod::get()->setSavedValue("menuLoopCompactSongList", m_compact);
    refreshFilterButtons();
    rebuildList();
}

void MenuMusicLibraryPopup::onScrollTop(CCObject*) {
    if (m_scroll) m_scroll->scrollToTop();
}

void MenuMusicLibraryPopup::onScrollCurrent(CCObject*) {
    if (!m_scroll) return;
    const auto& currentId = MenuMusicPlayer::get().state().currentTrackId;
    auto* card = currentId.empty()
        ? nullptr
        : m_scroll->m_contentLayer->getChildByID(
            fmt::format("track-card-{}", currentId));
    if (!card) {
        m_scroll->scrollToTop();
        return;
    }

    const float viewHeight = m_scroll->getContentSize().height;
    const float contentHeight = m_scroll->m_contentLayer->getContentSize().height;
    const float target = viewHeight / 2.f
        - (card->getPositionY() + card->getContentSize().height / 2.f);
    m_scroll->m_contentLayer->setPositionY(
        std::clamp(target, std::min(0.f, viewHeight - contentHeight), 0.f));
}

void MenuMusicLibraryPopup::onScrollBottom(CCObject*) {
    if (m_scroll) m_scroll->m_contentLayer->setPositionY(0.f);
}

void MenuMusicLibraryPopup::keyDown(enumKeyCodes key, double timestamp) {
    if (key == enumKeyCodes::KEY_Escape) {
        onClose(nullptr);
        return;
    }
    if (key == enumKeyCodes::KEY_Home) {
        onScrollTop(nullptr);
        return;
    }
    if (key == enumKeyCodes::KEY_End) {
        onScrollBottom(nullptr);
        return;
    }

    if (!Mod::get()->getSettingValue<bool>("menuLoopEnableKeyboardShortcuts")) {
        Popup::keyDown(key, timestamp);
        return;
    }

#ifdef GEODE_IS_DESKTOP
    auto* keyboard = CCKeyboardDispatcher::get();
    if (!keyboard) {
        Popup::keyDown(key, timestamp);
        return;
    }
    const bool shift = keyboard->getShiftKeyPressed();
#ifdef GEODE_IS_MACOS
    const bool ctrl = keyboard->getCommandKeyPressed();
    const bool favoriteModifier = keyboard->getControlKeyPressed();
#else
    const bool ctrl = keyboard->getControlKeyPressed();
    const bool favoriteModifier = keyboard->getAltKeyPressed();
#endif

    if (key >= enumKeyCodes::KEY_Zero && key <= enumKeyCodes::KEY_Nine) {
        paimon::menuloop::MenuLoopControl::setSongPercentage(
            10 * (static_cast<int>(key) - static_cast<int>(enumKeyCodes::KEY_Zero)));
        return;
    }
    if (key >= enumKeyCodes::KEY_NumPad0 && key <= enumKeyCodes::KEY_NumPad9) {
        paimon::menuloop::MenuLoopControl::setSongPercentage(
            10 * (static_cast<int>(key) - static_cast<int>(enumKeyCodes::KEY_NumPad0)));
        return;
    }
    if ((ctrl && key == enumKeyCodes::KEY_S) || (shift && key == enumKeyCodes::KEY_N)
        || (ctrl && (key == enumKeyCodes::KEY_Right || key == enumKeyCodes::KEY_ArrowRight))) {
        MenuMusicPlayer::get().playNext();
        return;
    }
    if ((shift && key == enumKeyCodes::KEY_P)
        || (ctrl && (key == enumKeyCodes::KEY_Left || key == enumKeyCodes::KEY_ArrowLeft))) {
        MenuMusicPlayer::get().playPrevious();
        return;
    }
    if (ctrl && key == enumKeyCodes::KEY_R) {
        paimon::menuloop::MenuLoopControl::setSongPercentage(0);
        return;
    }
    if (ctrl && (key == enumKeyCodes::KEY_H || key == enumKeyCodes::KEY_K)) {
        onClose(nullptr);
        return;
    }
    if (shift && ctrl && key == enumKeyCodes::KEY_J) {
        onScrollCurrent(nullptr);
        return;
    }
    if (shift && favoriteModifier && key == enumKeyCodes::KEY_B) {
        auto* track = MenuMusicPlayer::get().currentTrack();
        if (track) MenuMusicLibrary::get().setFavorite(track->id, true);
        return;
    }
#endif
    if (key == enumKeyCodes::KEY_L || key == enumKeyCodes::KEY_Right
        || key == enumKeyCodes::KEY_ArrowRight) {
        paimon::menuloop::MenuLoopControl::skipForward();
        return;
    }
    if (key == enumKeyCodes::KEY_J || key == enumKeyCodes::KEY_Left
        || key == enumKeyCodes::KEY_ArrowLeft) {
        paimon::menuloop::MenuLoopControl::skipBackward();
        return;
    }
    Popup::keyDown(key, timestamp);
}

void MenuMusicLibraryPopup::refreshFilterButtons() {
    static const std::unordered_map<std::string, const char*> labels = {
        {"alphabetical", "A-Z"}, {"date", "Date"}, {"length", "Time"},
        {"size", "Size"}, {"extension", "Ext"},
    };
    if (m_sortSprite) {
        auto it = labels.find(m_sortMode);
        m_sortSprite->setString(it == labels.end() ? "A-Z" : it->second);
    }
    auto tint = [](ButtonSprite* sprite, bool active) {
        if (sprite && sprite->m_BGSprite) {
            sprite->m_BGSprite->setColor(active ? ccColor3B{120, 210, 150}
                                                : ccColor3B{190, 190, 205});
        }
    };
    tint(m_localSprite, m_localOnly);
    tint(m_favoritesSprite, m_favoritesOnly);
    tint(m_blacklistedSprite, m_blacklistedOnly);
    tint(m_reverseSprite, m_sortReverse);
    tint(m_compactSprite, m_compact);
}

} // namespace paimon::menumusic
