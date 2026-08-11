#include "NewgroundsBrowserPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <chrono>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

NewgroundsBrowserPopup* NewgroundsBrowserPopup::create() {
    auto ret = new NewgroundsBrowserPopup();
    if (ret && ret->init(460.f, 300.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool NewgroundsBrowserPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Newgrounds Music");

    buildHeader();
    buildList();
    loadWeekly();
    return true;
}

void NewgroundsBrowserPopup::onExit() {
    ++m_requestGeneration;
    ++m_previewRequestGeneration;
    if (m_searchInput) m_searchInput->setCallback(nullptr);
    stopPreview();
    Popup::onExit();
}

void NewgroundsBrowserPopup::buildHeader() {
    auto size = m_mainLayer->getContentSize();

    m_searchInput = TextInput::create(235.f, "Song name, artist, ID or URL", "chatFont.fnt");
    if (m_searchInput) {
        m_searchInput->setCommonFilter(CommonFilter::Any);
        // Any doesn't include ':' '/' in some Geode builds; URLs need them.
        if (auto* inner = m_searchInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(CommonFilter::Any);
        }
        m_searchInput->setMaxCharCount(300);
        m_searchInput->setPosition({140.f, size.height - 40.f});
        m_searchInput->setID("newgrounds-search"_spr);
        m_mainLayer->addChild(m_searchInput, 4);
    }

    auto controls = CCMenu::create();
    controls->setPosition({0.f, 0.f});
    m_mainLayer->addChild(controls, 4);

    auto searchSprite = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png");
    if (searchSprite) {
        searchSprite->setScale(0.72f);
        auto button = CCMenuItemSpriteExtra::create(
            searchSprite, this, menu_selector(NewgroundsBrowserPopup::onSearch)
        );
        button->setPosition({278.f, size.height - 40.f});
        button->setID("newgrounds-search-btn"_spr);
        controls->addChild(button);
    }

    auto weeklySprite = ButtonSprite::create(
        "Weekly", 74, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f
    );
    if (weeklySprite) {
        auto button = CCMenuItemSpriteExtra::create(
            weeklySprite, this, menu_selector(NewgroundsBrowserPopup::onWeekly)
        );
        button->setPosition({341.f, size.height - 40.f});
        button->setID("newgrounds-weekly-btn"_spr);
        controls->addChild(button);
    }

    auto infoSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
    if (infoSprite) {
        infoSprite->setScale(0.72f);
        auto button = CCMenuItemSpriteExtra::create(
            infoSprite, this, menu_selector(NewgroundsBrowserPopup::onInfo)
        );
        button->setPosition({size.width - 26.f, size.height - 40.f});
        button->setID("newgrounds-info-btn"_spr);
        controls->addChild(button);
    }

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.42f);
        m_statusLabel->setAnchorPoint({0.f, 0.5f});
        m_statusLabel->setPosition({14.f, size.height - 63.f});
        m_statusLabel->setColor({205, 215, 235});
        m_statusLabel->setID("newgrounds-status"_spr);
        m_mainLayer->addChild(m_statusLabel, 4);
    }
}

void NewgroundsBrowserPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    m_scroll = ScrollLayer::create({size.width - 24.f, size.height - 84.f});
    if (!m_scroll) return;
    m_scroll->setPosition({12.f, 10.f});
    m_scroll->setID("newgrounds-results"_spr);
    m_mainLayer->addChild(m_scroll, 3);
}

void NewgroundsBrowserPopup::loadWeekly() {
    m_loading = true;
    m_listTitle = "Weekly Top 5";
    m_emptyMessage.clear();
    auto generation = ++m_requestGeneration;
    if (m_statusLabel) {
        m_statusLabel->setString("Loading the weekly Newgrounds picks...");
        m_statusLabel->setColor({255, 220, 145});
    }
    m_tracks.clear();
    rebuildList();

    auto weakThis = WeakRef<CCNode>(this);
    fetchWeeklyPicks([weakThis, generation](NewgroundsListResult result) {
        auto ref = weakThis.lock();
        auto* self = ref ? typeinfo_cast<NewgroundsBrowserPopup*>(ref.data()) : nullptr;
        if (!self || self->m_requestGeneration != generation) return;
        self->showResult(std::move(result));
    });
}

void NewgroundsBrowserPopup::runSearch() {
    auto query = m_searchInput ? m_searchInput->getString() : std::string();
    if (query.empty()) {
        loadWeekly();
        return;
    }

    m_loading = true;
    m_emptyMessage.clear();
    auto generation = ++m_requestGeneration;
    m_tracks.clear();
    rebuildList();

    auto weakThis = WeakRef<CCNode>(this);
    auto onResult = [weakThis, generation](NewgroundsListResult result) {
        auto ref = weakThis.lock();
        auto* self = ref ? typeinfo_cast<NewgroundsBrowserPopup*>(ref.data()) : nullptr;
        if (!self || self->m_requestGeneration != generation) return;
        self->showResult(std::move(result));
    };

    if (auto songId = parseNewgroundsSongId(query)) {
        m_listTitle = fmt::format("Song #{}", songId);
        if (m_statusLabel) {
            m_statusLabel->setString(
                fmt::format("Looking up song #{} on GD's servers...", songId).c_str());
            m_statusLabel->setColor({255, 220, 145});
        }
        fetchNewgroundsSongInfo(songId,
            [songId, onResult](NewgroundsSongResult result) {
                NewgroundsListResult list;
                list.listTitle = fmt::format("Song #{}", songId);
                if (result.success) {
                    list.success = true;
                    list.tracks.push_back(std::move(result.track));
                } else {
                    list.error = fmt::format(
                        "Song #{} was not found on GD's servers. Only songs "
                        "enabled for GD can be fetched by ID.", songId);
                }
                onResult(std::move(list));
            });
        return;
    }

    m_listTitle = fmt::format("Results for \"{}\"", query);
    if (m_statusLabel) {
        m_statusLabel->setString("Searching Newgrounds songs...");
        m_statusLabel->setColor({255, 220, 145});
    }
    searchNewgroundsSongs(query, onResult);
}

void NewgroundsBrowserPopup::showResult(NewgroundsListResult result) {
    m_loading = false;
    m_listTitle = result.listTitle;
    m_tracks = std::move(result.tracks);
    m_emptyMessage = result.success ? "" : result.error;
    rebuildList();
}

void NewgroundsBrowserPopup::rebuildList() {
    if (!m_scroll) return;
    m_previewButtons.clear();
    m_scroll->m_contentLayer->removeAllChildren();

    constexpr float cellH = 36.f;
    auto cellW = m_scroll->getContentSize().width;
    auto contentH = std::max(
        m_scroll->getContentSize().height,
        static_cast<float>(m_tracks.size()) * cellH
    );
    m_scroll->m_contentLayer->setContentSize({cellW, contentH});

    for (std::size_t i = 0; i < m_tracks.size(); ++i) {
        auto const& track = m_tracks[i];
        auto downloaded = isNewgroundsSongDownloaded(track.songId);
        auto downloading = isNewgroundsSongDownloading(track.songId);

        auto row = CCNode::create();
        row->setContentSize({cellW, cellH});
        row->setPosition({0.f, contentH - (i + 1) * cellH});

        auto background = CCLayerColor::create(
            i % 2 == 0 ? ccc4(25, 24, 34, 190) : ccc4(34, 31, 43, 190),
            cellW - 7.f, cellH - 2.f
        );
        if (background) {
            background->setPosition({2.f, 1.f});
            row->addChild(background);
        }

        auto accentColor = downloaded
            ? ccc4(110, 230, 140, 150)
            : track.gdAvailable ? ccc4(255, 190, 80, 130) : ccc4(130, 130, 145, 110);
        auto accent = CCLayerColor::create(accentColor, 3.f, cellH - 8.f);
        if (accent) {
            accent->setPosition({2.f, 4.f});
            row->addChild(accent, 1);
        }

        auto title = CCLabelBMFont::create(track.title.c_str(), "bigFont.fnt");
        if (title) {
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({11.f, 25.f});
            title->limitLabelWidth(cellW - 160.f, 0.40f, 0.24f);
            row->addChild(title, 2);
        }

        std::string detail;
        if (track.gdAvailable) {
            detail = track.artist.empty() ? "Unknown artist" : track.artist;
            if (track.sizeMb > 0.f) {
                detail += fmt::format("  -  {:.1f} MB", track.sizeMb);
            }
            detail += fmt::format("  -  #{}", track.songId);
        } else {
            detail = fmt::format(
                "#{}  -  Not on GD's servers - can't preview or download",
                track.songId);
        }
        auto subtitle = CCLabelBMFont::create(detail.c_str(), "chatFont.fnt");
        if (subtitle) {
            subtitle->setAnchorPoint({0.f, 0.5f});
            subtitle->setPosition({11.f, 10.f});
            subtitle->setColor(track.gdAvailable
                ? ccColor3B{180, 195, 220}
                : ccColor3B{200, 150, 150});
            subtitle->limitLabelWidth(cellW - 160.f, 0.32f, 0.20f);
            row->addChild(subtitle, 2);
        }

        auto controls = CCMenu::create();
        controls->setPosition({0.f, 0.f});
        row->addChild(controls, 3);

        auto copySprite = ButtonSprite::create(
            "Copy", 40, true, "bigFont.fnt", "GJ_button_05.png", 18.f, 0.32f
        );
        if (copySprite) {
            auto button = CCMenuItemSpriteExtra::create(
                copySprite, this,
                menu_selector(NewgroundsBrowserPopup::onCopyTrackId)
            );
            button->setPosition({cellW - 101.f, cellH / 2.f});
            button->setTag(track.songId);
            button->setID("newgrounds-copy-id-btn"_spr);
            controls->addChild(button);
        }

        if (!track.streamUrl.empty()) {
            auto previewSprite = paimon::SpriteHelper::safeCreateWithFrameName(
                track.songId == m_previewSongId
                    ? "GJ_stopMusicBtn_001.png"
                    : "GJ_playMusicBtn_001.png"
            );
            if (previewSprite) {
                previewSprite->setScale(0.52f);
                auto button = CCMenuItemSpriteExtra::create(
                    previewSprite, this,
                    menu_selector(NewgroundsBrowserPopup::onPreviewTrack)
                );
                button->setPosition({cellW - 56.f, cellH / 2.f});
                button->setTag(track.songId);
                button->setID("newgrounds-preview-btn"_spr);
                controls->addChild(button);
                m_previewButtons.push_back(button);
            }
        }

        if (downloaded) {
            auto check = paimon::SpriteHelper::safeCreateWithFrameName(
                "GJ_completesIcon_001.png");
            if (check) {
                check->setScale(0.6f);
                check->setPosition({cellW - 22.f, cellH / 2.f});
                row->addChild(check, 3);
            }
        } else if (downloading) {
            auto spinner = CCSprite::create("loadingCircle.png");
            if (spinner) {
                spinner->setScale(0.35f);
                spinner->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
                spinner->setPosition({cellW - 22.f, cellH / 2.f});
                spinner->runAction(CCRepeatForever::create(
                    CCRotateBy::create(1.f, 360.f)));
                row->addChild(spinner, 3);
            }
        } else if (track.gdAvailable) {
            auto downloadSprite = paimon::SpriteHelper::safeCreateWithFrameName(
                "GJ_downloadBtn_001.png");
            if (downloadSprite) {
                downloadSprite->setScale(0.62f);
                auto button = CCMenuItemSpriteExtra::create(
                    downloadSprite, this,
                    menu_selector(NewgroundsBrowserPopup::onDownloadTrack)
                );
                button->setPosition({cellW - 22.f, cellH / 2.f});
                button->setTag(track.songId);
                button->setID("newgrounds-download-btn"_spr);
                controls->addChild(button);
            }
        }

        m_scroll->m_contentLayer->addChild(row);
    }

    if (m_tracks.empty() && !m_loading) {
        auto message = !m_emptyMessage.empty()
            ? m_emptyMessage
            : std::string("No songs found. Paste a Newgrounds song ID or "
                          "/audio/listen/ URL to fetch any GD-enabled song.");
        auto empty = CCLabelBMFont::create(message.c_str(), "chatFont.fnt");
        if (empty) {
            empty->setScale(0.5f);
            empty->setPosition({cellW / 2.f, contentH / 2.f});
            empty->setColor({205, 205, 220});
            empty->setAlignment(kCCTextAlignmentCenter);
            auto maxW = cellW - 40.f;
            if (empty->getContentSize().width * empty->getScale() > maxW) {
                empty->setScale(maxW / empty->getContentSize().width);
            }
            m_scroll->m_contentLayer->addChild(empty);
        }
    }

    refreshStatus();
    m_scroll->scrollToTop();
}

void NewgroundsBrowserPopup::refreshStatus() {
    if (!m_statusLabel) return;

    if (m_previewSongId != 0 && m_previewSound && !m_previewChannel) {
        m_statusLabel->setString("Buffering preview...");
        m_statusLabel->setColor({255, 220, 145});
        return;
    }
    if (m_previewChannel) {
        auto title = std::to_string(m_previewSongId);
        if (auto* track = trackById(m_previewSongId)) title = track->title;
        m_statusLabel->setString(fmt::format("Streaming: {}", title).c_str());
        m_statusLabel->setColor({145, 245, 155});
        return;
    }
    if (m_loading) return;

    if (m_tracks.empty()) {
        if (!m_emptyMessage.empty()) {
            m_statusLabel->setString(m_emptyMessage.c_str());
            m_statusLabel->setColor({255, 170, 145});
        } else {
            m_statusLabel->setString(m_listTitle.c_str());
            m_statusLabel->setColor({205, 215, 235});
        }
        return;
    }

    auto usable = std::count_if(m_tracks.begin(), m_tracks.end(),
        [](NewgroundsTrack const& t) { return t.gdAvailable; });
    m_statusLabel->setString(fmt::format(
        "{}  -  {} songs ({} downloadable)",
        m_listTitle, m_tracks.size(), usable).c_str());
    m_statusLabel->setColor({175, 230, 185});
}

NewgroundsTrack const* NewgroundsBrowserPopup::trackById(int songId) const {
    auto found = std::find_if(m_tracks.begin(), m_tracks.end(),
        [songId](NewgroundsTrack const& t) { return t.songId == songId; });
    return found != m_tracks.end() ? &*found : nullptr;
}

void NewgroundsBrowserPopup::updatePreviewButtons() {
    for (auto* button : m_previewButtons) {
        if (!button) continue;
        auto active = button->getTag() == m_previewSongId;
        auto sprite = paimon::SpriteHelper::safeCreateWithFrameName(
            active ? "GJ_stopMusicBtn_001.png" : "GJ_playMusicBtn_001.png"
        );
        if (!sprite) continue;
        sprite->setScale(0.52f);
        button->setNormalImage(sprite);
    }
}

void NewgroundsBrowserPopup::startPreviewStream(std::string const& url) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        Notification::create("FMOD is not available.", NotificationIcon::Error)->show();
        stopPreview();
        return;
    }

    auto result = engine->m_system->createSound(
        url.c_str(), FMOD_CREATESTREAM | FMOD_NONBLOCKING | FMOD_2D,
        nullptr, &m_previewSound
    );
    if (result != FMOD_OK || !m_previewSound) {
        log::warn("[Newgrounds] createSound failed ({})", static_cast<int>(result));
        Notification::create("The stream could not be opened.", NotificationIcon::Error)->show();
        stopPreview();
        return;
    }

    m_previewOpenStarted = std::chrono::steady_clock::now();
    this->unschedule(schedule_selector(NewgroundsBrowserPopup::pollPreview));
    this->schedule(schedule_selector(NewgroundsBrowserPopup::pollPreview), 0.05f);
    refreshStatus();
}

void NewgroundsBrowserPopup::pollPreview(float) {
    if (m_previewChannel) {
        bool playing = false;
        if (m_previewChannel->isPlaying(&playing) != FMOD_OK || !playing) {
            stopPreview();
        }
        return;
    }
    if (!m_previewSound) return;

    FMOD_OPENSTATE openState = FMOD_OPENSTATE_CONNECTING;
    unsigned int buffered = 0;
    bool starving = false;
    bool diskBusy = false;
    auto stateResult = m_previewSound->getOpenState(
        &openState, &buffered, &starving, &diskBusy
    );
    auto elapsed = std::chrono::steady_clock::now() - m_previewOpenStarted;
    if (stateResult != FMOD_OK || openState == FMOD_OPENSTATE_ERROR ||
        elapsed > std::chrono::seconds(15)) {
        log::warn(
            "[Newgrounds] stream open failed (result={}, state={}, buffered={})",
            static_cast<int>(stateResult), static_cast<int>(openState), buffered
        );
        Notification::create("Newgrounds preview timed out.", NotificationIcon::Error)->show();
        stopPreview();
        return;
    }
    if (openState != FMOD_OPENSTATE_READY) return;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        stopPreview();
        return;
    }

    FMOD::Channel* channel = nullptr;
    auto playResult = engine->m_system->playSound(m_previewSound, nullptr, true, &channel);
    if (playResult == FMOD_ERR_NOTREADY) return;
    if (playResult != FMOD_OK || !channel) {
        log::warn("[Newgrounds] playSound failed ({})", static_cast<int>(playResult));
        Notification::create("The preview could not be played.", NotificationIcon::Error)->show();
        stopPreview();
        return;
    }

    if (engine->m_backgroundMusicChannel) {
        m_backgroundWasPaused = false;
        engine->m_backgroundMusicChannel->getPaused(&m_backgroundWasPaused);
        m_backgroundPauseCaptured = true;
        engine->m_backgroundMusicChannel->setPaused(true);
    }

    m_previewChannel = channel;
    m_previewChannel->setVolume(engine->m_musicVolume);
    m_previewChannel->setPriority(0);
    if (m_previewChannel->setPaused(false) != FMOD_OK) {
        Notification::create("The preview could not be started.", NotificationIcon::Error)->show();
        stopPreview();
        return;
    }

    this->unschedule(schedule_selector(NewgroundsBrowserPopup::pollPreview));
    this->schedule(schedule_selector(NewgroundsBrowserPopup::pollPreview), 0.25f);
    refreshStatus();
}

void NewgroundsBrowserPopup::stopPreview() {
    this->unschedule(schedule_selector(NewgroundsBrowserPopup::pollPreview));
    if (m_previewChannel) {
        m_previewChannel->stop();
        m_previewChannel = nullptr;
    }
    if (m_previewSound) {
        m_previewSound->release();
        m_previewSound = nullptr;
    }

    if (m_backgroundPauseCaptured) {
        auto* engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel && !m_backgroundWasPaused) {
            engine->m_backgroundMusicChannel->setPaused(false);
        }
    }

    m_backgroundPauseCaptured = false;
    m_backgroundWasPaused = false;
    m_previewSongId = 0;
    updatePreviewButtons();
    refreshStatus();
}

void NewgroundsBrowserPopup::onSearch(CCObject*) {
    if (m_loading) {
        Notification::create("Still loading, one moment...", NotificationIcon::Info)->show();
        return;
    }
    runSearch();
}

void NewgroundsBrowserPopup::onWeekly(CCObject*) {
    if (m_loading) {
        Notification::create("Still loading, one moment...", NotificationIcon::Info)->show();
        return;
    }
    if (m_searchInput) m_searchInput->setString("");
    loadWeekly();
}

void NewgroundsBrowserPopup::onInfo(CCObject*) {
    PopupManager::get().alert(
        "Newgrounds Music",
        "Search works through <cy>GD's own servers</c>, so every song that is "
        "enabled for Geometry Dash can be found.\n\n"
        "<cg>By name/artist:</c> searches Newgrounds listen pages.\n"
        "<cg>By ID or URL:</c> paste a song ID (e.g. <cy>467339</c>) or a "
        "newgrounds.com/audio/listen link.\n\n"
        "Downloads are saved in the GD save folder as <cy>[songID].mp3</c> - "
        "exactly like GD-accepted custom songs - and are added to your "
        "menu music library automatically."
    ).showInstant();
}

void NewgroundsBrowserPopup::onCopyTrackId(CCObject* sender) {
    auto* button = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    auto songId = button->getTag();
    if (songId <= 0) return;

    if (geode::utils::clipboard::write(std::to_string(songId))) {
        Notification::create(
            fmt::format("Song ID {} copied!", songId),
            NotificationIcon::Success
        )->show();
    } else {
        Notification::create(
            "Could not copy the song ID.", NotificationIcon::Error
        )->show();
    }
}

void NewgroundsBrowserPopup::onPreviewTrack(CCObject* sender) {
    auto* button = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    auto songId = button->getTag();
    if (songId <= 0) return;

    ++m_previewRequestGeneration;
    if (songId == m_previewSongId) {
        stopPreview();
        return;
    }

    auto* track = trackById(songId);
    if (!track || track->streamUrl.empty()) return;

    stopPreview();
    m_previewSongId = songId;
    updatePreviewButtons();
    startPreviewStream(track->streamUrl);
}

void NewgroundsBrowserPopup::onDownloadTrack(CCObject* sender) {
    auto* button = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    auto songId = button->getTag();
    if (songId <= 0) return;

    if (isNewgroundsSongDownloading(songId)) {
        Notification::create("This song is already downloading.", NotificationIcon::Info)->show();
        return;
    }

    std::string title = std::to_string(songId);
    if (auto* track = trackById(songId)) title = track->title;

    auto weakThis = WeakRef<CCNode>(this);
    downloadNewgroundsSong(songId,
        [weakThis, songId, title](NewgroundsDownloadResult result) {
            if (result.success) {
                Notification::create(
                    fmt::format("{} saved as {}.mp3!", title, songId),
                    NotificationIcon::Success)->show();
            } else {
                Notification::create(result.error, NotificationIcon::Error, 4.f)->show();
            }

            auto ref = weakThis.lock();
            auto* self = ref ? typeinfo_cast<NewgroundsBrowserPopup*>(ref.data()) : nullptr;
            if (self) self->rebuildList();
        });

    rebuildList();
    if (m_statusLabel) {
        m_statusLabel->setString(fmt::format("Downloading {}...", title).c_str());
        m_statusLabel->setColor({255, 220, 145});
    }
}

} // namespace paimon::menumusic
