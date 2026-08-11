#include "ProfileMusicPopup.hpp"
#include "SongSearchPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../framework/PermissionPolicy.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <optional>
#include <system_error>

using namespace geode::prelude;

namespace {
    inline std::string tr(std::string const& key) {
        return Localization::get().getString(key);
    }

// Format milliseconds as m:ss.
    std::string formatMsClock(int ms) {
        if (ms < 0) ms = 0;
        int totalSec = ms / 1000;
        return fmt::format("{}:{:02d}", totalSec / 60, totalSec % 60);
    }

// Format milliseconds as seconds for the editable inputs.
    std::string formatSeconds(int ms) {
        if (ms < 0) ms = 0;
        if (ms % 1000 == 0) {
            return std::to_string(ms / 1000);
        }
        std::string out = fmt::format("{:.2f}", ms / 1000.0);
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
        return out;
    }

// Parse m:ss, :ss, m:, or decimal seconds into milliseconds.
    std::optional<int> parseClockToMs(std::string const& raw) {
        if (raw.empty()) return std::nullopt;

        auto colon = raw.find(':');
        if (colon == std::string::npos) {
            auto parsed = geode::utils::numFromString<double>(raw);
            if (!parsed.isOk()) return std::nullopt;
            double seconds = parsed.unwrap();
            if (seconds < 0) return std::nullopt;
            return static_cast<int>(std::lround(seconds * 1000.0));
        }

        std::string minPart = raw.substr(0, colon);
        std::string secPart = raw.substr(colon + 1);

        int minutes = 0;
        if (!minPart.empty()) {
            auto parsed = geode::utils::numFromString<int>(minPart);
            if (!parsed.isOk()) return std::nullopt;
            minutes = parsed.unwrap();
        }

        double seconds = 0.0;
        if (!secPart.empty()) {
            auto parsed = geode::utils::numFromString<double>(secPart);
            if (!parsed.isOk()) return std::nullopt;
            seconds = parsed.unwrap();
        }

        if (minutes < 0 || seconds < 0) return std::nullopt;
        return static_cast<int>(std::lround((minutes * 60 + seconds) * 1000.0));
    }

    std::optional<std::string> stageCustomAudioFile(std::filesystem::path const& src) {
        std::error_code ec;

        auto destDir = Mod::get()->getSaveDir() / "profile-music-import";
        std::filesystem::create_directories(destDir, ec);

// Best-effort cleanup; FMOD-locked preview files are left for later.
        std::error_code iterEc;
        if (std::filesystem::is_directory(destDir, iterEc)) {
            for (auto const& entry : std::filesystem::directory_iterator(destDir, iterEc)) {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
            }
        }

        std::string ext = geode::utils::string::pathToString(src.extension());
        if (ext.empty() || ext.size() > 8) {
            ext = ".mp3";
        }

        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dest = destDir / fmt::format("import_{}{}", stamp, ext);

        std::filesystem::copy_file(
            src, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            log::error("[ProfileMusic] Failed to stage custom audio file '{}': {}",
                geode::utils::string::pathToString(src), ec.message());
            return std::nullopt;
        }

        return geode::utils::string::pathToString(dest);
    }
}

ProfileMusicPopup* ProfileMusicPopup::create(int accountID) {
    auto ret = new ProfileMusicPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void ProfileMusicPopup::addSeparatorLine(float y) {
    auto sep = PaimonDrawNode::create();
    float sepWidth = m_mainLayer->getContentSize().width - 30.f;
    cocos2d::ccColor4F sepColor = {1.f, 1.f, 1.f, 0.09f};
    sep->drawSegment(ccp(0, 0), ccp(sepWidth, 0), 0.5f, sepColor);
    sep->setPosition({15.f, y});
    m_mainLayer->addChild(sep);
}

cocos2d::CCNode* ProfileMusicPopup::createHandleVisual(float height, cocos2d::ccColor3B color, bool isStart) {
    auto container = CCNode::create();
    container->setContentSize({22.f, height});

    auto draw = PaimonDrawNode::create();

    cocos2d::ccColor4F c     = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.00f };
    cocos2d::ccColor4F cSoft = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.28f };

    draw->drawSegment(ccp(0, 0), ccp(0, height), 5.0f, cSoft);
    draw->drawSegment(ccp(0, 0), ccp(0, height), 2.0f, c);

    float capDir = isStart ? 5.f : -5.f;
    draw->drawSegment(ccp(0, height), ccp(capDir, height), 2.0f, c);
    draw->drawSegment(ccp(0, 0.f),    ccp(capDir, 0.f),    2.0f, c);

    float knobH = 18.f;
    float knobY = height * 0.5f;
    cocos2d::ccColor4F knobFill = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.00f };
    cocos2d::ccColor4F knobEdge = { 1.f, 1.f, 1.f, 0.85f };
    cocos2d::CCPoint knob[4] = {
        ccp(-3.f, knobY - knobH / 2.f),
        ccp( 3.f, knobY - knobH / 2.f),
        ccp( 3.f, knobY + knobH / 2.f),
        ccp(-3.f, knobY + knobH / 2.f),
    };
    draw->drawPolygon(knob, 4, knobFill, 0.8f, knobEdge);

    cocos2d::ccColor4F grip = { 1.f, 1.f, 1.f, 0.75f };
    draw->drawSegment(ccp(-1.f, knobY - 4.f), ccp(-1.f, knobY + 4.f), 0.8f, grip);
    draw->drawSegment(ccp( 1.f, knobY - 4.f), ccp( 1.f, knobY + 4.f), 0.8f, grip);

    container->addChild(draw);
    return container;
}

bool ProfileMusicPopup::init(int accountID) {
    if (!Popup::init(410.f, 300.f)) return false;

    m_accountID = accountID;

    this->setTitle(tr("music.popup_title").c_str());
    if (m_title) {
        m_title->setScale(m_title->getScale() * 0.82f);
    }

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("main-menu"_spr);
    m_mainMenu->setPosition(CCPointZero);
    m_mainLayer->addChild(m_mainMenu);


    createSongIdInput();
    createWaveformDisplay();
    createTimeEditor();
    createControlButtons();

    loadExistingConfig();

    paimon::markDynamicPopup(this);
    return true;
}

void ProfileMusicPopup::createSongIdInput() {
    auto winSize = m_mainLayer->getContentSize();

    const float rowY        = winSize.height - 50.f;
    const bool  hasCustomBtn = ProfileMusicManager::get().canUploadCustomMusic();

    auto inputRow = CCMenu::create();
    inputRow->setID("input-row"_spr);
    inputRow->setContentSize({winSize.width - 24.f, 32.f});
    inputRow->ignoreAnchorPointForPosition(false);
    inputRow->setAnchorPoint({0.5f, 0.5f});
    inputRow->setPosition({winSize.width / 2.f, rowY});

    auto idLabel = CCLabelBMFont::create(tr("music.song_id_label").c_str(), "bigFont.fnt");
    idLabel->setScale(0.45f);
    idLabel->setID("id-label"_spr);
    inputRow->addChild(idLabel);

    m_songIdInput = TextInput::create(85.f, tr("music.short_id").c_str());
    m_songIdInput->setCommonFilter(geode::CommonFilter::Uint);
    m_songIdInput->setMaxCharCount(10);
    m_songIdInput->setID("song-id-input"_spr);
    inputRow->addChild(m_songIdInput);

    auto loadSpr = ButtonSprite::create(tr("music.load_song").c_str(), 50, true,
        "bigFont.fnt", "GJ_button_01.png", 22.f, 0.55f);
    auto loadBtn = CCMenuItemSpriteExtra::create(loadSpr, this,
        menu_selector(ProfileMusicPopup::onLoadSong));
    loadBtn->setID("load-song-btn"_spr);
    inputRow->addChild(loadBtn);

    CCMenuItemSpriteExtra* searchBtn = nullptr;
    auto searchSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png");
    if (!searchSpr) searchSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_searchBtn_001.png");
    if (searchSpr) {
        searchSpr->setScale(0.55f);
        searchBtn = CCMenuItemSpriteExtra::create(searchSpr, this,
            menu_selector(ProfileMusicPopup::onSearchSong));
    } else {
        auto fbSpr = ButtonSprite::create(tr("music.search.button").c_str(), 40, true,
            "bigFont.fnt", "GJ_button_05.png", 18.f, 0.50f);
        searchBtn = CCMenuItemSpriteExtra::create(fbSpr, this,
            menu_selector(ProfileMusicPopup::onSearchSong));
    }
    searchBtn->setID("search-song-btn"_spr);
    inputRow->addChild(searchBtn);

    if (hasCustomBtn) {
        auto customSpr = ButtonSprite::create(tr("music.file").c_str(), 40, true,
            "bigFont.fnt", "GJ_button_04.png", 18.f, 0.55f);
        auto customBtn = CCMenuItemSpriteExtra::create(customSpr, this,
            menu_selector(ProfileMusicPopup::onLoadCustomFile));
        customBtn->setID("custom-file-btn"_spr);
        inputRow->addChild(customBtn);
    }

    inputRow->setLayout(
        RowLayout::create()
            ->setGap(7.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
    );
    inputRow->updateLayout();

    m_mainLayer->addChild(inputRow, 10);

    m_songInfoLabel = CCLabelBMFont::create(tr("music.no_song_loaded_short").c_str(), "goldFont.fnt");
    m_songInfoLabel->setScale(0.34f);
    m_songInfoLabel->setColor({160, 170, 185});
    m_songInfoLabel->setPosition({winSize.width / 2.f, winSize.height - 76.f});
    m_mainLayer->addChild(m_songInfoLabel);

}

void ProfileMusicPopup::createWaveformDisplay() {
    auto winSize = m_mainLayer->getContentSize();

    m_waveformWidth  = 356.f;
    m_waveformHeight = 40.f;
    m_waveformX = (winSize.width - m_waveformWidth) / 2.f;
    m_waveformY = winSize.height - 140.f;

    const float bgPad = 6.f;
    float wfBgW = m_waveformWidth + bgPad * 2.f;
    float wfBgH = m_waveformHeight + bgPad * 2.f;
    auto waveformBg = paimon::SpriteHelper::createDarkPanel(wfBgW, wfBgH, 155, 7.f);
    waveformBg->setPosition({winSize.width / 2.f - wfBgW / 2.f, m_waveformY - bgPad});
    m_mainLayer->addChild(waveformBg, 0);

    m_waveformContainer = CCNode::create();
    m_waveformContainer->setPosition({m_waveformX, m_waveformY});
    m_waveformContainer->setContentSize({m_waveformWidth, m_waveformHeight});
    m_mainLayer->addChild(m_waveformContainer, 1);

    m_selectionOverlay = CCLayerColor::create({255, 140, 0, 0});
    m_selectionOverlay->setContentSize({m_waveformWidth, m_waveformHeight});
    m_selectionOverlay->setPosition({0, 0});
    m_selectionOverlay->setVisible(false);
    m_waveformContainer->addChild(m_selectionOverlay, 1);

    m_startHandle = createHandleVisual(m_waveformHeight, {60, 230, 100}, true);
    m_startHandle->setPosition({0.f, 0.f});
    m_startHandle->setVisible(false);
    m_waveformContainer->addChild(m_startHandle, 3);

    m_endHandle = createHandleVisual(m_waveformHeight, {255, 70, 80}, false);
    m_endHandle->setPosition({m_waveformWidth * 0.5f, 0.f});
    m_endHandle->setVisible(false);
    m_waveformContainer->addChild(m_endHandle, 3);


    auto placeholderLabel = CCLabelBMFont::create(tr("music.placeholder").c_str(), "chatFont.fnt");
    placeholderLabel->setScale(0.72f);
    placeholderLabel->setOpacity(120);
    placeholderLabel->setPosition({m_waveformWidth / 2.f, m_waveformHeight / 2.f});
    placeholderLabel->setID("paimon-waveform-placeholder"_spr);
    m_waveformContainer->addChild(placeholderLabel, 0);
}

void ProfileMusicPopup::createTimeEditor() {
    auto winSize = m_mainLayer->getContentSize();

    m_timeEditorY = m_waveformY - 46.f;
const float groupOffset = 106.f;
const float labelYOff   = 27.f;

// Build a [− | input | +] group.
    auto buildGroup = [this](geode::TextInput** inputOut, int minusTag, int plusTag) -> CCMenu* {
        auto group = CCMenu::create();
        group->setContentSize({110.f, 30.f});
        group->ignoreAnchorPointForPosition(false);
        group->setAnchorPoint({0.5f, 0.5f});

        auto makeNudge = [this](const char* glyph, int tag) -> CCMenuItemSpriteExtra* {
            auto spr = ButtonSprite::create(glyph, 22, true, "bigFont.fnt",
                "GJ_button_04.png", 24.f, 0.7f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this,
                menu_selector(ProfileMusicPopup::onNudgeTime));
            btn->setTag(tag);
            return btn;
        };

        auto minusBtn = makeNudge("-", minusTag);
        minusBtn->setID(minusTag <= 2 ? "start-minus"_spr : "end-minus"_spr);
        group->addChild(minusBtn);

        auto input = TextInput::create(48.f, tr("music.time_placeholder").c_str());
        input->setFilter("0123456789:.");
        input->setMaxCharCount(7);
        input->setString("0");
        group->addChild(input);
        *inputOut = input;

        auto plusBtn = makeNudge("+", plusTag);
        plusBtn->setID(plusTag <= 2 ? "start-plus"_spr : "end-plus"_spr);
        group->addChild(plusBtn);

        group->setLayout(
            RowLayout::create()
                ->setGap(4.f)
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setAutoScale(false)
        );
        group->updateLayout();
        return group;
    };

    auto startGroup = buildGroup(&m_startTimeInput, 1, 2);
    startGroup->setID("start-time-group"_spr);
    startGroup->setPosition({winSize.width / 2.f - groupOffset - 20.f, m_timeEditorY});
    m_mainLayer->addChild(startGroup, 10);

    auto endGroup = buildGroup(&m_endTimeInput, 3, 4);
    endGroup->setID("end-time-group"_spr);
    endGroup->setPosition({winSize.width / 2.f + groupOffset, m_timeEditorY});
    m_mainLayer->addChild(endGroup, 10);

    m_startTimeInput->setCallback(
        paimon::ui::safeTextInputCallback<ProfileMusicPopup>(
            this, &ProfileMusicPopup::onStartTimeChanged));
    m_endTimeInput->setCallback(
        paimon::ui::safeTextInputCallback<ProfileMusicPopup>(
            this, &ProfileMusicPopup::onEndTimeChanged));

    auto startCap = CCLabelBMFont::create(tr("music.start_label").c_str(), "bigFont.fnt");
    startCap->setScale(0.34f);
    startCap->setColor({90, 230, 130});
    startCap->setPosition({winSize.width / 2.f - groupOffset - 20.f, m_timeEditorY + labelYOff});
    m_mainLayer->addChild(startCap, 5);

    auto endCap = CCLabelBMFont::create(tr("music.end_label").c_str(), "bigFont.fnt");
    endCap->setScale(0.34f);
    endCap->setColor({255, 110, 120});
    endCap->setPosition({winSize.width / 2.f + groupOffset, m_timeEditorY + labelYOff});
    m_mainLayer->addChild(endCap, 5);

    const float badgeCenterX = winSize.width / 2.f;
    float badgeW = 80.f, badgeH = 22.f;
    auto selBg = paimon::SpriteHelper::createColorPanel(
        badgeW, badgeH, {0, 0, 0}, 120, 5.f);
    selBg->setPosition({badgeCenterX - badgeW / 2.f, m_timeEditorY - badgeH / 2.f});
    m_mainLayer->addChild(selBg, 4);

    m_selectionLabel = CCLabelBMFont::create("20 seg", "bigFont.fnt");
    m_selectionLabel->setScale(0.40f);
    m_selectionLabel->setPosition({badgeCenterX, m_timeEditorY});
    m_mainLayer->addChild(m_selectionLabel, 5);

// Show live seconds → m:ss conversion beneath each input.
    m_startConvLabel = CCLabelBMFont::create("0:00", "goldFont.fnt");
    m_startConvLabel->setScale(0.30f);
    m_startConvLabel->setColor({150, 190, 220});
    m_startConvLabel->setPosition({winSize.width / 2.f - groupOffset - 20.f, m_timeEditorY - 28.f});
    m_mainLayer->addChild(m_startConvLabel, 5);

    m_endConvLabel = CCLabelBMFont::create("0:00", "goldFont.fnt");
    m_endConvLabel->setScale(0.30f);
    m_endConvLabel->setColor({150, 190, 220});
    m_endConvLabel->setPosition({winSize.width / 2.f + groupOffset, m_timeEditorY - 28.f});
    m_mainLayer->addChild(m_endConvLabel, 5);

    m_durationLabel = CCLabelBMFont::create(tr("music.duration_unknown").c_str(), "bigFont.fnt");
    m_durationLabel->setScale(0.28f);
    m_durationLabel->setColor({155, 170, 185});
    m_durationLabel->setPosition({winSize.width / 2.f, m_timeEditorY - 28.f});
    m_mainLayer->addChild(m_durationLabel, 1);

    addSeparatorLine(m_timeEditorY - 42.f);
    updateSelectionLabel();
}

void ProfileMusicPopup::createControlButtons() {
    auto winSize = m_mainLayer->getContentSize();

    const float row1Y     = 54.f;

    auto playbackMenu = CCMenu::create();
    playbackMenu->setID("playback-menu"_spr);
    playbackMenu->setContentSize({240.f, 38.f});
    playbackMenu->ignoreAnchorPointForPosition(false);
    playbackMenu->setAnchorPoint({0.5f, 0.5f});
    playbackMenu->setPosition({winSize.width / 2.f, row1Y});

    auto makeIconBtn = [this](const char* primaryFrame, const char* fallbackFrame,
                              const char* fallbackLabelKey, SEL_MenuHandler selector,
                              float iconScale) -> CCMenuItemSpriteExtra* {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName(primaryFrame);
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName(fallbackFrame);
        if (spr) {
            spr->setScale(iconScale);
            return CCMenuItemSpriteExtra::create(spr, this, selector);
        }
        auto fb = ButtonSprite::create(tr(fallbackLabelKey).c_str(), 50, true,
            "bigFont.fnt", "GJ_button_01.png", 20.f, 0.5f);
        return CCMenuItemSpriteExtra::create(fb, this, selector);
    };

    auto playBtn = makeIconBtn("GJ_playBtn2_001.png", "GJ_playMusicBtn_001.png",
        "music.play_preview", menu_selector(ProfileMusicPopup::onPlayPreview), 0.34f);
    playBtn->setID("play-btn"_spr);
    playbackMenu->addChild(playBtn);

    auto stopBtn = makeIconBtn("GJ_stopMusicBtn_001.png", "GJ_deleteBtn_001.png",
        "music.stop_preview", menu_selector(ProfileMusicPopup::onStopPreview), 0.40f);
    stopBtn->setID("stop-btn"_spr);
    playbackMenu->addChild(stopBtn);

    auto dlBtn = makeIconBtn("GJ_downloadBtn_001.png", "GJ_downloadsIcon_001.png",
        "music.dl_short", menu_selector(ProfileMusicPopup::onDownloadSong), 0.42f);
    dlBtn->setID("dl-btn"_spr);
    playbackMenu->addChild(dlBtn);

    playbackMenu->setLayout(
        RowLayout::create()
            ->setGap(48.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
    );
    playbackMenu->updateLayout();
    m_mainLayer->addChild(playbackMenu, 10);

    const float row2Y = 22.f;

    auto actionsMenu = CCMenu::create();
    actionsMenu->setID("actions-menu"_spr);
    actionsMenu->setContentSize({200.f, 32.f});
    actionsMenu->ignoreAnchorPointForPosition(false);
    actionsMenu->setAnchorPoint({0.5f, 0.5f});
    actionsMenu->setPosition({winSize.width / 2.f, row2Y});

    auto saveSpr = ButtonSprite::create(tr("music.save").c_str(), 70, true,
        "bigFont.fnt", "GJ_button_01.png", 24.f, 0.6f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this,
        menu_selector(ProfileMusicPopup::onSave));
    saveBtn->setID("save-btn"_spr);
    actionsMenu->addChild(saveBtn);

    auto deleteSpr = ButtonSprite::create(tr("music.delete").c_str(), 70, true,
        "bigFont.fnt", "GJ_button_06.png", 24.f, 0.6f);
    auto deleteBtn = CCMenuItemSpriteExtra::create(deleteSpr, this,
        menu_selector(ProfileMusicPopup::onDelete));
    deleteBtn->setID("delete-btn"_spr);
    actionsMenu->addChild(deleteBtn);

    actionsMenu->setLayout(
        RowLayout::create()
            ->setGap(20.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
    );
    actionsMenu->updateLayout();
    m_mainLayer->addChild(actionsMenu, 10);
}


void ProfileMusicPopup::onSearchSong(CCObject*) {
    if (m_isPreviewPlaying) {
        ProfileMusicManager::get().stopPreview();
        m_isPreviewPlaying = false;
        unschedulePlaybackTracking();
    }

    WeakRef<ProfileMusicPopup> self = this;
    auto popup = SongSearchPopup::create([self](int songID) {
        auto popup = self.lock();
        if (!popup || songID <= 0) return;

        if (popup->m_songIdInput) {
            popup->m_songIdInput->setString(std::to_string(songID));
        }
        popup->onLoadSong(nullptr);
    });
    if (popup) {
        popup->show();
    }
}

void ProfileMusicPopup::onLoadSong(CCObject*) {
    std::string idStr = m_songIdInput->getString();
    if (idStr.empty()) {
        showError(tr("music.enter_song_id"));
        return;
    }

    auto parsed = geode::utils::numFromString<int>(idStr);
    if (!parsed.isOk()) {
        showError(tr("music.invalid_song_id"));
        return;
    }
    m_songID = parsed.unwrap();
    if (m_songID <= 0) {
        showError(tr("music.invalid_song_id"));
        return;
    }

    m_isCustomFile = false;
    m_customFilePath.clear();

    showLoading();

    WeakRef<ProfileMusicPopup> self = this;
    ProfileMusicManager::get().getSongInfo(m_songID, [self](bool success, std::string const& name, std::string const& artist, int durationMs) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success) {
            popup->hideLoading();
            popup->showError(tr("music.load_error"));
            return;
        }

        popup->m_songName = name;
        popup->m_artistName = artist;
        popup->m_songDurationMs = durationMs;

        std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
        if (infoText.length() > 50) {
            infoText = infoText.substr(0, 47) + "...";
        }
        popup->m_songInfoLabel->setString(infoText.c_str());
        popup->m_songInfoLabel->setColor({255, 215, 80});

        int mins = popup->m_songDurationMs / 60000;
        int secs = (popup->m_songDurationMs % 60000) / 1000;
        popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

        if (popup->m_endMs > popup->m_songDurationMs) {
            popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
            popup->m_startMs = std::max(0, popup->m_endMs - MAX_FRAGMENT_MS);
        }

        popup->loadWaveform();
    });
}

void ProfileMusicPopup::onLoadCustomFile(CCObject*) {
    if (!ProfileMusicManager::get().canUploadCustomMusic()) {
        showError(tr("music.no_custom_perm"));
        return;
    }

    WeakRef<ProfileMusicPopup> self = this;
    pt::pickAudio([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;

        if (result.isErr() || !result.unwrap().has_value()) {
            return;
        }

        auto filePath = result.unwrap().value();
        popup->showLoading();

        auto staged = stageCustomAudioFile(filePath);
        if (!staged.has_value()) {
            popup->hideLoading();
            popup->showError(tr("music.read_audio_error"));
            return;
        }

        popup->m_isCustomFile = true;
        popup->m_customFilePath = staged.value();
        popup->m_songID = -1;

        ProfileMusicManager::get().getLocalSongInfo(popup->m_customFilePath,
            [self](bool success, std::string const& name, std::string const& artist, int durationMs) {
            auto popup = self.lock();
            if (!popup) return;

            if (!success) {
                popup->hideLoading();
                popup->showError(tr("music.read_audio_error"));
                popup->m_isCustomFile = false;
                popup->m_customFilePath.clear();
                return;
            }

            popup->m_songName = name;
            popup->m_artistName = artist;
            popup->m_songDurationMs = durationMs;

            std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
            if (infoText.length() > 50) {
                infoText = infoText.substr(0, 47) + "...";
            }
            popup->m_songInfoLabel->setString(infoText.c_str());
            popup->m_songInfoLabel->setColor({100, 200, 255});

            int mins = popup->m_songDurationMs / 60000;
            int secs = (popup->m_songDurationMs % 60000) / 1000;
            popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

            if (popup->m_endMs > popup->m_songDurationMs) {
                popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
                popup->m_startMs = std::max(0, popup->m_endMs - MAX_FRAGMENT_MS);
            }

            popup->m_previewPath = popup->m_customFilePath;

            ProfileMusicManager::get().getWaveformPeaksForFile(popup->m_customFilePath,
                [self](bool success, std::vector<float> const& peaks, int durationMs) {
                auto popup = self.lock();
                if (!popup) return;

                popup->hideLoading();

                if (!success) {
                    popup->showError(tr("music.analyze_audio_error"));
                    return;
                }

                popup->m_peaks = peaks;

                if (durationMs > 0) {
                    popup->m_songDurationMs = durationMs;
                    int mins = popup->m_songDurationMs / 60000;
                    int secs = (popup->m_songDurationMs % 60000) / 1000;
                    popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

                    popup->m_startMs = 0;
                    popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
                }

                if (auto placeholder = popup->m_waveformContainer->getChildByID("paimon-waveform-placeholder"_spr)) {
                    placeholder->removeFromParent();
                }

                popup->renderWaveform();

                if (popup->m_selectionOverlay) {
                    popup->m_selectionOverlay->setVisible(true);
                }
                if (popup->m_startHandle) {
                    popup->m_startHandle->setVisible(true);
                }
                if (popup->m_endHandle) {
                    popup->m_endHandle->setVisible(true);
                }

                popup->updateSelectionOverlay();
                popup->updateSelectionLabel();
            });
        });
    });
}

void ProfileMusicPopup::loadWaveform() {
    WeakRef<ProfileMusicPopup> self = this;

    ProfileMusicManager::get().downloadSongForPreview(m_songID, [self](bool success, std::string const& path) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success || path.empty()) {
            popup->hideLoading();
            popup->showError(tr("music.download_failed"));
            return;
        }

        popup->m_previewPath = path;

        ProfileMusicManager::get().getWaveformPeaks(popup->m_songID, [self](bool success, std::vector<float> const& peaks, int durationMs) {
            auto popup = self.lock();
            if (!popup) return;

            popup->hideLoading();

            if (!success) {
                popup->showError(tr("music.analyze_song_error"));
                return;
            }

            popup->m_peaks = peaks;

            if (durationMs > 0) {
                popup->m_songDurationMs = durationMs;

                int mins = popup->m_songDurationMs / 60000;
                int secs = (popup->m_songDurationMs % 60000) / 1000;
                popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

                popup->m_startMs = 0;
                popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
            }

            if (auto placeholder = popup->m_waveformContainer->getChildByID("paimon-waveform-placeholder"_spr)) {
                placeholder->removeFromParent();
            }

            popup->renderWaveform();

            if (popup->m_selectionOverlay) {
                popup->m_selectionOverlay->setVisible(true);
            }
            if (popup->m_startHandle) {
                popup->m_startHandle->setVisible(true);
            }
            if (popup->m_endHandle) {
                popup->m_endHandle->setVisible(true);
            }

            popup->updateSelectionOverlay();
            popup->updateSelectionLabel();
        });
    });
}

void ProfileMusicPopup::renderWaveform() {
    for (auto bar : m_waveformBars) {
        bar->removeFromParent();
    }
    m_waveformBars.clear();

    if (auto existingOrange = m_waveformContainer->getChildByID("paimon-waveform-selection"_spr)) {
        existingOrange->removeFromParent();
    }

    auto waveformDraw = PaimonDrawNode::create();
    waveformDraw->setID("paimon-waveform-draw"_spr);

    if (m_peaks.empty()) {
        cocos2d::ccColor4F lineC = {0.25f, 0.32f, 0.38f, 0.55f};
        waveformDraw->drawSegment(
            ccp(0.f, m_waveformHeight / 2.f),
            ccp(m_waveformWidth, m_waveformHeight / 2.f),
            1.5f, lineC
        );
    } else {
        const int   numBars      = 150;
        const float barWidth     = m_waveformWidth / numBars;
        const float maxBarHeight = m_waveformHeight - 6.f;
        const float centerY      = m_waveformHeight / 2.f;
        const float gap          = (barWidth > 2.f) ? 0.7f : 0.f;

        cocos2d::ccColor4F grayColor = {0.22f, 0.26f, 0.30f, 0.62f};

        for (int i = 0; i < numBars; ++i) {
            float startRatio = static_cast<float>(i)     / static_cast<float>(numBars);
            float endRatio   = static_cast<float>(i + 1) / static_cast<float>(numBars);
            int   pkStart    = static_cast<int>(startRatio * static_cast<float>(m_peaks.size()));
            int   pkEnd      = static_cast<int>(endRatio   * static_cast<float>(m_peaks.size()));
            pkEnd = std::max(pkStart + 1, pkEnd);
            pkEnd = std::min(pkEnd, static_cast<int>(m_peaks.size()));

            float peakVal = 0.f;
            for (int j = pkStart; j < pkEnd; ++j) {
                peakVal = std::max(peakVal, m_peaks[j]);
            }
            peakVal = std::max(0.f, std::min(1.f, peakVal));

            float displayVal = std::pow(peakVal, 0.55f);
            float barH       = std::max(2.f, displayVal * maxBarHeight);
            float x          = static_cast<float>(i) * barWidth;

            cocos2d::CCPoint rect[4] = {
                ccp(x + gap / 2.f,            centerY - barH / 2.f),
                ccp(x + barWidth - gap / 2.f, centerY - barH / 2.f),
                ccp(x + barWidth - gap / 2.f, centerY + barH / 2.f),
                ccp(x + gap / 2.f,            centerY + barH / 2.f)
            };
            waveformDraw->drawPolygon(rect, 4, grayColor, 0.f, grayColor);
        }
    }

    m_waveformContainer->addChild(waveformDraw, 0);
    m_waveformBars.push_back(waveformDraw);

    auto ticksDraw = PaimonDrawNode::create();
    cocos2d::ccColor4F tickC = {0.55f, 0.65f, 0.70f, 0.30f};
    for (int i = 0; i <= 10; ++i) {
        float x     = static_cast<float>(i) / 10.f * m_waveformWidth;
        float tickH = (i % 5 == 0) ? 6.f : 3.f;
        ticksDraw->drawSegment(ccp(x, 0.f),              ccp(x, tickH),                    0.6f, tickC);
        ticksDraw->drawSegment(ccp(x, m_waveformHeight), ccp(x, m_waveformHeight - tickH), 0.6f, tickC);
    }
    m_waveformContainer->addChild(ticksDraw, 2);
    m_waveformBars.push_back(ticksDraw);
}

void ProfileMusicPopup::drawSelectionBars() {
    if (m_peaks.empty() || m_songDurationMs <= 0) return;

    if (auto existingNode = m_waveformContainer->getChildByID("paimon-waveform-selection"_spr)) {
        existingNode->removeFromParent();
    }

    auto orangeDraw = PaimonDrawNode::create();
    orangeDraw->setID("paimon-waveform-selection"_spr);

    const int   numBars      = 150;
    const float barWidth     = m_waveformWidth / static_cast<float>(numBars);
    const float maxBarHeight = m_waveformHeight - 6.f;
    const float centerY      = m_waveformHeight / 2.f;
    const float gap          = (barWidth > 2.f) ? 0.7f : 0.f;

    float selStartX = msToPosition(m_startMs);
    float selEndX   = msToPosition(m_endMs);

    {
        cocos2d::ccColor4F selectionTint = {1.f, 0.55f, 0.10f, 0.10f};
        cocos2d::CCPoint stripRect[4] = {
            ccp(selStartX, 1.f),
            ccp(selEndX,   1.f),
            ccp(selEndX,   m_waveformHeight - 1.f),
            ccp(selStartX, m_waveformHeight - 1.f),
        };
        orangeDraw->drawPolygon(stripRect, 4, selectionTint, 0.f, selectionTint);
    }

    cocos2d::ccColor4F orangeGlow = {1.f, 0.65f, 0.18f, 0.45f};
    cocos2d::ccColor4F orangeCore = {1.f, 0.68f, 0.20f, 1.00f};

    for (int i = 0; i < numBars; ++i) {
        float barStartX  = static_cast<float>(i) * barWidth;
        float barCenterX = barStartX + barWidth * 0.5f;

        if (barCenterX < selStartX || barCenterX > selEndX) continue;

        float startRatio = static_cast<float>(i)     / static_cast<float>(numBars);
        float endRatio   = static_cast<float>(i + 1) / static_cast<float>(numBars);
        int   pkStart    = static_cast<int>(startRatio * static_cast<float>(m_peaks.size()));
        int   pkEnd      = static_cast<int>(endRatio   * static_cast<float>(m_peaks.size()));
        pkEnd = std::max(pkStart + 1, pkEnd);
        pkEnd = std::min(pkEnd, static_cast<int>(m_peaks.size()));

        float peakVal = 0.f;
        for (int j = pkStart; j < pkEnd; ++j) {
            peakVal = std::max(peakVal, m_peaks[j]);
        }
        peakVal = std::max(0.f, std::min(1.f, peakVal));

        float displayVal = std::pow(peakVal, 0.55f);
        float barH       = std::max(2.f, displayVal * maxBarHeight);

        const float glowExtra = 0.6f;
        cocos2d::CCPoint glowRect[4] = {
            ccp(barStartX + gap / 2.f - glowExtra,            centerY - barH / 2.f - glowExtra),
            ccp(barStartX + barWidth - gap / 2.f + glowExtra, centerY - barH / 2.f - glowExtra),
            ccp(barStartX + barWidth - gap / 2.f + glowExtra, centerY + barH / 2.f + glowExtra),
            ccp(barStartX + gap / 2.f - glowExtra,            centerY + barH / 2.f + glowExtra),
        };
        orangeDraw->drawPolygon(glowRect, 4, orangeGlow, 0.f, orangeGlow);

        cocos2d::CCPoint rect[4] = {
            ccp(barStartX + gap / 2.f,            centerY - barH / 2.f),
            ccp(barStartX + barWidth - gap / 2.f, centerY - barH / 2.f),
            ccp(barStartX + barWidth - gap / 2.f, centerY + barH / 2.f),
            ccp(barStartX + gap / 2.f,            centerY + barH / 2.f)
        };
        orangeDraw->drawPolygon(rect, 4, orangeCore, 0.f, orangeCore);
    }

    m_waveformContainer->addChild(orangeDraw, 1);
}

void ProfileMusicPopup::updateSelectionOverlay() {
    if (!m_selectionOverlay || m_songDurationMs <= 0) return;

    float startX = msToPosition(m_startMs);
    float endX   = msToPosition(m_endMs);

    m_selectionOverlay->setPosition({startX, 0});
    m_selectionOverlay->setContentSize({endX - startX, m_waveformHeight});

    if (m_startHandle) {
        m_startHandle->setPositionX(startX);
        m_startHandle->setPositionY(0.f);
    }
    if (m_endHandle) {
        m_endHandle->setPositionX(endX);
        m_endHandle->setPositionY(0.f);
    }

    drawSelectionBars();
}

void ProfileMusicPopup::updateSelectionLabel() {
    if (!m_selectionLabel) return;

    int durationSecs = (m_endMs - m_startMs) / 1000;

    std::string text = fmt::format(fmt::runtime(tr("music.frag_len_fmt")), durationSecs);
    m_selectionLabel->setString(text.c_str());

    if (durationSecs > 20 || durationSecs < 5) {
        m_selectionLabel->setColor({255, 100, 100});
    } else {
        m_selectionLabel->setColor({120, 230, 150});
    }

// Sync inputs unless the user is actively typing.
    if (!m_editingTimeInput) {
        syncTimeInputsFromSelection();
    }
}

int ProfileMusicPopup::positionToMs(float x) {
    if (m_songDurationMs <= 0) return 0;
    float ratio = x / m_waveformWidth;
    return static_cast<int>(ratio * m_songDurationMs);
}

float ProfileMusicPopup::msToPosition(int ms) {
    if (m_songDurationMs <= 0) return 0;
    return (static_cast<float>(ms) / m_songDurationMs) * m_waveformWidth;
}

void ProfileMusicPopup::clampSelection() {
    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;

    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        if (m_endMs + MIN_FRAGMENT_MS - (m_endMs - m_startMs) <= m_songDurationMs) {
            m_endMs = m_startMs + MIN_FRAGMENT_MS;
        } else {
            m_startMs = m_endMs - MIN_FRAGMENT_MS;
        }
    }

    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        m_endMs = m_startMs + MAX_FRAGMENT_MS;
    }

    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;
}

void ProfileMusicPopup::applyStartMs(int newStartMs) {
    if (m_songDurationMs <= 0) return;

    if (newStartMs < 0) newStartMs = 0;
    if (newStartMs > m_songDurationMs) newStartMs = m_songDurationMs;

    if (newStartMs > m_endMs - MIN_FRAGMENT_MS) {
// Push the end handle when the start collides with it.
        m_startMs = newStartMs;
        m_endMs   = newStartMs + MIN_FRAGMENT_MS;
        if (m_endMs > m_songDurationMs) {
            m_endMs   = m_songDurationMs;
            m_startMs = std::max(0, m_endMs - MIN_FRAGMENT_MS);
        }
    } else if (m_endMs - newStartMs > MAX_FRAGMENT_MS) {
// Move the end with the start when the maximum window length is reached.
        m_startMs = newStartMs;
        m_endMs   = newStartMs + MAX_FRAGMENT_MS;
        if (m_endMs > m_songDurationMs) {
            m_endMs   = m_songDurationMs;
            m_startMs = std::max(0, m_endMs - MAX_FRAGMENT_MS);
        }
    } else {
        m_startMs = newStartMs;
    }
}

void ProfileMusicPopup::applyEndMs(int newEndMs) {
    if (m_songDurationMs <= 0) return;

    if (newEndMs > m_songDurationMs) newEndMs = m_songDurationMs;
    if (newEndMs < 0) newEndMs = 0;

    if (newEndMs < m_startMs + MIN_FRAGMENT_MS) {
// Push the start handle when the end collides with it.
        m_endMs   = newEndMs;
        m_startMs = newEndMs - MIN_FRAGMENT_MS;
        if (m_startMs < 0) {
            m_startMs = 0;
            m_endMs   = std::min(m_songDurationMs, MIN_FRAGMENT_MS);
        }
    } else if (newEndMs - m_startMs > MAX_FRAGMENT_MS) {
        m_endMs   = newEndMs;
        m_startMs = newEndMs - MAX_FRAGMENT_MS;
        if (m_startMs < 0) {
            m_startMs = 0;
            m_endMs   = std::min(m_songDurationMs, MAX_FRAGMENT_MS);
        }
    } else {
        m_endMs = newEndMs;
    }
}

void ProfileMusicPopup::syncTimeInputsFromSelection() {
    if (!m_startTimeInput || !m_endTimeInput) return;

    m_suppressTimeInput = true;
// Inputs hold seconds; the conversion label shows m:ss.
    m_startTimeInput->setString(formatSeconds(m_startMs));
    m_endTimeInput->setString(formatSeconds(m_endMs));
    m_suppressTimeInput = false;

    updateConversionLabels();
}

void ProfileMusicPopup::updateConversionLabels() {
    if (m_startConvLabel) m_startConvLabel->setString(formatMsClock(m_startMs).c_str());
    if (m_endConvLabel)   m_endConvLabel->setString(formatMsClock(m_endMs).c_str());
}

void ProfileMusicPopup::onNudgeTime(CCObject* sender) {
    if (m_songDurationMs <= 0) return;

    auto* node = static_cast<CCNode*>(sender);
    if (!node) return;

const int step = 1000;
    switch (node->getTag()) {
        case 1: applyStartMs(m_startMs - step); break;
        case 2: applyStartMs(m_startMs + step); break;
        case 3: applyEndMs(m_endMs - step);     break;
        case 4: applyEndMs(m_endMs + step);     break;
        default: return;
    }

    updateSelectionOverlay();
    updateSelectionLabel();
}

void ProfileMusicPopup::onStartTimeChanged(std::string const& text) {
    if (m_suppressTimeInput || m_songDurationMs <= 0) return;

    auto parsed = parseClockToMs(text);
    if (!parsed.has_value()) return;

    int requested = std::max(0, std::min(m_songDurationMs, parsed.value()));

    m_editingTimeInput = true;
    applyStartMs(parsed.value());
    updateSelectionOverlay();
    updateSelectionLabel();
    updateConversionLabels();

    if (m_endTimeInput) {
        m_suppressTimeInput = true;
        m_endTimeInput->setString(formatSeconds(m_endMs));
        m_suppressTimeInput = false;
    }
// Correct the start field when the typed end is clamped.
    if (m_startTimeInput && m_startMs != requested) {
        m_suppressTimeInput = true;
        m_startTimeInput->setString(formatSeconds(m_startMs));
        m_suppressTimeInput = false;
    }
    m_editingTimeInput = false;
}

void ProfileMusicPopup::onEndTimeChanged(std::string const& text) {
    if (m_suppressTimeInput || m_songDurationMs <= 0) return;

    auto parsed = parseClockToMs(text);
    if (!parsed.has_value()) return;

    int requested = std::max(0, std::min(m_songDurationMs, parsed.value()));

    m_editingTimeInput = true;
    applyEndMs(parsed.value());
    updateSelectionOverlay();
    updateSelectionLabel();
    updateConversionLabels();

    if (m_startTimeInput) {
        m_suppressTimeInput = true;
        m_startTimeInput->setString(formatSeconds(m_startMs));
        m_suppressTimeInput = false;
    }
    if (m_endTimeInput && m_endMs != requested) {
        m_suppressTimeInput = true;
        m_endTimeInput->setString(formatSeconds(m_endMs));
        m_suppressTimeInput = false;
    }
    m_editingTimeInput = false;
}

bool ProfileMusicPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!Popup::ccTouchBegan(touch, event)) return false;

    if (m_songDurationMs <= 0) return true;

    auto touchPos = touch->getLocation();
    auto localPos = m_waveformContainer->convertToNodeSpace(touchPos);

    if (localPos.x < -20 || localPos.x > m_waveformWidth + 20 ||
        localPos.y < -20 || localPos.y > m_waveformHeight + 20) {
        return true;
    }

    float startX = msToPosition(m_startMs);
    float endX   = msToPosition(m_endMs);

    float tolerance = 26.f;

    float distToStart = std::abs(localPos.x - startX);
    float distToEnd   = std::abs(localPos.x - endX);

    bool touchingStart = distToStart < tolerance;
    bool touchingEnd   = distToEnd   < tolerance;

    if (touchingStart && touchingEnd) {
        if (distToStart < distToEnd) {
            m_isDraggingStart = true;
            m_dragStartX  = localPos.x;
            m_dragStartMs = m_startMs;
            return true;
        } else {
            m_isDraggingEnd  = true;
            m_dragStartX  = localPos.x;
            m_dragStartMs = m_endMs;
            return true;
        }
    } else if (touchingStart) {
        m_isDraggingStart = true;
        m_dragStartX  = localPos.x;
        m_dragStartMs = m_startMs;
        return true;
    } else if (touchingEnd) {
        m_isDraggingEnd  = true;
        m_dragStartX  = localPos.x;
        m_dragStartMs = m_endMs;
        return true;
    }

    if (localPos.x >= startX && localPos.x <= endX) {
        m_isDraggingSelection = true;
        m_dragStartX  = localPos.x;
        m_dragStartMs = m_startMs;
        return true;
    }

    return true;
}

void ProfileMusicPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (m_songDurationMs <= 0) return;

    auto touchPos = touch->getLocation();
    auto localPos = m_waveformContainer->convertToNodeSpace(touchPos);

    localPos.x = std::max(0.f, std::min(m_waveformWidth, localPos.x));

    if (m_isDraggingStart) {
        applyStartMs(positionToMs(localPos.x));
    }
    else if (m_isDraggingEnd) {
        applyEndMs(positionToMs(localPos.x));
    }
    else if (m_isDraggingSelection) {
        int   deltaMs  = positionToMs(localPos.x) - positionToMs(m_dragStartX);
        int   duration = m_endMs - m_startMs;
        int   newStart = m_dragStartMs + deltaMs;

        if (newStart < 0) newStart = 0;
        if (newStart + duration > m_songDurationMs) {
            newStart = m_songDurationMs - duration;
        }
        if (newStart < 0) newStart = 0;

        m_startMs = newStart;
        m_endMs   = newStart + duration;
    }
    else {
        return;
    }

    updateSelectionOverlay();
    updateSelectionLabel();
}

void ProfileMusicPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_isDraggingStart     = false;
    m_isDraggingEnd       = false;
    m_isDraggingSelection = false;
}

void ProfileMusicPopup::onPlayPreview(CCObject*) {
    if (m_isCustomFile) {
        if (m_customFilePath.empty()) {
            showError(tr("music.custom_no_file"));
            return;
        }
        ProfileMusicManager::get().playPreview(m_customFilePath, m_startMs, m_endMs);
    } else {
        if (m_previewPath.empty() || m_songID <= 0) {
            showError(tr("music.song_required_first"));
            return;
        }
        ProfileMusicManager::get().playPreview(m_previewPath, m_startMs, m_endMs);
    }

    m_isPreviewPlaying = true;
    if (!m_playbackCursor) {
        buildPlaybackCursor();
    }
    if (m_playbackCursor) {
        m_playbackCursor->setVisible(true);
    }
    schedulePlaybackTracking();
}

void ProfileMusicPopup::onStopPreview(CCObject*) {
    ProfileMusicManager::get().stopPreview();

    m_isPreviewPlaying = false;
    unschedulePlaybackTracking();
    if (m_playbackCursor) {
        m_playbackCursor->setVisible(false);
    }
}

void ProfileMusicPopup::onDownloadSong(CCObject*) {
    if (m_isCustomFile) {
        PaimonNotify::create(tr("music.song_already_local").c_str(), NotificationIcon::Info)->show();
        return;
    }

    if (m_songID <= 0) {
        showError(tr("music.song_required_first"));
        return;
    }

    showLoading();

    WeakRef<ProfileMusicPopup> self = this;
    ProfileMusicManager::get().downloadSongForPreview(m_songID, [self](bool success, std::string const& path) {
        auto popup = self.lock();
        if (!popup) return;

        popup->hideLoading();

        if (success) {
            popup->m_previewPath = path;
            PaimonNotify::create(tr("music.song_dl_success").c_str(), NotificationIcon::Success)->show();
        } else {
            popup->showError(tr("music.download_error"));
        }
    });
}

void ProfileMusicPopup::onSave(CCObject*) {
    if (m_isCustomFile) {
        if (m_customFilePath.empty()) {
            showError(tr("music.custom_no_file"));
            return;
        }
    } else {
        if (m_songID <= 0) {
            showError(tr("music.song_required_first"));
            return;
        }
    }

    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        showError(tr("music.fragment_max"));
        return;
    }

    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        showError(tr("music.fragment_min"));
        return;
    }

    showLoading();

    ProfileMusicManager::ProfileMusicConfig config;
    config.songID     = m_songID;
    config.startMs    = m_startMs;
    config.endMs      = m_endMs;
    config.volume     = 1.0f;
    config.enabled    = true;
    config.songName   = m_songName;
    config.artistName = m_artistName;
    config.isCustom   = m_isCustomFile;

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        PaimonNotify::create(tr("music.account_unavailable").c_str(), NotificationIcon::Error)->show();
        return;
    }
    std::string username = accountManager->m_username;

    WeakRef<ProfileMusicPopup> self = this;

    if (m_isCustomFile) {
        ProfileMusicManager::get().uploadCustomProfileMusic(m_accountID, username, m_customFilePath, config, [self](bool success, std::string const& msg) {
            auto popup = self.lock();
            if (!popup) return;

            popup->hideLoading();

            if (success) {
                PaimonNotify::create(tr("music.custom_song_uploaded").c_str(), NotificationIcon::Success)->show();
                popup->onClose(nullptr);
            } else {
                popup->showError(fmt::format(fmt::runtime(tr("music.upload_failed_fmt")), msg));
            }
        });
    } else {
        ProfileMusicManager::get().uploadProfileMusic(m_accountID, username, config, [self](bool success, std::string const& msg) {
            auto popup = self.lock();
            if (!popup) return;

            popup->hideLoading();

            if (success) {
                PaimonNotify::create(tr("music.song_uploaded").c_str(), NotificationIcon::Success)->show();
                popup->onClose(nullptr);
            } else {
                popup->showError(fmt::format(fmt::runtime(tr("music.upload_failed_fmt")), msg));
            }
        });
    }
}

void ProfileMusicPopup::onDelete(CCObject*) {
    WeakRef<ProfileMusicPopup> self = this;

    PopupManager::get().quickPopup(
        tr("music.delete_title").c_str(),
        tr("music.delete_message"),
        tr("music.delete_cancel").c_str(),
        tr("music.delete_btn").c_str(),
        [self](auto, bool confirmed) {
            auto popup = self.lock();
            if (!popup) return;

            if (confirmed) {
                popup->showLoading();

                auto* accountManager = GJAccountManager::get();
                if (!accountManager) {
                    popup->hideLoading();
                    PaimonNotify::create(tr("music.account_unavailable").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                std::string username = accountManager->m_username;

                ProfileMusicManager::get().deleteProfileMusic(popup->m_accountID, username, [self](bool success, std::string const& msg) {
                    auto popup = self.lock();
                    if (!popup) return;

                    popup->hideLoading();

                    if (success) {
                        PaimonNotify::create(tr("music.deleted_ok").c_str(), NotificationIcon::Success)->show();
                        popup->onClose(nullptr);
                    } else {
                        popup->showError(fmt::format(fmt::runtime(tr("music.delete_failed")), msg));
                    }
                });
            }
        }
    ).showInstant();
}

void ProfileMusicPopup::onClose(CCObject* sender) {
    unschedulePlaybackTracking();
    m_isPreviewPlaying = false;

    paimon::ui::detachGeodeTextInput(m_songIdInput);
    paimon::ui::detachGeodeTextInput(m_startTimeInput);
    paimon::ui::detachGeodeTextInput(m_endTimeInput);

    ProfileMusicManager::get().stopPreview();
    Popup::onClose(sender);
}

void ProfileMusicPopup::onExit() {
    unschedulePlaybackTracking();
    m_isPreviewPlaying = false;

    paimon::ui::detachGeodeTextInput(m_songIdInput);
    paimon::ui::detachGeodeTextInput(m_startTimeInput);
    paimon::ui::detachGeodeTextInput(m_endTimeInput);

    ProfileMusicManager::get().stopPreview();
    Popup::onExit();
}

void ProfileMusicPopup::loadExistingConfig() {
    WeakRef<ProfileMusicPopup> self = this;
    ProfileMusicManager::get().getProfileMusicConfig(m_accountID, [self](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success || (config.songID <= 0 && !config.isCustom)) return;

        popup->m_songID     = config.songID;
        popup->m_startMs    = config.startMs;
        popup->m_endMs      = config.endMs;
        popup->m_songName   = config.songName;
        popup->m_artistName = config.artistName;
        popup->m_isCustomFile = config.isCustom;

        if (config.isCustom) {
            std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
            if (infoText.length() > 50) {
                infoText = infoText.substr(0, 47) + "...";
            }
            popup->m_songInfoLabel->setString(infoText.c_str());
            popup->m_songInfoLabel->setColor({100, 200, 255});
        } else {
            popup->m_songIdInput->setString(std::to_string(popup->m_songID));
            popup->onLoadSong(nullptr);
        }
    });
}

void ProfileMusicPopup::showLoading() {
    if (m_loadingSpinner) return;

    m_loadingSpinner = PaimonLoadingOverlay::create(tr("music.loading_default").c_str(), 30.f);
    m_loadingSpinner->show(m_mainLayer, 100);
}

void ProfileMusicPopup::hideLoading() {
    if (m_loadingSpinner) {
        m_loadingSpinner->dismiss();
        m_loadingSpinner = nullptr;
    }
}

void ProfileMusicPopup::showError(std::string const& message) {
    PopupManager::get().alert(tr("music.error_title"), message, tr("music.ok")).showInstant();
}


void ProfileMusicPopup::buildPlaybackCursor() {
    if (!m_waveformContainer || m_playbackCursor) return;

    auto cursor = CCNode::create();
    cursor->setContentSize({4.f, m_waveformHeight});
    cursor->setAnchorPoint({0.5f, 0.f});

    auto draw = PaimonDrawNode::create();

    cocos2d::ccColor4F glow = {1.f, 1.f, 1.f, 0.35f};
    draw->drawSegment(
        ccp(2.f, 0.f),
        ccp(2.f, m_waveformHeight),
        3.5f, glow);

    cocos2d::ccColor4F core = {1.f, 1.f, 1.f, 0.95f};
    draw->drawSegment(
        ccp(2.f, 0.f),
        ccp(2.f, m_waveformHeight),
        1.2f, core);

    cocos2d::ccColor4F head = {1.f, 1.f, 1.f, 0.95f};
    cocos2d::CCPoint topDiamond[4] = {
        ccp(2.f, m_waveformHeight + 4.f),
        ccp(6.f, m_waveformHeight),
        ccp(2.f, m_waveformHeight - 4.f),
        ccp(-2.f, m_waveformHeight),
    };
    draw->drawPolygon(topDiamond, 4, head, 0.f, head);

    cocos2d::CCPoint botDiamond[4] = {
        ccp(2.f, -4.f),
        ccp(6.f, 0.f),
        ccp(2.f, 4.f),
        ccp(-2.f, 0.f),
    };
    draw->drawPolygon(botDiamond, 4, head, 0.f, head);

    cursor->addChild(draw);
    cursor->setVisible(false);

    m_waveformContainer->addChild(cursor, 4);
    m_playbackCursor = cursor;
}

void ProfileMusicPopup::schedulePlaybackTracking() {
    if (m_cursorScheduled) return;
    this->schedule(schedule_selector(ProfileMusicPopup::updatePlaybackCursor), 1.f / 30.f);
    m_cursorScheduled = true;
}

void ProfileMusicPopup::unschedulePlaybackTracking() {
    if (!m_cursorScheduled) return;
    this->unschedule(schedule_selector(ProfileMusicPopup::updatePlaybackCursor));
    m_cursorScheduled = false;
}

void ProfileMusicPopup::updatePlaybackCursorPosition() {
    if (!m_playbackCursor || m_songDurationMs <= 0) return;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) {
        m_playbackCursor->setVisible(false);
        return;
    }

    if (!engine->isMusicPlaying(0) || !ProfileMusicManager::get().isPlaying()) {
        m_playbackCursor->setVisible(false);
        return;
    }

    int currentMs = static_cast<int>(engine->getMusicTimeMS(0));
    if (currentMs < 0) currentMs = 0;
    if (currentMs > m_songDurationMs) currentMs = m_songDurationMs;

    float x = msToPosition(currentMs);
    if (x < 0.f) x = 0.f;
    if (x > m_waveformWidth) x = m_waveformWidth;

    m_playbackCursor->setVisible(true);
    m_playbackCursor->setPositionX(x);
    m_playbackCursor->setPositionY(0.f);
}

void ProfileMusicPopup::updatePlaybackCursor(float dt) {
    m_cursorPulse += dt * 6.f;
    if (m_cursorPulse > 6.2831853f) m_cursorPulse -= 6.2831853f;
    if (m_playbackCursor) {
        float pulse = 1.f + 0.08f * std::sin(m_cursorPulse);
        m_playbackCursor->setScaleX(pulse);
    }

    if (!m_isPreviewPlaying) {
        unschedulePlaybackTracking();
        if (m_playbackCursor) m_playbackCursor->setVisible(false);
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->isMusicPlaying(0) || !ProfileMusicManager::get().isPlaying()) {
        m_isPreviewPlaying = false;
        unschedulePlaybackTracking();
        if (m_playbackCursor) m_playbackCursor->setVisible(false);
        return;
    }

    updatePlaybackCursorPosition();
}
