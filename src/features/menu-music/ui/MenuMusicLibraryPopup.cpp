#include "MenuMusicLibraryPopup.hpp"
#include "MenuMusicAddPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/DominantColors.hpp"
#include "../../../utils/stb_image.h"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace paimon::menumusic {

static constexpr float kPopupWidth  = 390.f;
static constexpr float kPopupHeight = 264.f;

static constexpr float kCardHeight = 56.f;
static constexpr float kCardGap    = 8.f;
static constexpr float kCardRadius = 8.f;
static constexpr float kThumbRadius = 6.f;

static constexpr cocos2d::ccColor4B kCardBaseColor = {24, 26, 38, 210};

static constexpr cocos2d::ccColor3B kFallbackDominantColor = {96, 84, 164};

namespace {
    struct CachedColor {
        bool ok = false;
        cocos2d::ccColor3B color{};
    };

    static std::unordered_map<std::string, CachedColor>& colorCache() {
        static std::unordered_map<std::string, CachedColor> cache;
        return cache;
    }

    static std::vector<uint8_t> readBinary(const std::string& path) {
        auto res = geode::utils::file::readBinary(path);
        if (!res) return {};
        return res.unwrap();
    }

    static CachedColor extractDominant(const std::string& coverPath) {
        auto it = colorCache().find(coverPath);
        if (it != colorCache().end()) return it->second;

        CachedColor out;
        auto bytes = readBinary(coverPath);
        if (bytes.empty()) {
            colorCache().emplace(coverPath, out);
            return out;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &w, &h, &ch, 3);
        if (!px || w <= 0 || h <= 0) {
            if (px) stbi_image_free(px);
            colorCache().emplace(coverPath, out);
            return out;
        }

        // Downsample to ~64x64 (box filter) — dominant color only needs a
        // general impression, not pixel precision.
        constexpr int targetDim = 64;
        int dsW = w, dsH = h;
        if (w > targetDim || h > targetDim) {
            const float ratio = std::max(
                static_cast<float>(w) / targetDim,
                static_cast<float>(h) / targetDim);
            dsW = std::max(1, static_cast<int>(w / ratio));
            dsH = std::max(1, static_cast<int>(h / ratio));
        }

        std::vector<uint8_t> ds;
        const uint8_t* rgbSrc = px;
        if (dsW != w || dsH != h) {
            ds.resize(static_cast<size_t>(dsW) * dsH * 3);
            for (int y = 0; y < dsH; ++y) {
                const int srcY = std::min(h - 1, y * h / dsH);
                for (int x = 0; x < dsW; ++x) {
                    const int srcX = std::min(w - 1, x * w / dsW);
                    const uint8_t* s = px + (srcY * w + srcX) * 3;
                    uint8_t* d = ds.data() + (y * dsW + x) * 3;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                }
            }
            rgbSrc = ds.data();
        }

        auto pair = DominantColors::extract(rgbSrc, dsW, dsH);
        stbi_image_free(px);

        out.ok = true;
        out.color = {pair.first.r, pair.first.g, pair.first.b};
        colorCache().emplace(coverPath, out);
        return out;
    }

    // Horizontal gradient drawn as N adjacent quads with interpolated colors.
    // Strips must share the exact same x at their boundary (no gap, no overlap):
    // overlapping semi-transparent quads sum their alpha and produce a bright
    // banding line.
    static PaimonDrawNode* buildHorizontalGradient(
        float width, float height,
        cocos2d::ccColor4F from,
        cocos2d::ccColor4F to,
        float fadePivot = 0.5f
    ) {
        auto node = PaimonDrawNode::create();
        if (!node) return nullptr;

        const int strips = 48;

        for (int i = 0; i < strips; ++i) {
            const float x0 = static_cast<float>(i)     / strips * width;
            const float x1 = static_cast<float>(i + 1) / strips * width;
            const float tm = (x0 + x1) * 0.5f / width;

            const float clamped = fadePivot > 0.f
                ? std::clamp(tm / fadePivot, 0.f, 1.f)
                : 1.f;
            const float s = clamped * clamped * (3.f - 2.f * clamped);

            cocos2d::ccColor4F c {
                from.r + (to.r - from.r) * s,
                from.g + (to.g - from.g) * s,
                from.b + (to.b - from.b) * s,
                from.a + (to.a - from.a) * s,
            };

            // 4 points counter-clockwise (cocos winding convention).
            cocos2d::CCPoint rect[4] = {
                {x0, 0},
                {x1, 0},
                {x1, height},
                {x0, height},
            };
            node->drawPolygon(rect, 4, c, 0.f, ccc4f(0, 0, 0, 0));
        }

        node->setContentSize({width, height});
        return node;
    }

    static cocos2d::ccColor3B normalizeDominant(cocos2d::ccColor3B in) {
        const int r = in.r, g = in.g, b = in.b;
        const int maxC = std::max({r, g, b});
        const int minC = std::min({r, g, b});
        const int luma = (299 * r + 587 * g + 114 * b) / 1000;

        if (luma < 24 || (maxC - minC) < 12) {
            return kFallbackDominantColor;
        }
        if (luma > 225) {
            return cocos2d::ccColor3B{
                static_cast<uint8_t>(r * 4 / 5),
                static_cast<uint8_t>(g * 4 / 5),
                static_cast<uint8_t>(b * 4 / 5)
            };
        }
        return in;
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

    const float headerRowY = size.height - 42.f;

    m_searchBar = TextInput::create(size.width * 0.58f, "Search tracks...");
    if (m_searchBar) {
        m_searchBar->setPosition({size.width * 0.36f, headerRowY});
        m_searchBar->setCallback([this](const std::string& s) {
            this->onSearchChanged(s);
        });
        m_searchBar->setID("search-input"_spr);
        m_mainLayer->addChild(m_searchBar, 2);
    }

    auto addSpr = ButtonSprite::create("Add", 50, true, "bigFont.fnt", "GJ_button_05.png", 20.f, 0.55f);
    if (addSpr) {
        auto btn = CCMenuItemSpriteExtra::create(addSpr, this,
            menu_selector(MenuMusicLibraryPopup::onAddMusic));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.85f, headerRowY});
        menu->addChild(btn);
        menu->setID("add-menu"_spr);
        m_mainLayer->addChild(menu, 2);
    }
}

void MenuMusicLibraryPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    const float scrollH = size.height - 70.f;
    m_scroll = ScrollLayer::create({size.width - 26.f, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({13.f, 12.f});
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
    std::sort(orderedIds.begin(), orderedIds.end(), [&](const auto& a, const auto& b) {
        auto* ta = lib.findTrack(a);
        auto* tb = lib.findTrack(b);
        if (!ta || !tb) return false;
        return geode::utils::string::toLower(ta->displayName) <
               geode::utils::string::toLower(tb->displayName);
    });

    auto qLower = geode::utils::string::toLower(m_query);

    // ScrollLayer children grow in +Y, so accumulate the total height first
    // and place each card at (totalH - y) to render newest-on-top.
    std::vector<std::string> visibleIds;
    for (const auto& tid : orderedIds) {
        auto* track = lib.findTrack(tid);
        if (!track) continue;
        if (!matchesQuery(*track, qLower)) continue;
        visibleIds.push_back(tid);
    }

    const float cardW = m_scroll->getContentSize().width - 4.f;
    float totalH = static_cast<float>(visibleIds.size()) * (kCardHeight + kCardGap);
    if (totalH > 0.f) totalH -= kCardGap;
    totalH = std::max(totalH, m_scroll->getContentSize().height);

    if (visibleIds.empty()) {
        auto label = CCLabelBMFont::create(
            tracks.empty() ? "Library empty — tap 'Add' to import or download a song."
                           : "No tracks match your search.",
            "chatFont.fnt");
        if (label) {
            label->setScale(0.5f);
            label->setPosition({m_scroll->getContentSize().width / 2.f,
                                m_scroll->getContentSize().height / 2.f});
            label->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(label);
        }
    } else {
        float y = totalH - kCardHeight;
        for (const auto& tid : visibleIds) {
            auto card = buildTrackCard(tid, cardW);
            if (!card) continue;
            card->setPosition({2.f, y});
            m_scroll->m_contentLayer->addChild(card);
            y -= (kCardHeight + kCardGap);
        }
    }

    m_scroll->m_contentLayer->setContentHeight(totalH);
    m_scroll->scrollToTop();
}

cocos2d::CCNode* MenuMusicLibraryPopup::buildTrackCard(const std::string& trackId, float widthOverride) {
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(trackId);
    if (!track) return nullptr;

    auto node = CCNode::create();
    node->setContentSize({widthOverride, kCardHeight});
    node->setAnchorPoint({0, 0});
    node->setID(fmt::format("track-card-{}", trackId).c_str());

    if (auto bg = paimon::SpriteHelper::createRoundedRect(
            widthOverride, kCardHeight, kCardRadius,
            ccc4FFromccc4B(kCardBaseColor))) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        bg->setID("card-bg"_spr);
        node->addChild(bg, 0);
    }

    cocos2d::ccColor3B dominant = kFallbackDominantColor;
    if (!track->coverPath.empty()) {
        auto cc = extractDominant(track->coverPath);
        if (cc.ok) dominant = cc.color;
    }
    dominant = normalizeDominant(dominant);

    {
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(
            widthOverride, kCardHeight, kCardRadius);
        auto clip = CCClippingNode::create();
        if (stencil && clip) {
            clip->setStencil(stencil);
            clip->setAlphaThreshold(0.05f);
            clip->setContentSize({widthOverride, kCardHeight});
            clip->setAnchorPoint({0.f, 0.f});
            clip->setPosition({0.f, 0.f});
            clip->setID("card-gradient-clip"_spr);

            cocos2d::ccColor4F from = {
                dominant.r / 255.f,
                dominant.g / 255.f,
                dominant.b / 255.f,
                150.f / 255.f
            };
            cocos2d::ccColor4F to = {
                dominant.r / 255.f,
                dominant.g / 255.f,
                dominant.b / 255.f,
                0.f
            };

            if (auto grad = buildHorizontalGradient(
                    widthOverride, kCardHeight, from, to, 0.5f)) {
                grad->setAnchorPoint({0.f, 0.f});
                grad->setPosition({0.f, 0.f});
                grad->setID("card-gradient"_spr);
                clip->addChild(grad, 0);
            }

            node->addChild(clip, 1);
        }
    }

    const float thumbSize = kCardHeight - 10.f;
    const float thumbX = 8.f;
    const float thumbY = (kCardHeight - thumbSize) / 2.f;

    auto thumbStencil = paimon::SpriteHelper::createRoundedRectStencil(
        thumbSize, thumbSize, kThumbRadius);
    auto thumbClip = CCClippingNode::create();
    if (thumbStencil && thumbClip) {
        thumbClip->setStencil(thumbStencil);
        thumbClip->setAlphaThreshold(0.05f);
        thumbClip->setContentSize({thumbSize, thumbSize});
        thumbClip->setAnchorPoint({0.f, 0.f});
        thumbClip->setPosition({thumbX, thumbY});
        thumbClip->setID("card-thumb-clip"_spr);
        node->addChild(thumbClip, 3);

        bool thumbFilled = false;
        if (!track->coverPath.empty()) {
            if (auto* tex = CCTextureCache::sharedTextureCache()->addImage(
                    track->coverPath.c_str(), false)) {
                if (auto spr = CCSprite::createWithTexture(tex)) {
                    const CCSize sz = spr->getContentSize();
                    const float s = thumbSize / std::min(sz.width, sz.height);
                    spr->setScale(s);
                    spr->setAnchorPoint({0.5f, 0.5f});
                    spr->setPosition({thumbSize / 2, thumbSize / 2});
                    thumbClip->addChild(spr);
                    thumbFilled = true;
                }
            }
        }

        if (!thumbFilled) {
            if (auto solid = paimon::SpriteHelper::createRoundedRect(
                    thumbSize, thumbSize, 0.f,
                    ccc4FFromccc4B({36, 38, 55, 255}))) {
                solid->setAnchorPoint({0.f, 0.f});
                solid->setPosition({0.f, 0.f});
                thumbClip->addChild(solid, 0);
            }
            if (auto defSpr = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png")) {
                const float s = thumbSize * 0.55f /
                    std::max(defSpr->getContentSize().width, 1.f);
                defSpr->setScale(s);
                defSpr->setAnchorPoint({0.5f, 0.5f});
                defSpr->setPosition({thumbSize / 2, thumbSize / 2});
                defSpr->setColor({160, 160, 180});
                thumbClip->addChild(defSpr, 1);
            }
        }
    }

    const float textX = thumbX + thumbSize + 12.f;
    const float textAreaW = widthOverride - textX - 110.f;

    // Marcar visualmente la cancion que esta sonando ahora mismo: barra
    // verde en el borde izquierdo + titulo en verde.
    const bool isPlayingNow =
        MenuMusicPlayer::get().state().currentTrackId == trackId
        && !trackId.empty();

    if (isPlayingNow) {
        if (auto accent = paimon::SpriteHelper::createRoundedRect(
                4.f, kCardHeight, 2.f, ccc4f(0.42f, 0.94f, 0.52f, 1.f))) {
            accent->setAnchorPoint({0.f, 0.f});
            accent->setPosition({0.f, 0.f});
            accent->setID("card-playing-accent"_spr);
            node->addChild(accent, 2);
        }
    }

    auto nameLbl = CCLabelBMFont::create(track->displayName.c_str(), "bigFont.fnt");
    if (nameLbl) {
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setColor(isPlayingNow
            ? cocos2d::ccColor3B{120, 240, 140}
            : cocos2d::ccColor3B{255, 255, 255});
        nameLbl->limitLabelWidth(textAreaW, 0.52f, 0.28f);
        nameLbl->setPosition({textX, kCardHeight * 0.66f});
        nameLbl->setID("card-title"_spr);
        node->addChild(nameLbl, 4);
    }

    std::string sourceStr = (track->source == TrackSource::Downloaded) ? "downloaded" : "local";
    std::string durStr = track->durationMs > 0
        ? fmt::format("{}:{:02}", track->durationMs / 60000, (track->durationMs / 1000) % 60)
        : "—";
    std::string subStr = fmt::format("{} • {}", sourceStr, durStr);
    if (!track->artist.empty()) {
        subStr = fmt::format("{} • {}", track->artist, subStr);
    }

    auto subLbl = CCLabelBMFont::create(subStr.c_str(), "chatFont.fnt");
    if (subLbl) {
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setColor({200, 205, 225});
        subLbl->limitLabelWidth(textAreaW, 0.42f, 0.25f);
        subLbl->setPosition({textX, kCardHeight * 0.30f});
        subLbl->setID("card-subtitle"_spr);
        node->addChild(subLbl, 4);
    }

    auto menu = CCMenu::create();
    menu->setContentSize({100.f, kCardHeight});
    menu->setAnchorPoint({1.f, 0.5f});
    menu->setPosition({widthOverride - 6.f, kCardHeight / 2.f});
    menu->ignoreAnchorPointForPosition(false);

    auto makeIconBtn = [&](const char* frame, SEL_MenuHandler handler,
                           float scale) -> CCMenuItemSpriteExtra* {
        auto s = CCSprite::createWithSpriteFrameName(frame);
        if (!s) return nullptr;
        s->setScale(scale);
        auto b = CCMenuItemSpriteExtra::create(s, this, handler);
        if (b) b->setUserObject(CCString::create(trackId.c_str()));
        return b;
    };

    if (auto b = makeIconBtn("GJ_playMusicBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onPlayTrack), 0.7f)) {
        menu->addChild(b);
    }
    if (auto b = makeIconBtn("GJ_plusBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onAddToPlaylist), 0.55f)) {
        menu->addChild(b);
    }
    if (auto b = makeIconBtn("GJ_deleteBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onRemoveTrack), 0.55f)) {
        menu->addChild(b);
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

void MenuMusicLibraryPopup::onRemoveTrack(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    std::string id = idStr->getCString();

    geode::createQuickPopup(
        "Remove Track",
        "Remove this track from your library?\n<cy>Downloaded tracks will also have their file deleted.</c>",
        "Cancel", "Remove",
        [id](FLAlertLayer*, bool confirm) {
            if (!confirm) return;
            MenuMusicLibrary::get().removeTrack(id, /*deleteFiles=*/true);
        }
    );
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

void MenuMusicLibraryPopup::onAddMusic(CCObject*) {
    if (auto p = MenuMusicAddPopup::create()) p->show();
}

} // namespace paimon::menumusic
