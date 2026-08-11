#include "ExternalSongsPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../../menu-loop/services/MenuLoopManager.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <system_error>

using namespace geode::prelude;

namespace paimon::menumusic {

static constexpr float kPopupW = 430.f;
static constexpr float kPopupH = 290.f;
static constexpr float kRowHeight = 36.f;
static constexpr float kRowGap = 3.f;

ExternalSongsPopup* ExternalSongsPopup::create() {
    auto ret = new ExternalSongsPopup();
    if (ret && ret->init(kPopupW, kPopupH)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ExternalSongsPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);

    this->setTitle("Song List");

    auto addUnique = [this](const std::string& path, const std::string& label,
                            const std::string& source) {
        for (auto& r : m_rows) if (r.path == path) return;
        m_rows.push_back({path, label, source});
    };

    for (const auto& t : MenuMusicLibrary::get().tracks()) {
        if (t.audioPath.empty()) continue;
        std::string label = t.displayName.empty()
            ? geode::utils::string::pathToString(std::filesystem::path(t.audioPath).stem())
            : t.displayName;
        addUnique(t.audioPath, label, "library");
    }
    for (const auto& s : paimon::menuloop::MenuLoopManager::get().getSongs()) {
        if (s.empty()) continue;
        addUnique(s, geode::utils::string::pathToString(std::filesystem::path(s).stem()), "menu-loop");
    }
    if (auto* mdm = MusicDownloadManager::sharedState()) {
        for (auto* song : CCArrayExt<SongInfoObject*>(mdm->getDownloadedSongs())) {
            if (!song) continue;
            if (mdm->isResourceSong(song->m_songID)) continue;
            std::string songPath = mdm->pathForSong(song->m_songID);
            if (songPath.empty()) continue;
            std::error_code ec;
            if (!std::filesystem::exists(songPath, ec) || ec) continue;
            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(std::filesystem::path(songPath).extension()));
            if (ext != ".mp3" && ext != ".ogg" && ext != ".wav" &&
                ext != ".flac" && ext != ".oga" && ext != ".m4a") continue;
            std::string label = song->m_songName.empty()
                ? geode::utils::string::pathToString(std::filesystem::path(songPath).stem())
                : std::string(song->m_songName);
            addUnique(songPath, label, "downloaded");
        }
    }

    buildHeader();
    buildList();
    rebuildList();

    return true;
}

void ExternalSongsPopup::onExit() {
    if (m_searchBar) {
        m_searchBar->setCallback(nullptr);
    }
    Popup::onExit();
}

void ExternalSongsPopup::buildHeader() {
    auto size = m_mainLayer->getContentSize();
    const float headerY = size.height - 40.f;

    m_searchBar = TextInput::create(245.f, "Search songs", "chatFont.fnt");
    if (m_searchBar) {
        m_searchBar->setCallback([this](const std::string& q) {
            this->onSearchChanged(q);
        });
        m_searchBar->setPosition({140.f, headerY});
        m_searchBar->setID("search-bar"_spr);
        m_mainLayer->addChild(m_searchBar, 5);
    }

    m_summaryLabel = CCLabelBMFont::create(
        fmt::format("{} songs", m_rows.size()).c_str(), "chatFont.fnt");
    if (m_summaryLabel) {
        m_summaryLabel->setScale(0.5f);
        m_summaryLabel->setColor({225, 225, 240});
        m_summaryLabel->setAnchorPoint({1.f, 0.5f});
        m_summaryLabel->setPosition({size.width - 14.f, size.height - 66.f});
        m_summaryLabel->setID("summary-label"_spr);
        m_mainLayer->addChild(m_summaryLabel, 5);
    }

    auto menu = CCMenu::create();
    menu->setContentSize({96.f, 28.f});
    menu->setPosition({350.f, headerY});
    if (auto spr = ButtonSprite::create("Shuffle", 90, true, "bigFont.fnt",
            "GJ_button_02.png", 20.f, 0.45f)) {
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ExternalSongsPopup::onShuffleAll));
        if (btn) {
            btn->setID("shuffle-all-btn"_spr);
            menu->addChild(btn);
        }
    }
    menu->setID("shuffle-menu"_spr);
    m_mainLayer->addChild(menu, 5);
}

void ExternalSongsPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    const float scrollHeight = size.height - 84.f;
    const CCSize scrollSize{size.width - 30.f, scrollHeight};
    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
        panel->setContentSize({size.width - 20.f, scrollHeight + 6.f});
        panel->setPosition({size.width / 2.f, 12.f + scrollHeight / 2.f});
        panel->setOpacity(220);
        panel->setID("song-list-bg"_spr);
        m_mainLayer->addChild(panel, 2);
    }

    m_scroll = ScrollLayer::create(scrollSize);
    if (!m_scroll) return;
    m_scroll->setPosition({15.f, 12.f});
    m_scroll->setID("song-list-scroll"_spr);
    m_scroll->m_contentLayer->setID("song-list-content"_spr);
    m_mainLayer->addChild(m_scroll, 3);
}

static bool matchesQuery(const std::string& label, const std::string& q) {
    if (q.empty()) return true;
    return geode::utils::string::toLower(label)
        .find(geode::utils::string::toLower(q)) != std::string::npos;
}

void ExternalSongsPopup::rebuildList() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    const float cellW = m_scroll->getContentSize().width - 4.f;
    std::string current = paimon::menuloop::MenuLoopManager::get().getCurrentSong();
    if (auto* track = MenuMusicPlayer::get().currentTrack()) {
        current = track->audioPath;
    }

    std::vector<Row> filtered;
    for (const auto& r : m_rows) {
        if (matchesQuery(r.label, m_query)) filtered.push_back(r);
    }

    float contentHeight = static_cast<float>(filtered.size()) * (kRowHeight + kRowGap);
    if (contentHeight > 0.f) contentHeight -= kRowGap;
    contentHeight = std::max(contentHeight, m_scroll->getContentSize().height);
    m_scroll->m_contentLayer->setContentHeight(contentHeight);

    std::size_t shown = 0;
    for (const auto& r : filtered) {
        const bool playing = r.path == current;
        auto row = CCNode::create();
        row->setContentSize({cellW, kRowHeight});
        row->setAnchorPoint({0.f, 0.f});
        row->setPosition({2.f, contentHeight - (shown + 1) * kRowHeight
            - shown * kRowGap});
        row->setID(fmt::format("song-row-{}", shown).c_str());

        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
            bg->setContentSize({cellW, kRowHeight});
            bg->setAnchorPoint({0.f, 0.f});
            bg->setPosition({0.f, 0.f});
            bg->setOpacity(playing ? 245 : (shown % 2 == 0 ? 220 : 195));
            if (playing) bg->setColor({135, 215, 145});
            bg->setID("song-row-bg"_spr);
            row->addChild(bg, 0);
        }

        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(
                "GJ_musicOnBtn_001.png")) {
            icon->setScale(0.34f);
            icon->setPosition({21.f, kRowHeight / 2.f});
            icon->setOpacity(playing ? 255 : 205);
            row->addChild(icon, 1);
        }

        auto label = CCLabelBMFont::create(r.label.c_str(), "bigFont.fnt");
        if (label) {
            label->limitLabelWidth(cellW - 92.f, 0.43f, 0.25f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({42.f, kRowHeight * 0.65f});
            label->setColor(playing ? ccColor3B{120, 245, 140}
                                    : ccColor3B{255, 255, 255});
            row->addChild(label, 1);
        }

        const char* source = r.source == "library" ? "My Songs"
            : r.source == "menu-loop" ? "Menu loop"
            : "Geometry Dash";
        auto tag = CCLabelBMFont::create(source, "chatFont.fnt");
        if (tag) {
            tag->setScale(0.34f);
            tag->setAnchorPoint({0.f, 0.5f});
            tag->setPosition({42.f, kRowHeight * 0.26f});
            tag->setColor({220, 205, 175});
            row->addChild(tag, 1);
        }

        auto menu = CCMenu::create();
        menu->setContentSize({38.f, kRowHeight});
        menu->setAnchorPoint({1.f, 0.5f});
        menu->setPosition({cellW - 5.f, kRowHeight / 2.f});
        menu->ignoreAnchorPointForPosition(false);
        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png")) {
            spr->setScale(0.5f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(ExternalSongsPopup::onPlayTapped));
            if (btn) {
                btn->setPosition({19.f, kRowHeight / 2.f});
                btn->setUserObject(
                    std::string("song-path"), CCString::create(r.path.c_str()));
                btn->setID("play-song-btn"_spr);
                menu->addChild(btn);
            }
        }
        row->addChild(menu, 2);

        m_scroll->m_contentLayer->addChild(row);
        ++shown;
    }

    if (filtered.empty()) {
        auto* label = CCLabelBMFont::create(
            m_rows.empty() ? "No songs found." : "No songs match your search.",
            "chatFont.fnt");
        if (label) {
            label->setScale(0.5f);
            label->setPosition({m_scroll->getContentSize().width / 2.f,
                                m_scroll->getContentSize().height / 2.f});
            label->setColor({205, 205, 220});
            m_scroll->m_contentLayer->addChild(label);
        }
    }

    m_scroll->scrollToTop();

    if (m_summaryLabel) {
        m_summaryLabel->setString(fmt::format("{}/{} songs", shown, m_rows.size()).c_str());
    }
}

void ExternalSongsPopup::onSearchChanged(const std::string& query) {
    m_query = query;
    rebuildList();
}

void ExternalSongsPopup::onPlayTapped(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* payload = static_cast<CCString*>(btn->getUserObject("song-path"));
    if (!payload) return;
    playSongPath(payload->getCString());
    rebuildList();
}

void ExternalSongsPopup::playSongPath(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        Notification::create(
            fmt::format("File not found: {}",
                geode::utils::string::pathToString(std::filesystem::path(path).filename())),
            NotificationIcon::Error)->show();
        return;
    }

    // If the path is part of the MenuMusicLibrary, use the player so it
    // manages history/listeners. Otherwise fall back to the menu-loop
    // override (same plumbing as "Random All").
    for (const auto& t : MenuMusicLibrary::get().tracks()) {
        if (t.audioPath == path) {
            MenuMusicPlayer::get().playSpecific(t.id);
            Notification::create(
                fmt::format("Playing: {}", t.displayName.empty()
                    ? geode::utils::string::pathToString(std::filesystem::path(path).stem())
                    : t.displayName),
                NotificationIcon::Info)->show();
            return;
        }
    }

    MenuMusicLibrary::get().setMode(PlaybackMode::Disabled);
    MenuMusicPlayer::get().forgetCurrentTrack();
    auto& loop = paimon::menuloop::MenuLoopManager::get();
    loop.setOverride(path);
    loop.setCurrentSong(path);
    loop.setCurrentSongDisplayName(
        geode::utils::string::pathToString(std::filesystem::path(path).stem()));

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) fmod->m_backgroundMusicChannel->stop();
    GameManager::sharedState()->playMenuMusic();

    Notification::create(
        fmt::format("Playing: {}",
            geode::utils::string::pathToString(std::filesystem::path(path).stem())),
        NotificationIcon::Info)->show();
}

void ExternalSongsPopup::onShuffleAll(CCObject*) {
    if (m_rows.empty()) {
        Notification::create("No songs available.", NotificationIcon::Warning)->show();
        return;
    }
    static std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::size_t> dist(0, m_rows.size() - 1);
    const auto current = paimon::menuloop::MenuLoopManager::get().getCurrentSong();
    std::string pick = m_rows[dist(rng)].path;
    for (int i = 0; i < 5 && pick == current && m_rows.size() > 1; ++i) {
        pick = m_rows[dist(rng)].path;
    }
    playSongPath(pick);
    rebuildList();
}

} // namespace paimon::menumusic
