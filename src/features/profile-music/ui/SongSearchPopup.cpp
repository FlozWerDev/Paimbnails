#include "SongSearchPopup.hpp"

#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace {
    inline std::string tr(std::string const& key) {
        return Localization::get().getString(key);
    }

    std::string asciiLower(std::string const& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            out.push_back(c);
        }
        return out;
    }
}


SongSearchRowWidget* SongSearchRowWidget::create(SongSearchPopup* parent) {
    auto ret = new SongSearchRowWidget();
    if (ret && ret->init(parent)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SongSearchRowWidget::init(SongSearchPopup* parent) {
    if (!CCLayer::init()) return false;

    m_parent = parent;
    this->setContentSize({320.f, 36.f});
    this->setAnchorPoint({0.5f, 0.5f});
    this->ignoreAnchorPointForPosition(false);
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);

    if (auto* bg = paimon::SpriteHelper::createDarkPanel(320.f, 36.f, 130, 5.f)) {
        bg->setPosition({0.f, 0.f});
        this->addChild(bg, -1);
    }

    m_nameLabel = CCLabelBMFont::create("?", "bigFont.fnt");
    m_nameLabel->setAnchorPoint({0.f, 0.5f});
    m_nameLabel->setScale(0.42f);
    m_nameLabel->setPosition({10.f, 24.f});
    this->addChild(m_nameLabel);

    m_artistLabel = CCLabelBMFont::create("?", "goldFont.fnt");
    m_artistLabel->setAnchorPoint({0.f, 0.5f});
    m_artistLabel->setScale(0.32f);
    m_artistLabel->setColor({200, 200, 220});
    m_artistLabel->setPosition({10.f, 11.f});
    this->addChild(m_artistLabel);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu);

    auto playSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png");
    if (playSpr) {
        playSpr->setScale(0.55f);
    } else {
        playSpr = CCSprite::create();
    }
    m_playButton = CCMenuItemSpriteExtra::create(
        playSpr, this, menu_selector(SongSearchRowWidget::onPlayClicked));
    m_playButton->setPosition({320.f - 22.f, 18.f});
    menu->addChild(m_playButton);

    auto selSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_selectSongBtn_001.png");
    if (selSpr) {
        selSpr->setScale(0.55f);
        m_selectButton = CCMenuItemSpriteExtra::create(
            selSpr, this, menu_selector(SongSearchRowWidget::onSelectClicked));
        m_selectButton->setPosition({320.f - 56.f, 18.f});
        menu->addChild(m_selectButton);
    }

    return true;
}

void SongSearchRowWidget::setSong(SongInfoObject* song) {
    m_song = song;
    if (!song) {
        m_nameLabel->setString("?");
        m_artistLabel->setString("?");
        return;
    }

    m_nameLabel->setString(song->m_songName.c_str());
    m_artistLabel->setString(song->m_artistName.c_str());

    constexpr float maxNameWidth = 220.f;
    constexpr float baseScale    = 0.42f;
    auto nameSize = m_nameLabel->getContentSize();
    if (nameSize.width * baseScale > maxNameWidth) {
        m_nameLabel->setScale(maxNameWidth / nameSize.width);
    } else {
        m_nameLabel->setScale(baseScale);
    }

    auto artSize = m_artistLabel->getContentSize();
    constexpr float maxArtistWidth = 220.f;
    constexpr float baseArtistScale = 0.32f;
    if (artSize.width * baseArtistScale > maxArtistWidth) {
        m_artistLabel->setScale(maxArtistWidth / artSize.width);
    } else {
        m_artistLabel->setScale(baseArtistScale);
    }

    updatePlayButton();
}

void SongSearchRowWidget::updatePlayButton() {
    if (!m_song || !m_playButton) return;

    auto* fmod = FMODAudioEngine::sharedEngine();
    bool playing = false;
    if (fmod && m_parent) {
        playing = (m_parent->getCurrentPreviewSongID() == m_song->m_songID) &&
                  fmod->isMusicPlaying(0);
    }

    const char* frame = playing ? "GJ_stopMusicBtn_001.png" : "GJ_playMusicBtn_001.png";
    auto newSpr = paimon::SpriteHelper::safeCreateWithFrameName(frame);
    if (newSpr) {
        newSpr->setScale(0.55f);
        m_playButton->setNormalImage(newSpr);
    }
}

void SongSearchRowWidget::onPlayClicked(CCObject*) {
    if (!m_song || !m_parent) return;
    m_parent->onSongPreview(m_song);
}

void SongSearchRowWidget::onSelectClicked(CCObject*) {
    if (!m_song || !m_parent) return;
    m_parent->onSongSelected(m_song->m_songID);
}

bool SongSearchRowWidget::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_song) return false;
    auto local = this->convertTouchToNodeSpace(touch);
    auto sz = this->getContentSize();
    m_touchInside = (local.x >= 0 && local.x <= sz.width &&
                     local.y >= 0 && local.y <= sz.height);
    return m_touchInside;
}

void SongSearchRowWidget::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_touchInside && m_song && m_parent) {
        auto local = this->convertTouchToNodeSpace(touch);
        auto sz = this->getContentSize();
        if (local.x >= 0 && local.x <= sz.width &&
            local.y >= 0 && local.y <= sz.height) {
            m_parent->onSongSelected(m_song->m_songID);
        }
    }
    m_touchInside = false;
}


SongSearchPopup* SongSearchPopup::create(SelectCallback callback) {
    auto ret = new SongSearchPopup();
    if (ret && ret->init(std::move(callback))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SongSearchPopup::init(SelectCallback callback) {
    if (!Popup::init(380.f, 300.f)) return false;

    m_callback = std::move(callback);
    this->setTitle(tr("music.search.title").c_str());
    this->setID("SongSearchPopup"_spr);

    auto winSize = m_mainLayer->getContentSize();

    m_searchInput = TextInput::create(280.f, tr("music.search.placeholder").c_str());
    m_searchInput->setPosition({winSize.width / 2.f, winSize.height - 38.f});
    m_searchInput->setMaxCharCount(60);
    m_searchInput->setID("song-search-input"_spr);
    m_searchInput->setDelegate(this);
    m_mainLayer->addChild(m_searchInput, 11);

    m_resultsLabel = CCLabelBMFont::create("0", "goldFont.fnt");
    m_resultsLabel->setAnchorPoint({1.f, 0.5f});
    m_resultsLabel->setScale(0.40f);
    m_resultsLabel->setPosition({winSize.width - 12.f, winSize.height - 14.f});
    m_resultsLabel->setColor({200, 200, 220});
    m_mainLayer->addChild(m_resultsLabel);

    constexpr float listW = 340.f;
    constexpr float listH = SongSearchPopup::kVisibleRows *
                            (SongSearchPopup::kRowHeight + SongSearchPopup::kRowSpacing);
    constexpr float listX = (380.f - listW) / 2.f;
    constexpr float listY = 30.f;

    if (auto* bg = paimon::SpriteHelper::createDarkPanel(listW, listH, 145, 6.f)) {
        bg->setPosition({listX, listY});
        m_mainLayer->addChild(bg, 0);
    }

    auto stencil = paimon::SpriteHelper::createRectStencil(listW, listH);
    auto clip = CCClippingNode::create(stencil);
    clip->setAlphaThreshold(0.05f);
    clip->setPosition({listX, listY});
    clip->setContentSize({listW, listH});
    m_mainLayer->addChild(clip, 1);

    m_scrollContent = CCNode::create();
    m_scrollContent->setAnchorPoint({0.f, 0.f});
    m_scrollContent->setPosition({listW / 2.f, listH});
    m_scrollContent->setContentSize({listW, listH});
    clip->addChild(m_scrollContent);

    m_emptyLabel = CCLabelBMFont::create(tr("music.search.empty").c_str(), "bigFont.fnt");
    m_emptyLabel->setScale(0.45f);
    m_emptyLabel->setOpacity(140);
    m_emptyLabel->setPosition({listX + listW / 2.f, listY + listH / 2.f});
    m_emptyLabel->setVisible(false);
    m_mainLayer->addChild(m_emptyLabel, 2);

    auto hint = CCLabelBMFont::create(tr("music.search.hint").c_str(), "chatFont.fnt");
    hint->setScale(0.55f);
    hint->setOpacity(150);
    hint->setPosition({winSize.width / 2.f, 16.f});
    m_mainLayer->addChild(hint, 0);

    rebuildScrollList();

    paimon::markDynamicPopup(this);

    this->setTouchEnabled(true);
    this->setMouseEnabled(true);

    return true;
}

void SongSearchPopup::rebuildScrollList() {
    m_allDownloaded.clear();

    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm) return;

    auto* downloaded = mdm->getDownloadedSongs();
    if (!downloaded) return;

    for (auto* song : CCArrayExt<SongInfoObject*>(downloaded)) {
        if (!song) continue;
        std::string indexable = asciiLower(std::string(song->m_songName) + " " + std::string(song->m_artistName));
        m_allDownloaded.emplace_back(std::move(indexable), song);
    }

    runSearch();
}

void SongSearchPopup::runSearch() {
    m_filtered.clear();

    std::string query;
    if (m_searchInput) {
        query = asciiLower(m_searchInput->getString());
    }

    if (query.empty()) {
        m_filtered.reserve(m_allDownloaded.size());
        for (auto const& [_, song] : m_allDownloaded) {
            m_filtered.push_back(song);
        }
        std::sort(m_filtered.begin(), m_filtered.end(), [](auto* a, auto* b) {
            return asciiLower(a->m_songName) < asciiLower(b->m_songName);
        });
    } else {
        std::vector<std::pair<int, SongInfoObject*>> scored;
        scored.reserve(m_allDownloaded.size());
        for (auto const& [haystack, song] : m_allDownloaded) {
            int score = 0;
            if (fuzzyMatch(query, haystack, score)) {
                scored.emplace_back(score, song);
            }
        }
        std::sort(scored.begin(), scored.end(),
                  [](auto const& a, auto const& b) { return a.first > b.first; });
        m_filtered.reserve(scored.size());
        for (auto const& [_, song] : scored) {
            m_filtered.push_back(song);
        }
    }

    m_yScroll = 0.f;

    if (m_resultsLabel) {
        std::string txt = fmt::format(fmt::runtime(tr("music.search.results_fmt")), m_filtered.size());
        m_resultsLabel->setString(txt.c_str());
    }
    if (m_emptyLabel) {
        m_emptyLabel->setVisible(m_filtered.empty());
    }

    updateScrollLayout(true);
}

void SongSearchPopup::updateScrollLayout(bool forceRefresh) {
    if (!m_scrollContent) return;

    if (m_rowPool.empty()) {
        for (int i = 0; i < kVisibleRows + 1; ++i) {
            auto* row = SongSearchRowWidget::create(this);
            if (!row) continue;
            row->setVisible(false);
            m_scrollContent->addChild(row);
            m_rowPool.push_back(row);
        }
    }

    const float pitch = kRowHeight + kRowSpacing;

    int totalRows = static_cast<int>(m_filtered.size());

    const float maxScroll = std::max(0.f, pitch * (totalRows - kVisibleRows + 1));
    if (maxScroll <= 0.f) {
        m_yScroll = 0.f;
    } else {
        m_yScroll = std::clamp(m_yScroll, 0.f, maxScroll);
    }

    int startRow = static_cast<int>(m_yScroll / pitch);
    if (startRow < 0) startRow = 0;
    if (startRow > std::max(0, totalRows - 1)) startRow = std::max(0, totalRows - 1);

    for (int i = 0; i < static_cast<int>(m_rowPool.size()); ++i) {
        auto* row = m_rowPool[i];
        int rowIndex = startRow + i;
        if (rowIndex >= totalRows) {
            row->setVisible(false);
            continue;
        }
        row->setVisible(true);
        bool needsUpdate = forceRefresh ||
                           (row->getSong() != m_filtered[rowIndex]);
        if (needsUpdate) {
            row->setSong(m_filtered[rowIndex]);
        }
        const float fractionalOffset = std::fmod(m_yScroll, pitch);
        const float yPos = - (i * pitch) + fractionalOffset - kRowHeight * 0.5f;
        row->setPosition({0.f, yPos});
    }

    m_prevYScroll = m_yScroll;
}

bool SongSearchPopup::fuzzyMatch(std::string const& query, std::string const& target, int& outScore) {
    outScore = 0;
    if (query.empty()) {
        outScore = 1;
        return true;
    }
    if (target.empty()) return false;

    auto pos = target.find(query);
    if (pos != std::string::npos) {
        outScore = 10000;
        if (pos == 0) {
            outScore += 500;
        } else {
            outScore -= static_cast<int>(pos);
        }
        return true;
    }

    size_t qIdx = 0;
    int contiguous = 0;
    int matchedAtStart = 0;
    for (size_t tIdx = 0; tIdx < target.size() && qIdx < query.size(); ++tIdx) {
        if (target[tIdx] == query[qIdx]) {
            outScore += 5 + contiguous * 3;
            if (tIdx < 3 && qIdx == 0) matchedAtStart = 50;
            qIdx++;
            contiguous++;
        } else {
            contiguous = 0;
        }
    }

    if (qIdx < query.size()) {
        outScore = 0;
        return false;
    }

    outScore += matchedAtStart;
    return true;
}


void SongSearchPopup::textChanged(CCTextInputNode*) {
    runSearch();
}

void SongSearchPopup::onClose(cocos2d::CCObject* sender) {
    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->stop();
    }
    Popup::onClose(sender);
}

bool SongSearchPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    return Popup::ccTouchBegan(touch, event);
}

void SongSearchPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    Popup::ccTouchMoved(touch, event);

    auto delta = touch->getDelta();
    if (std::abs(delta.y) > 0.f) {
        m_yScroll -= delta.y;
        updateScrollLayout(false);
    }
}

void SongSearchPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    Popup::ccTouchEnded(touch, event);
}

void SongSearchPopup::scrollWheel(float vertical, float ) {
    m_yScroll -= vertical * 1.5f;
    updateScrollLayout(false);
}


void SongSearchPopup::onSongSelected(int songID) {
    if (m_callback) m_callback(songID);
    this->onClose(nullptr);
}

void SongSearchPopup::onSongPreview(SongInfoObject* song) {
    if (!song) return;

    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm) return;

    auto path = mdm->pathForSong(song->m_songID);
    auto* fmod = FMODAudioEngine::sharedEngine();
    if (!fmod) return;

    bool sameSongPlaying = (m_currentPreviewSongID == song->m_songID) &&
                           fmod->isMusicPlaying(0);

    if (fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->stop();
    }

    if (sameSongPlaying) {
        m_currentPreviewSongID = 0;
    } else {
        fmod->playMusic(path, false, 0.f, 0);
        m_currentPreviewSongID = song->m_songID;
    }

    refreshAllRowsPlayState();
}

void SongSearchPopup::refreshAllRowsPlayState() {
    for (auto* row : m_rowPool) {
        if (row && row->isVisible()) {
            row->updatePlayButton();
        }
    }
}
