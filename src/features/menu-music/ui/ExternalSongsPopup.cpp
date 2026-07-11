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

static constexpr float kPopupW = 380.f;
static constexpr float kPopupH = 260.f;

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

    m_searchBar = TextInput::create(size.width * 0.6f, "Search songs", "chatFont.fnt");
    if (m_searchBar) {
        m_searchBar->setCallback([this](const std::string& q) {
            this->onSearchChanged(q);
        });
        m_searchBar->setPosition({size.width / 2.f - 40.f, size.height - 38.f});
        m_searchBar->setID("search-bar"_spr);
        m_mainLayer->addChild(m_searchBar, 5);
    }

    m_summaryLabel = CCLabelBMFont::create(
        fmt::format("{} songs", m_rows.size()).c_str(), "chatFont.fnt");
    if (m_summaryLabel) {
        m_summaryLabel->setScale(0.5f);
        m_summaryLabel->setColor({225, 225, 240});
        m_summaryLabel->setAnchorPoint({1.f, 0.5f});
        m_summaryLabel->setPosition({size.width - 14.f, size.height - 38.f});
        m_summaryLabel->setID("summary-label"_spr);
        m_mainLayer->addChild(m_summaryLabel, 5);
    }

    auto menu = CCMenu::create();
    menu->setContentSize({size.width, 34.f});
    menu->setPosition({size.width / 2.f, 22.f});
    if (auto spr = ButtonSprite::create("Shuffle All", 80, true, "bigFont.fnt",
            "GJ_button_02.png", 24.f, 0.5f)) {
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ExternalSongsPopup::onShuffleAll));
        if (btn) {
            btn->setID("shuffle-all-btn"_spr);
            menu->addChild(btn);
        }
    }
    menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::Center));
    menu->updateLayout();
    m_mainLayer->addChild(menu, 5);
}

void ExternalSongsPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    const CCSize scrollSize{size.width - 24.f, size.height - 90.f};
    m_scroll = ScrollLayer::create(scrollSize);
    if (!m_scroll) return;
    m_scroll->setPosition({12.f, 42.f});
    m_scroll->setID("song-list-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 4);
}

static bool matchesQuery(const std::string& label, const std::string& q) {
    if (q.empty()) return true;
    return geode::utils::string::toLower(label)
        .find(geode::utils::string::toLower(q)) != std::string::npos;
}

void ExternalSongsPopup::rebuildList() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    const float cellW = m_scroll->getContentSize().width;
    const float cellH = 24.f;
    const std::string current = paimon::menuloop::MenuLoopManager::get().getCurrentSong();

    std::vector<Row> filtered;
    for (const auto& r : m_rows) {
        if (matchesQuery(r.label, m_query)) filtered.push_back(r);
    }

    float y = std::max<float>(
        m_scroll->getContentSize().height,
        static_cast<float>(filtered.size()) * cellH);
    m_scroll->m_contentLayer->setContentSize({cellW, y});

    int shown = 0;
    for (const auto& r : filtered) {
        auto row = CCNode::create();
        row->setContentSize({cellW, cellH});
        row->setAnchorPoint({0.f, 0.f});
        row->setPosition({0.f, y - (shown + 1) * cellH});

        auto bg = paimon::SpriteHelper::createRoundedRect(
            cellW - 4.f, cellH - 2.f, 4.f,
            (shown % 2 == 0)
                ? ccc4f(0.08f, 0.08f, 0.12f, 0.65f)
                : ccc4f(0.12f, 0.12f, 0.18f, 0.65f));
        if (bg) {
            bg->setAnchorPoint({0.f, 0.f});
            bg->setPosition({2.f, 1.f});
            row->addChild(bg, 0);
        }

        auto label = CCLabelBMFont::create(r.label.c_str(), "bigFont.fnt");
        if (label) {
            label->limitLabelWidth(cellW - 90.f, 0.5f, 0.25f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({12.f, cellH / 2.f});
            if (r.path == current) {
                label->setColor({120, 255, 140});
            } else {
                label->setColor({235, 235, 245});
            }
            row->addChild(label, 1);
        }

        auto tag = CCLabelBMFont::create(r.source.c_str(), "chatFont.fnt");
        if (tag) {
            tag->setScale(0.38f);
            tag->setAnchorPoint({1.f, 0.5f});
            tag->setPosition({cellW - 44.f, cellH / 2.f});
            tag->setColor({195, 210, 255});
            row->addChild(tag, 1);
        }

        auto menu = CCMenu::create();
        menu->setContentSize({30.f, cellH});
        menu->setAnchorPoint({1.f, 0.5f});
        menu->setPosition({cellW - 10.f, cellH / 2.f});
        menu->ignoreAnchorPointForPosition(false);
        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png")) {
            spr->setScale(0.45f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(ExternalSongsPopup::onPlayTapped));
            if (btn) {
                int tagIdx = static_cast<int>(shown + 1);
                btn->setTag(tagIdx);
                btn->setUserObject(
                    std::string("song-path"), CCString::create(r.path.c_str()));
                menu->addChild(btn);
            }
        }
        row->addChild(menu, 2);

        m_scroll->m_contentLayer->addChild(row);
        shown++;
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
