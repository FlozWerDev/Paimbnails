#include "ProfileMusicPopup.hpp"
#include "SongSearchPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../framework/PermissionPolicy.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
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

    // GD's bundled FMOD opens audio files by narrow (non-wide) path. On Windows it
    // fails to open any path containing non-ASCII characters (accented folder names,
    // the localized "Documentos"/"Música" folders, OneDrive mirrors, etc.), which the
    // UI surfaces as "No se pudo leer el archivo de audio". The menu-music feature
    // avoids this by copying picked files into its own ASCII directory first, while
    // profile-music used to read the user's original path directly.
    //
    // Stage the picked file into the mod save dir under a plain ASCII name so every
    // downstream FMOD createSound() call (info, waveform, preview, upload) works
    // regardless of where the user picked the file from.
    std::optional<std::string> stageCustomAudioFile(std::filesystem::path const& src) {
        std::error_code ec;

        auto destDir = Mod::get()->getSaveDir() / "profile-music-import";
        std::filesystem::create_directories(destDir, ec);

        // Best-effort prune of previously staged imports. A file may still be locked
        // by FMOD while a preview is streaming; those removals just fail and are
        // ignored, so at most a couple of stale files linger.
        std::error_code iterEc;
        if (std::filesystem::is_directory(destDir, iterEc)) {
            for (auto const& entry : std::filesystem::directory_iterator(destDir, iterEc)) {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
            }
        }

        std::string ext = geode::utils::string::pathToString(src.extension());
        if (ext.empty() || ext.size() > 8) {
            ext = ".mp3"; // sane fallback; the audio picker only yields known extensions
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
    container->setContentSize({20.f, height});

    auto draw = PaimonDrawNode::create();

    cocos2d::ccColor4F c     = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.92f };
    cocos2d::ccColor4F cSoft = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.30f };

    // Glow (wide soft segment drawn first, then sharp line on top)
    draw->drawSegment(ccp(0, 0), ccp(0, height), 4.5f, cSoft);
    draw->drawSegment(ccp(0, 0), ccp(0, height), 1.8f, c);

    // Directional arrow at center, pointing inward toward the selection
    float arrowY  = height * 0.5f;
    float arrowSz = 9.f;
    cocos2d::CCPoint tri[3];
    if (isStart) {
        // Right-pointing (start of selection)
        tri[0] = ccp(0.f,            arrowY - arrowSz);
        tri[1] = ccp(0.f,            arrowY + arrowSz);
        tri[2] = ccp(arrowSz + 4.f,  arrowY);
    } else {
        // Left-pointing (end of selection)
        tri[0] = ccp(0.f,              arrowY - arrowSz);
        tri[1] = ccp(0.f,              arrowY + arrowSz);
        tri[2] = ccp(-(arrowSz + 4.f), arrowY);
    }
    draw->drawPolygon(tri, 3, c, 0.f, c);

    container->addChild(draw);
    return container;
}

bool ProfileMusicPopup::init(int accountID) {
    if (!Popup::init(400.f, 260.f)) return false;

    m_accountID = accountID;

    this->setTitle(tr("music.popup_title").c_str());

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("main-menu"_spr);
    m_mainMenu->setPosition(CCPointZero);
    m_mainLayer->addChild(m_mainMenu);

    // geode::Popup already manages touch priority; don't override with a hardcoded value.

    createSongIdInput();
    createWaveformDisplay();
    createControlButtons();

    // Load existing config if any
    loadExistingConfig();

    paimon::markDynamicPopup(this);
    return true;
}

void ProfileMusicPopup::createSongIdInput() {
    auto winSize = m_mainLayer->getContentSize(); // {400, 260}

    // Top row via RowLayout: group label + input + buttons in a CCMenu and let RowLayout
    // distribute space evenly (handles the hidden-FILE case for non-VIP cleanly).
    const float rowY        = winSize.height - 38.f;       // y = 222
    const bool  hasCustomBtn = ProfileMusicManager::get().canUploadCustomMusic();

    auto inputRow = CCMenu::create();
    inputRow->setID("input-row"_spr);
    inputRow->setContentSize({winSize.width - 24.f, 32.f});
    // Position at the row center so RowLayout lays out items around winSize.width/2.
    inputRow->ignoreAnchorPointForPosition(false);
    inputRow->setAnchorPoint({0.5f, 0.5f});
    inputRow->setPosition({winSize.width / 2.f, rowY});

    // "ID:" label
    auto idLabel = CCLabelBMFont::create(tr("music.song_id_label").c_str(), "bigFont.fnt");
    idLabel->setScale(0.45f);
    idLabel->setID("id-label"_spr);
    inputRow->addChild(idLabel);

    // Input (TextInput works inside CCMenu as a regular child)
    m_songIdInput = TextInput::create(85.f, tr("music.short_id").c_str());
    m_songIdInput->setCommonFilter(geode::CommonFilter::Uint);
    m_songIdInput->setMaxCharCount(10);
    m_songIdInput->setID("song-id-input"_spr);
    inputRow->addChild(m_songIdInput);

    // Load
    auto loadSpr = ButtonSprite::create(tr("music.load_song").c_str(), 50, true,
        "bigFont.fnt", "GJ_button_01.png", 22.f, 0.55f);
    auto loadBtn = CCMenuItemSpriteExtra::create(loadSpr, this,
        menu_selector(ProfileMusicPopup::onLoadSong));
    loadBtn->setID("load-song-btn"_spr);
    inputRow->addChild(loadBtn);

    // Search (magnifier) — fall back to a text button if the frame is missing
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

    // FILE — VIP/Mod/whitelist only; if omitted, RowLayout closes the gap automatically.
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

    // Song info label, centered below the input row.
    m_songInfoLabel = CCLabelBMFont::create(tr("music.no_song_loaded_short").c_str(), "goldFont.fnt");
    m_songInfoLabel->setScale(0.34f);
    m_songInfoLabel->setColor({160, 170, 185});
    m_songInfoLabel->setPosition({winSize.width / 2.f, winSize.height - 56.f}); // y=204
    m_mainLayer->addChild(m_songInfoLabel);

    // No separator below song-info: the dark waveform panel already acts as a visual divider.
}

void ProfileMusicPopup::createWaveformDisplay() {
    auto winSize = m_mainLayer->getContentSize(); // {400, 260}

    m_waveformWidth  = 320.f;
    m_waveformHeight = 50.f;
    m_waveformX = (winSize.width - m_waveformWidth) / 2.f;
    m_waveformY = winSize.height - 120.f; // bottom edge of waveform

    // Background panel with rounded corners
    const float bgPad = 6.f;
    float wfBgW = m_waveformWidth + bgPad * 2.f;
    float wfBgH = m_waveformHeight + bgPad * 2.f;
    auto waveformBg = paimon::SpriteHelper::createDarkPanel(wfBgW, wfBgH, 155, 7.f);
    waveformBg->setPosition({winSize.width / 2.f - wfBgW / 2.f, m_waveformY - bgPad});
    m_mainLayer->addChild(waveformBg, 0);

    // Waveform container
    m_waveformContainer = CCNode::create();
    m_waveformContainer->setPosition({m_waveformX, m_waveformY});
    m_waveformContainer->setContentSize({m_waveformWidth, m_waveformHeight});
    m_mainLayer->addChild(m_waveformContainer, 1);

    // Selection overlay — more visible than before
    m_selectionOverlay = CCLayerColor::create({255, 140, 0, 0}); // fully transparent — visual replaced by orange bars
    m_selectionOverlay->setContentSize({m_waveformWidth, m_waveformHeight});
    m_selectionOverlay->setPosition({0, 0});
    m_selectionOverlay->setVisible(false);
    m_waveformContainer->addChild(m_selectionOverlay, 1);

    // Handles — draw-node based (reliable, no sprite fallbacks needed)
    m_startHandle = createHandleVisual(m_waveformHeight, {60, 230, 100}, true);
    m_startHandle->setPosition({0.f, 0.f});
    m_startHandle->setVisible(false);
    m_waveformContainer->addChild(m_startHandle, 3);

    m_endHandle = createHandleVisual(m_waveformHeight, {255, 70, 80}, false);
    m_endHandle->setPosition({m_waveformWidth * 0.5f, 0.f});
    m_endHandle->setVisible(false);
    m_waveformContainer->addChild(m_endHandle, 3);

    // Moving playback cursor — hidden until play; built on demand in buildPlaybackCursor().

    // Placeholder text
    auto placeholderLabel = CCLabelBMFont::create(tr("music.placeholder").c_str(), "chatFont.fnt");
    placeholderLabel->setScale(0.72f);
    placeholderLabel->setOpacity(120);
    placeholderLabel->setPosition({m_waveformWidth / 2.f, m_waveformHeight / 2.f});
    placeholderLabel->setID("paimon-waveform-placeholder"_spr);
    m_waveformContainer->addChild(placeholderLabel, 0);

    // Selection time — small badge panel behind the label, nudged down ~2px to keep a margin from the waveform panel.
    float badgeW = 160.f, badgeH = 18.f;
    auto selBg = paimon::SpriteHelper::createColorPanel(
        badgeW, badgeH, {30, 65, 90}, 110, 4.f
    );
    selBg->setPosition({winSize.width / 2.f - badgeW / 2.f, m_waveformY - 16.f - badgeH / 2.f});
    m_mainLayer->addChild(selBg, 0);

    m_selectionLabel = CCLabelBMFont::create("0:00 - 0:20", "bigFont.fnt");
    m_selectionLabel->setScale(0.32f);
    m_selectionLabel->setPosition({winSize.width / 2.f, m_waveformY - 16.f});
    m_mainLayer->addChild(m_selectionLabel, 1);

    // Duration label (smaller, below selection label)
    m_durationLabel = CCLabelBMFont::create(tr("music.duration_unknown").c_str(), "bigFont.fnt");
    m_durationLabel->setScale(0.26f);
    m_durationLabel->setColor({155, 170, 185});
    m_durationLabel->setPosition({winSize.width / 2.f, m_waveformY - 30.f});
    m_mainLayer->addChild(m_durationLabel, 1);

    // Separator between waveform area and buttons
    addSeparatorLine(m_waveformY - 48.f);
    updateSelectionLabel();
}

void ProfileMusicPopup::createControlButtons() {
    auto winSize = m_mainLayer->getContentSize(); // {400, 260}

    // Row 1: Preview / Stop / DL icons via RowLayout; labels are placed after
    // updateLayout() from each button's real position.
    const float row1Y     = 65.f;
    const float labelYOff = 14.f;

    auto playbackMenu = CCMenu::create();
    playbackMenu->setID("playback-menu"_spr);
    playbackMenu->setContentSize({240.f, 38.f});
    playbackMenu->ignoreAnchorPointForPosition(false);
    playbackMenu->setAnchorPoint({0.5f, 0.5f});
    playbackMenu->setPosition({winSize.width / 2.f, row1Y});

    // Local helper to build icon buttons with a ButtonSprite fallback
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
        "music.play_preview", menu_selector(ProfileMusicPopup::onPlayPreview), 0.45f);
    playBtn->setID("play-btn"_spr);
    playbackMenu->addChild(playBtn);

    auto stopBtn = makeIconBtn("GJ_stopMusicBtn_001.png", "GJ_deleteBtn_001.png",
        "music.stop_preview", menu_selector(ProfileMusicPopup::onStopPreview), 0.45f);
    stopBtn->setID("stop-btn"_spr);
    playbackMenu->addChild(stopBtn);

    auto dlBtn = makeIconBtn("GJ_downloadBtn_001.png", "GJ_downloadsIcon_001.png",
        "music.dl_short", menu_selector(ProfileMusicPopup::onDownloadSong), 0.48f);
    dlBtn->setID("dl-btn"_spr);
    playbackMenu->addChild(dlBtn);

    playbackMenu->setLayout(
        RowLayout::create()
            ->setGap(40.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
    );
    playbackMenu->updateLayout();
    m_mainLayer->addChild(playbackMenu, 10);

    // Labels below: read each button's laid-out position and center the text under its icon.
    auto addBtnLabel = [this, &playbackMenu, row1Y, labelYOff](
            CCMenuItemSpriteExtra* btn, std::string const& text) {
        if (!btn) return;
        float worldX = playbackMenu->getPositionX()
                       + (btn->getPositionX() - playbackMenu->getContentSize().width * 0.5f);
        auto lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        lbl->setScale(0.26f);
        lbl->setOpacity(170);
        lbl->setPosition({worldX, row1Y - labelYOff});
        m_mainLayer->addChild(lbl);
    };
    addBtnLabel(playBtn, tr("music.preview"));
    addBtnLabel(stopBtn, tr("music.stop"));
    addBtnLabel(dlBtn,   tr("music.dl_short"));

    // Row 2: Save / Delete
    const float row2Y = 28.f;

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
    // Stop any active preview before opening SongSearchPopup (its rows have their own play button on the BG channel).
    if (m_isPreviewPlaying) {
        ProfileMusicManager::get().stopPreview();
        m_isPreviewPlaying = false;
        unschedulePlaybackTracking();
    }

    WeakRef<ProfileMusicPopup> self = this;
    auto popup = SongSearchPopup::create([self](int songID) {
        auto popup = self.lock();
        if (!popup || songID <= 0) return;

        // Reflect the chosen ID in the input and run the normal load flow.
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

    // Reset custom file state when loading a Newgrounds song
    m_isCustomFile = false;
    m_customFilePath.clear();

    showLoading();

    WeakRef<ProfileMusicPopup> self = this;
    // Get song info
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

        // Update UI
        std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
        if (infoText.length() > 50) {
            infoText = infoText.substr(0, 47) + "...";
        }
        popup->m_songInfoLabel->setString(infoText.c_str());
        popup->m_songInfoLabel->setColor({255, 215, 80}); // gold when a song is loaded

        int mins = popup->m_songDurationMs / 60000;
        int secs = (popup->m_songDurationMs % 60000) / 1000;
        popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

        // Clamp selection if it exceeds the duration
        if (popup->m_endMs > popup->m_songDurationMs) {
            popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
            popup->m_startMs = std::max(0, popup->m_endMs - MAX_FRAGMENT_MS);
        }

        // Load waveform
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
            return; // User cancelled or error
        }

        auto filePath = result.unwrap().value();
        popup->showLoading();

        // Stage the file under an ASCII path so GD's FMOD can open it regardless of
        // the original location (non-ASCII folders break createSound on Windows).
        auto staged = stageCustomAudioFile(filePath);
        if (!staged.has_value()) {
            popup->hideLoading();
            popup->showError(tr("music.read_audio_error"));
            return;
        }

        // Mark as custom file
        popup->m_isCustomFile = true;
        popup->m_customFilePath = staged.value();
        popup->m_songID = -1; // Custom files use -1 as song ID

        // Get song info from local file
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

            // Update UI
            std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
            if (infoText.length() > 50) {
                infoText = infoText.substr(0, 47) + "...";
            }
            popup->m_songInfoLabel->setString(infoText.c_str());
            popup->m_songInfoLabel->setColor({100, 200, 255}); // blue for custom

            int mins = popup->m_songDurationMs / 60000;
            int secs = (popup->m_songDurationMs % 60000) / 1000;
            popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

            // Adjust selection if it exceeds duration
            if (popup->m_endMs > popup->m_songDurationMs) {
                popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
                popup->m_startMs = std::max(0, popup->m_endMs - MAX_FRAGMENT_MS);
            }

            // Load waveform from local file
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

                // Remove placeholder
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

    // Download the song for preview first
    ProfileMusicManager::get().downloadSongForPreview(m_songID, [self](bool success, std::string const& path) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success || path.empty()) {
            popup->hideLoading();
            popup->showError(tr("music.download_failed"));
            return;
        }

        // Save the preview path
        popup->m_previewPath = path;

        // Now get the waveform
        ProfileMusicManager::get().getWaveformPeaks(popup->m_songID, [self](bool success, std::vector<float> const& peaks, int durationMs) {
            auto popup = self.lock();
            if (!popup) return;

            popup->hideLoading();

            if (!success) {
                popup->showError(tr("music.analyze_song_error"));
                return;
            }

            popup->m_peaks = peaks;

            // Set duration from waveform analysis
            if (durationMs > 0) {
                popup->m_songDurationMs = durationMs;

                // Update duration label
                int mins = popup->m_songDurationMs / 60000;
                int secs = (popup->m_songDurationMs % 60000) / 1000;
                popup->m_durationLabel->setString(fmt::format(fmt::runtime(tr("music.duration_fmt")), mins, secs).c_str());

                // Set default selection to first 20 seconds (or less if song is shorter)
                popup->m_startMs = 0;
                popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
            }

            // Remove placeholder
            if (auto placeholder = popup->m_waveformContainer->getChildByID("paimon-waveform-placeholder"_spr)) {
                placeholder->removeFromParent();
            }

            popup->renderWaveform();

            // Show overlay and handles now that we have the waveform
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
    // Remove previous waveform nodes
    for (auto bar : m_waveformBars) {
        bar->removeFromParent();
    }
    m_waveformBars.clear();

    // Also remove any existing orange selection bars
    if (auto existingOrange = m_waveformContainer->getChildByID("paimon-waveform-selection"_spr)) {
        existingOrange->removeFromParent();
    }

    auto waveformDraw = PaimonDrawNode::create();
    waveformDraw->setID("paimon-waveform-draw"_spr);

    if (m_peaks.empty()) {
        // Fallback: simple center line
        cocos2d::ccColor4F lineC = {0.25f, 0.32f, 0.38f, 0.55f};
        waveformDraw->drawSegment(
            ccp(0.f, m_waveformHeight / 2.f),
            ccp(m_waveformWidth, m_waveformHeight / 2.f),
            1.5f, lineC
        );
    } else {
        // 150 bars with range-max sampling for precision
        const int   numBars      = 150;
        const float barWidth     = m_waveformWidth / numBars;
        const float maxBarHeight = m_waveformHeight - 6.f;
        const float centerY      = m_waveformHeight / 2.f;
        const float gap          = (barWidth > 2.f) ? 0.7f : 0.f;

        // Dark grey low-opacity bars: the background for the orange selected bars.
        cocos2d::ccColor4F grayColor = {0.22f, 0.26f, 0.30f, 0.62f};

        for (int i = 0; i < numBars; ++i) {
            // Range-based max sampling: take the loudest peak in this bar's time slice
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

            // Power curve: exponent < 1 amplifies quiet parts → more detailed waveform
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

    // Tick marks at top and bottom edges for time reference
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

    // Remove previous orange selection bars
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

    // Very subtle orange tint strip under the whole selection, hinting the active range.
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

    // Outer glow (wide, low alpha) + bright core (narrow); selection glows orange.
    cocos2d::ccColor4F orangeGlow = {1.f, 0.65f, 0.18f, 0.45f};  // halo
    cocos2d::ccColor4F orangeCore = {1.f, 0.68f, 0.20f, 1.00f};  // bright center

    for (int i = 0; i < numBars; ++i) {
        float barStartX  = static_cast<float>(i) * barWidth;
        float barCenterX = barStartX + barWidth * 0.5f;

        // Only render bars within the selected region
        if (barCenterX < selStartX || barCenterX > selEndX) continue;

        // Range-max sampling (same as gray bars for visual consistency)
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

        // Outer glow (same rect, low alpha, slightly wider)
        const float glowExtra = 0.6f;
        cocos2d::CCPoint glowRect[4] = {
            ccp(barStartX + gap / 2.f - glowExtra,            centerY - barH / 2.f - glowExtra),
            ccp(barStartX + barWidth - gap / 2.f + glowExtra, centerY - barH / 2.f - glowExtra),
            ccp(barStartX + barWidth - gap / 2.f + glowExtra, centerY + barH / 2.f + glowExtra),
            ccp(barStartX + gap / 2.f - glowExtra,            centerY + barH / 2.f + glowExtra),
        };
        orangeDraw->drawPolygon(glowRect, 4, orangeGlow, 0.f, orangeGlow);

        // Bright core (normal rect)
        cocos2d::CCPoint rect[4] = {
            ccp(barStartX + gap / 2.f,            centerY - barH / 2.f),
            ccp(barStartX + barWidth - gap / 2.f, centerY - barH / 2.f),
            ccp(barStartX + barWidth - gap / 2.f, centerY + barH / 2.f),
            ccp(barStartX + gap / 2.f,            centerY + barH / 2.f)
        };
        orangeDraw->drawPolygon(rect, 4, orangeCore, 0.f, orangeCore);
    }

    // z=1: above gray bars (z=0), below tick marks (z=2) and handles (z=3)
    m_waveformContainer->addChild(orangeDraw, 1);
}

void ProfileMusicPopup::updateSelectionOverlay() {
    if (!m_selectionOverlay || m_songDurationMs <= 0) return;

    float startX = msToPosition(m_startMs);
    float endX   = msToPosition(m_endMs);

    // Keep overlay in sync (it is transparent, only for legacy position tracking)
    m_selectionOverlay->setPosition({startX, 0});
    m_selectionOverlay->setContentSize({endX - startX, m_waveformHeight});

    // Handles: origin at x position, y=0 (bottom of waveform)
    if (m_startHandle) {
        m_startHandle->setPositionX(startX);
        m_startHandle->setPositionY(0.f);
    }
    if (m_endHandle) {
        m_endHandle->setPositionX(endX);
        m_endHandle->setPositionY(0.f);
    }

    // Redraw orange bars for the newly selected region
    drawSelectionBars();
}

void ProfileMusicPopup::updateSelectionLabel() {
    int startSecs    = m_startMs / 1000;
    int endSecs      = m_endMs / 1000;
    int durationSecs = (m_endMs - m_startMs) / 1000;

    std::string text = fmt::format(fmt::runtime(tr("music.selection_fmt")),
        startSecs / 60, startSecs % 60,
        endSecs / 60, endSecs % 60,
        durationSecs);

    m_selectionLabel->setString(text.c_str());

    // Red if over 20 seconds
    if (durationSecs > 20) {
        m_selectionLabel->setColor({255, 100, 100});
    } else {
        m_selectionLabel->setColor({255, 255, 255});
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
    // Ensure it doesn't exceed the song duration
    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;

    // Enforce a 5-second minimum
    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        if (m_endMs + MIN_FRAGMENT_MS - (m_endMs - m_startMs) <= m_songDurationMs) {
            m_endMs = m_startMs + MIN_FRAGMENT_MS;
        } else {
            m_startMs = m_endMs - MIN_FRAGMENT_MS;
        }
    }

    // Enforce a 20-second maximum
    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        m_endMs = m_startMs + MAX_FRAGMENT_MS;
    }

    // Re-clamp after adjustments
    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;
}

bool ProfileMusicPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    // Let the parent handle it first
    if (!Popup::ccTouchBegan(touch, event)) return false;

    // Don't handle waveform touches if no song loaded
    if (m_songDurationMs <= 0) return true;

    auto touchPos = touch->getLocation();
    auto localPos = m_waveformContainer->convertToNodeSpace(touchPos);

    // Check if touch is inside waveform area
    if (localPos.x < -20 || localPos.x > m_waveformWidth + 20 ||
        localPos.y < -20 || localPos.y > m_waveformHeight + 20) {
        // Outside waveform - don't handle dragging
        return true;
    }

    float startX = msToPosition(m_startMs);
    float endX   = msToPosition(m_endMs);

    // Check handles (with tolerance) - prioritize the closest one
    float tolerance = 20.f;

    float distToStart = std::abs(localPos.x - startX);
    float distToEnd   = std::abs(localPos.x - endX);

    // Check if touching either handle
    bool touchingStart = distToStart < tolerance;
    bool touchingEnd   = distToEnd   < tolerance;

    if (touchingStart && touchingEnd) {
        // Both handles are close, pick the closest one
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

    // Check if inside selection (to move entire selection)
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

    // Clamp within the area
    localPos.x = std::max(0.f, std::min(m_waveformWidth, localPos.x));

    if (m_isDraggingStart) {
        int newStartMs = positionToMs(localPos.x);
        newStartMs = std::max(0, newStartMs);

        if (newStartMs > m_endMs - MIN_FRAGMENT_MS) {
            // Start trying to cross end: enforce minimum distance (start wins)
            m_startMs = m_endMs - MIN_FRAGMENT_MS;
            if (m_startMs < 0) { m_startMs = 0; m_endMs = MIN_FRAGMENT_MS; }
        }
        else if (m_endMs - newStartMs > MAX_FRAGMENT_MS) {
            // Going too far left: slide end left too (fixed 20-sec window)
            m_startMs = newStartMs;
            m_endMs   = newStartMs + MAX_FRAGMENT_MS;
            if (m_endMs > m_songDurationMs) {
                m_endMs   = m_songDurationMs;
                m_startMs = m_endMs - MAX_FRAGMENT_MS;
                if (m_startMs < 0) m_startMs = 0;
            }
        }
        else {
            // Normal: start moves freely, end stays
            m_startMs = newStartMs;
        }
    }
    else if (m_isDraggingEnd) {
        int newEndMs = positionToMs(localPos.x);
        newEndMs = std::min(newEndMs, m_songDurationMs);

        if (newEndMs < m_startMs + MIN_FRAGMENT_MS) {
            // End trying to cross start: enforce minimum distance (end wins)
            m_endMs = m_startMs + MIN_FRAGMENT_MS;
            if (m_endMs > m_songDurationMs) { m_endMs = m_songDurationMs; m_startMs = m_endMs - MIN_FRAGMENT_MS; }
        }
        else if (newEndMs > m_startMs + MAX_FRAGMENT_MS) {
            // Exceeds 20-sec max: slide start right too (fixed window slides)
            m_endMs   = newEndMs;
            m_startMs = newEndMs - MAX_FRAGMENT_MS;
            if (m_startMs < 0) {
                m_startMs = 0;
                m_endMs   = MAX_FRAGMENT_MS;
            }
        }
        else {
            // Normal: end moves freely, start stays
            m_endMs = newEndMs;
        }
    }
    else if (m_isDraggingSelection) {
        float deltaX  = localPos.x - m_dragStartX;
        int   deltaMs = positionToMs(m_dragStartX + deltaX) - positionToMs(m_dragStartX);

        int duration   = m_endMs - m_startMs;
        int newStartMs = m_dragStartMs + deltaMs;

        if (newStartMs < 0) newStartMs = 0;
        if (newStartMs + duration > m_songDurationMs) newStartMs = m_songDurationMs - duration;

        m_startMs = newStartMs;
        m_endMs   = newStartMs + duration;
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

    // Moving cursor: mark playing, build if needed, and start the per-frame position update (~30Hz).
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

    // Moving cursor: stop tracking and hide.
    m_isPreviewPlaying = false;
    unschedulePlaybackTracking();
    if (m_playbackCursor) {
        m_playbackCursor->setVisible(false);
    }
}

void ProfileMusicPopup::onDownloadSong(CCObject*) {
    if (m_isCustomFile) {
        // Custom files are already local, no download needed
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
        // Custom file upload
        if (m_customFilePath.empty()) {
            showError(tr("music.custom_no_file"));
            return;
        }
    } else {
        // Newgrounds song upload
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
    config.volume     = 1.0f; // always 1.0
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

    // Create a simple confirmation
    geode::createQuickPopup(
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
    );
}

void ProfileMusicPopup::onClose(CCObject* sender) {
    // Clear cursor tracking to avoid callbacks after destruction
    unschedulePlaybackTracking();
    m_isPreviewPlaying = false;

    ProfileMusicManager::get().stopPreview();
    Popup::onClose(sender);
}

void ProfileMusicPopup::onExit() {
    unschedulePlaybackTracking();
    m_isPreviewPlaying = false;
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
            // Custom song: show info label directly (no Newgrounds load)
            std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
            if (infoText.length() > 50) {
                infoText = infoText.substr(0, 47) + "...";
            }
            popup->m_songInfoLabel->setString(infoText.c_str());
            popup->m_songInfoLabel->setColor({100, 200, 255}); // blue for custom
        } else {
            // Newgrounds song: update input and load normally
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
    FLAlertLayer::create(nullptr, tr("music.error_title").c_str(), message, tr("music.ok").c_str(), nullptr)->show();
}

// Moving playback cursor

void ProfileMusicPopup::buildPlaybackCursor() {
    if (!m_waveformContainer || m_playbackCursor) return;

    // Thin (4px) container that moves across the waveform.
    auto cursor = CCNode::create();
    cursor->setContentSize({4.f, m_waveformHeight});
    cursor->setAnchorPoint({0.5f, 0.f});

    auto draw = PaimonDrawNode::create();

    // Outer glow (wide, low alpha, acts as a halo)
    cocos2d::ccColor4F glow = {1.f, 1.f, 1.f, 0.35f};
    draw->drawSegment(
        ccp(2.f, 0.f),
        ccp(2.f, m_waveformHeight),
        3.5f, glow);

    // Bright white center line
    cocos2d::ccColor4F core = {1.f, 1.f, 1.f, 0.95f};
    draw->drawSegment(
        ccp(2.f, 0.f),
        ccp(2.f, m_waveformHeight),
        1.2f, core);

    // Cursor heads: small diamonds top and bottom for visibility.
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

    // z=4: above the handles (z=3)
    m_waveformContainer->addChild(cursor, 4);
    m_playbackCursor = cursor;
}

void ProfileMusicPopup::schedulePlaybackTracking() {
    if (m_cursorScheduled) return;
    // ~30 Hz is smooth enough and cheap for this cursor.
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

    // Hide the cursor when preview ends or is paused externally, to avoid a frozen position.
    if (!engine->isMusicPlaying(0) || !ProfileMusicManager::get().isPlaying()) {
        m_playbackCursor->setVisible(false);
        return;
    }

    // getMusicTimeMS(0) returns absolute time in the audio file, which is what msToPosition() expects.
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
    // Cursor breathing: animate a slight scale instead of opacity (CCNode lacks setOpacity).
    m_cursorPulse += dt * 6.f;
    if (m_cursorPulse > 6.2831853f) m_cursorPulse -= 6.2831853f;
    if (m_playbackCursor) {
        // 0.92 .. 1.08 scale range — subtle pulse
        float pulse = 1.f + 0.08f * std::sin(m_cursorPulse);
        m_playbackCursor->setScaleX(pulse);
    }

    if (!m_isPreviewPlaying) {
        unschedulePlaybackTracking();
        if (m_playbackCursor) m_playbackCursor->setVisible(false);
        return;
    }

    // Auto-stop tracking if the engine reports no music, to avoid a ghost cursor.
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->isMusicPlaying(0) || !ProfileMusicManager::get().isPlaying()) {
        m_isPreviewPlaying = false;
        unschedulePlaybackTracking();
        if (m_playbackCursor) m_playbackCursor->setVisible(false);
        return;
    }

    updatePlaybackCursorPosition();
}