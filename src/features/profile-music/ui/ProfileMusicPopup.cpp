#include "ProfileMusicPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../framework/PermissionPolicy.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

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

    this->setTitle("Profile Music");

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("main-menu"_spr);
    m_mainMenu->setPosition(CCPointZero);
    m_mainLayer->addChild(m_mainMenu);

    // geode::Popup ya maneja touch priority via force priority system;
    // no sobreescribir con un valor hardcodeado.

    createSongIdInput();
    createWaveformDisplay();
    createControlButtons();

    // Cargar configuracion existente si la hay
    loadExistingConfig();

    paimon::markDynamicPopup(this);
    return true;
}

void ProfileMusicPopup::createSongIdInput() {
    auto winSize = m_mainLayer->getContentSize(); // {400, 260}

    // Label
    auto idLabel = CCLabelBMFont::create("Song ID:", "bigFont.fnt");
    idLabel->setScale(0.35f);
    idLabel->setAnchorPoint({0.f, 0.5f});
    idLabel->setPosition({15.f, winSize.height - 38.f});
    m_mainLayer->addChild(idLabel);

    // Input field
    m_songIdInput = TextInput::create(100.f, "ID...");
    m_songIdInput->setPosition({115.f, winSize.height - 38.f});
    m_songIdInput->setCommonFilter(geode::CommonFilter::Uint);
    m_songIdInput->setMaxCharCount(10);
    m_songIdInput->setID("song-id-input"_spr);
    m_mainLayer->addChild(m_songIdInput, 11);

    // Load button
    auto loadSpr = ButtonSprite::create("Load", 50, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.6f);
    auto loadBtn = CCMenuItemSpriteExtra::create(loadSpr, this, menu_selector(ProfileMusicPopup::onLoadSong));
    loadBtn->setPosition({225.f, winSize.height - 38.f});
    loadBtn->setID("load-song-btn"_spr);
    m_mainMenu->addChild(loadBtn);

    // Custom File button (only visible for authorized users)
    if (ProfileMusicManager::get().canUploadCustomMusic()) {
        auto customSpr = ButtonSprite::create("File", 40, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.55f);
        auto customBtn = CCMenuItemSpriteExtra::create(customSpr, this, menu_selector(ProfileMusicPopup::onLoadCustomFile));
        customBtn->setPosition({275.f, winSize.height - 38.f});
        customBtn->setID("custom-file-btn"_spr);
        m_mainMenu->addChild(customBtn);
    }

    // Song info label
    m_songInfoLabel = CCLabelBMFont::create("No song loaded", "goldFont.fnt");
    m_songInfoLabel->setScale(0.32f);
    m_songInfoLabel->setColor({160, 170, 185});
    m_songInfoLabel->setPosition({winSize.width / 2.f, winSize.height - 56.f});
    m_mainLayer->addChild(m_songInfoLabel);

    // Separator below song-info row
    addSeparatorLine(winSize.height - 66.f);
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

    // Placeholder text
    auto placeholderLabel = CCLabelBMFont::create("Enter song ID and press Load", "chatFont.fnt");
    placeholderLabel->setScale(0.72f);
    placeholderLabel->setOpacity(120);
    placeholderLabel->setPosition({m_waveformWidth / 2.f, m_waveformHeight / 2.f});
    placeholderLabel->setTag(999);
    m_waveformContainer->addChild(placeholderLabel, 0);

    // Selection time — small badge-like panel behind the label
    float badgeW = 160.f, badgeH = 18.f;
    auto selBg = paimon::SpriteHelper::createColorPanel(
        badgeW, badgeH, {30, 65, 90}, 110, 4.f
    );
    selBg->setPosition({winSize.width / 2.f - badgeW / 2.f, m_waveformY - 14.f - badgeH / 2.f});
    m_mainLayer->addChild(selBg, 0);

    m_selectionLabel = CCLabelBMFont::create("0:00 - 0:20", "bigFont.fnt");
    m_selectionLabel->setScale(0.32f);
    m_selectionLabel->setPosition({winSize.width / 2.f, m_waveformY - 14.f});
    m_mainLayer->addChild(m_selectionLabel, 1);

    // Duration label (smaller, below selection label)
    m_durationLabel = CCLabelBMFont::create("Duration: --:-- ", "bigFont.fnt");
    m_durationLabel->setScale(0.26f);
    m_durationLabel->setColor({155, 170, 185});
    m_durationLabel->setPosition({winSize.width / 2.f, m_waveformY - 28.f});
    m_mainLayer->addChild(m_durationLabel, 1);

    // Separator between waveform area and buttons
    addSeparatorLine(m_waveformY - 48.f);
    updateSelectionLabel();
}

void ProfileMusicPopup::createControlButtons() {
    auto winSize = m_mainLayer->getContentSize(); // {400, 260}

    // --- Row 1: playback controls ---
    const float row1Y     = 65.f;
    const float labelYOff = 14.f;   // offset below button centre for text label

    float cx = winSize.width / 2.f;

    // Play
    float playX = cx - 70.f;
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn2_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png");
        if (spr) {
            spr->setScale(0.45f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileMusicPopup::onPlayPreview));
            btn->setPosition({playX, row1Y});
            m_mainMenu->addChild(btn);
        } else {
            auto fb = ButtonSprite::create("Play", 50, true, "bigFont.fnt", "GJ_button_01.png", 20.f, 0.5f);
            auto btn = CCMenuItemSpriteExtra::create(fb, this, menu_selector(ProfileMusicPopup::onPlayPreview));
            btn->setPosition({playX, row1Y});
            m_mainMenu->addChild(btn);
        }
        auto lbl = CCLabelBMFont::create("Preview", "bigFont.fnt");
        lbl->setScale(0.26f);
        lbl->setOpacity(170);
        lbl->setPosition({playX, row1Y - labelYOff});
        m_mainLayer->addChild(lbl);
    }

    // Stop
    float stopX = cx;
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_stopMusicBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteBtn_001.png");
        if (spr) {
            spr->setScale(0.45f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileMusicPopup::onStopPreview));
            btn->setPosition({stopX, row1Y});
            m_mainMenu->addChild(btn);
        } else {
            auto fb = ButtonSprite::create("Stop", 50, true, "bigFont.fnt", "GJ_button_06.png", 20.f, 0.5f);
            auto btn = CCMenuItemSpriteExtra::create(fb, this, menu_selector(ProfileMusicPopup::onStopPreview));
            btn->setPosition({stopX, row1Y});
            m_mainMenu->addChild(btn);
        }
        auto lbl = CCLabelBMFont::create("Stop", "bigFont.fnt");
        lbl->setScale(0.26f);
        lbl->setOpacity(170);
        lbl->setPosition({stopX, row1Y - labelYOff});
        m_mainLayer->addChild(lbl);
    }

    // Download
    float dlX = cx + 70.f;
    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_downloadBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_downloadsIcon_001.png");
        if (spr) {
            spr->setScale(0.48f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfileMusicPopup::onDownloadSong));
            btn->setPosition({dlX, row1Y});
            m_mainMenu->addChild(btn);
        } else {
            auto fb = ButtonSprite::create("DL", 50, true, "bigFont.fnt", "GJ_button_01.png", 20.f, 0.5f);
            auto btn = CCMenuItemSpriteExtra::create(fb, this, menu_selector(ProfileMusicPopup::onDownloadSong));
            btn->setPosition({dlX, row1Y});
            m_mainMenu->addChild(btn);
        }
        auto lbl = CCLabelBMFont::create("DL", "bigFont.fnt");
        lbl->setScale(0.26f);
        lbl->setOpacity(170);
        lbl->setPosition({dlX, row1Y - labelYOff});
        m_mainLayer->addChild(lbl);
    }

    // --- Row 2: save / delete ---
    const float row2Y = 28.f;

    auto saveSpr = ButtonSprite::create("Save", 70, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.6f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(ProfileMusicPopup::onSave));
    saveBtn->setPosition({cx - 45.f, row2Y});
    m_mainMenu->addChild(saveBtn);

    auto deleteSpr = ButtonSprite::create("Delete", 70, true, "bigFont.fnt", "GJ_button_06.png", 24.f, 0.6f);
    auto deleteBtn = CCMenuItemSpriteExtra::create(deleteSpr, this, menu_selector(ProfileMusicPopup::onDelete));
    deleteBtn->setPosition({cx + 45.f, row2Y});
    m_mainMenu->addChild(deleteBtn);
}


void ProfileMusicPopup::onLoadSong(CCObject*) {
    std::string idStr = m_songIdInput->getString();
    if (idStr.empty()) {
        showError("Please enter a song ID");
        return;
    }

    auto parsed = geode::utils::numFromString<int>(idStr);
    if (!parsed.isOk()) {
        showError("Invalid song ID");
        return;
    }
    m_songID = parsed.unwrap();
    if (m_songID <= 0) {
        showError("Invalid song ID");
        return;
    }

    // Reset custom file state when loading a Newgrounds song
    m_isCustomFile = false;
    m_customFilePath.clear();

    showLoading();

    WeakRef<ProfileMusicPopup> self = this;
    // Obtener info de la cancion
    ProfileMusicManager::get().getSongInfo(m_songID, [self](bool success, std::string const& name, std::string const& artist, int durationMs) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success) {
            popup->hideLoading();
            popup->showError("Could not load song info. Make sure the ID is valid.");
            return;
        }

        popup->m_songName = name;
        popup->m_artistName = artist;
        popup->m_songDurationMs = durationMs;

        // Actualizar UI
        std::string infoText = fmt::format("{} - {}", popup->m_artistName, popup->m_songName);
        if (infoText.length() > 50) {
            infoText = infoText.substr(0, 47) + "...";
        }
        popup->m_songInfoLabel->setString(infoText.c_str());
        popup->m_songInfoLabel->setColor({255, 215, 80}); // gold cuando hay cancion cargada

        int mins = popup->m_songDurationMs / 60000;
        int secs = (popup->m_songDurationMs % 60000) / 1000;
        popup->m_durationLabel->setString(fmt::format("Duration: {}:{:02d}", mins, secs).c_str());

        // Ajustar seleccion si excede la duracion
        if (popup->m_endMs > popup->m_songDurationMs) {
            popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
            popup->m_startMs = std::max(0, popup->m_endMs - MAX_FRAGMENT_MS);
        }

        // Cargar waveform
        popup->loadWaveform();
    });
}

void ProfileMusicPopup::onLoadCustomFile(CCObject*) {
    if (!ProfileMusicManager::get().canUploadCustomMusic()) {
        showError("You don't have permission to upload custom music.\nRequires Moderator, VIP, or Whitelist rank.");
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

        // Mark as custom file
        popup->m_isCustomFile = true;
        popup->m_customFilePath = geode::utils::string::pathToString(filePath);
        popup->m_songID = -1; // Custom files use -1 as song ID

        // Get song info from local file
        ProfileMusicManager::get().getLocalSongInfo(popup->m_customFilePath,
            [self](bool success, std::string const& name, std::string const& artist, int durationMs) {
            auto popup = self.lock();
            if (!popup) return;

            if (!success) {
                popup->hideLoading();
                popup->showError("Could not read audio file. Make sure it's a valid audio file.");
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
            popup->m_durationLabel->setString(fmt::format("Duration: {}:{:02d}", mins, secs).c_str());

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
                    popup->showError("Could not analyze audio file");
                    return;
                }

                popup->m_peaks = peaks;

                if (durationMs > 0) {
                    popup->m_songDurationMs = durationMs;
                    int mins = popup->m_songDurationMs / 60000;
                    int secs = (popup->m_songDurationMs % 60000) / 1000;
                    popup->m_durationLabel->setString(fmt::format("Duration: {}:{:02d}", mins, secs).c_str());

                    popup->m_startMs = 0;
                    popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
                }

                // Remove placeholder
                if (auto placeholder = popup->m_waveformContainer->getChildByTag(999)) {
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

    // Primero descargar la cancion para preview
    ProfileMusicManager::get().downloadSongForPreview(m_songID, [self](bool success, std::string const& path) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success || path.empty()) {
            popup->hideLoading();
            popup->showError("Could not download song");
            return;
        }

        // Guardar path para preview
        popup->m_previewPath = path;

        // Ahora obtener el waveform
        ProfileMusicManager::get().getWaveformPeaks(popup->m_songID, [self](bool success, std::vector<float> const& peaks, int durationMs) {
            auto popup = self.lock();
            if (!popup) return;

            popup->hideLoading();

            if (!success) {
                popup->showError("Could not analyze song");
                return;
            }

            popup->m_peaks = peaks;

            // Set duration from waveform analysis
            if (durationMs > 0) {
                popup->m_songDurationMs = durationMs;

                // Update duration label
                int mins = popup->m_songDurationMs / 60000;
                int secs = (popup->m_songDurationMs % 60000) / 1000;
                popup->m_durationLabel->setString(fmt::format("Duration: {}:{:02d}", mins, secs).c_str());

                // Set default selection to first 20 seconds (or less if song is shorter)
                popup->m_startMs = 0;
                popup->m_endMs = std::min(popup->m_songDurationMs, MAX_FRAGMENT_MS);
            }

            // Eliminar placeholder
            if (auto placeholder = popup->m_waveformContainer->getChildByTag(999)) {
                placeholder->removeFromParent();
            }

            popup->renderWaveform();

            // Mostrar overlay y handles ahora que tenemos el waveform
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
    if (auto existingOrange = m_waveformContainer->getChildByTag(997)) {
        existingOrange->removeFromParent();
    }

    auto waveformDraw = PaimonDrawNode::create();
    waveformDraw->setTag(996);

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

        // Muted gray for unselected region
        cocos2d::ccColor4F grayColor = {0.35f, 0.38f, 0.42f, 0.70f};

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
    if (auto existingNode = m_waveformContainer->getChildByTag(997)) {
        existingNode->removeFromParent();
    }

    auto orangeDraw = PaimonDrawNode::create();
    orangeDraw->setTag(997);

    const int   numBars      = 150;
    const float barWidth     = m_waveformWidth / static_cast<float>(numBars);
    const float maxBarHeight = m_waveformHeight - 6.f;
    const float centerY      = m_waveformHeight / 2.f;
    const float gap          = (barWidth > 2.f) ? 0.7f : 0.f;

    float selStartX = msToPosition(m_startMs);
    float selEndX   = msToPosition(m_endMs);

    // Bright orange for selected bars
    cocos2d::ccColor4F orangeColor = {1.0f, 0.55f, 0.08f, 0.93f};

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

        cocos2d::CCPoint rect[4] = {
            ccp(barStartX + gap / 2.f,            centerY - barH / 2.f),
            ccp(barStartX + barWidth - gap / 2.f, centerY - barH / 2.f),
            ccp(barStartX + barWidth - gap / 2.f, centerY + barH / 2.f),
            ccp(barStartX + gap / 2.f,            centerY + barH / 2.f)
        };
        orangeDraw->drawPolygon(rect, 4, orangeColor, 0.f, orangeColor);
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

    std::string text = fmt::format("{}:{:02d} - {}:{:02d} ({} sec)",
        startSecs / 60, startSecs % 60,
        endSecs / 60, endSecs % 60,
        durationSecs);

    m_selectionLabel->setString(text.c_str());

    // Color rojo si excede 20 segundos
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
    // Asegurar que no exceda la duracion de la cancion
    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;

    // Asegurar minimo de 5 segundos
    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        if (m_endMs + MIN_FRAGMENT_MS - (m_endMs - m_startMs) <= m_songDurationMs) {
            m_endMs = m_startMs + MIN_FRAGMENT_MS;
        } else {
            m_startMs = m_endMs - MIN_FRAGMENT_MS;
        }
    }

    // Asegurar maximo de 20 segundos
    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        m_endMs = m_startMs + MAX_FRAGMENT_MS;
    }

    // Re-clampar despues de ajustes
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

    // Clampar dentro del area
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
            showError("No custom file loaded");
            return;
        }
        ProfileMusicManager::get().playPreview(m_customFilePath, m_startMs, m_endMs);
    } else {
        if (m_previewPath.empty() || m_songID <= 0) {
            showError("Load a song first");
            return;
        }
        ProfileMusicManager::get().playPreview(m_previewPath, m_startMs, m_endMs);
    }
}

void ProfileMusicPopup::onStopPreview(CCObject*) {
    ProfileMusicManager::get().stopPreview();
}

void ProfileMusicPopup::onDownloadSong(CCObject*) {
    if (m_isCustomFile) {
        // Custom files are already local, no download needed
        PaimonNotify::create("Custom file is already local", NotificationIcon::Info)->show();
        return;
    }

    if (m_songID <= 0) {
        showError("Load a song first");
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
            PaimonNotify::create("Song downloaded! You can now preview.", NotificationIcon::Success)->show();
        } else {
            popup->showError("Failed to download song");
        }
    });
}

void ProfileMusicPopup::onSave(CCObject*) {
    if (m_isCustomFile) {
        // Custom file upload
        if (m_customFilePath.empty()) {
            showError("No custom file loaded");
            return;
        }
    } else {
        // Newgrounds song upload
        if (m_songID <= 0) {
            showError("Please load a song first");
            return;
        }
    }

    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        showError("Fragment must be 20 seconds or less");
        return;
    }

    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        showError("Fragment must be at least 5 seconds");
        return;
    }

    showLoading();

    ProfileMusicManager::ProfileMusicConfig config;
    config.songID     = m_songID;
    config.startMs    = m_startMs;
    config.endMs      = m_endMs;
    config.volume     = 1.0f; // Siempre usar 1.0
    config.enabled    = true;
    config.songName   = m_songName;
    config.artistName = m_artistName;
    config.isCustom   = m_isCustomFile;

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
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
                PaimonNotify::create("Custom profile music saved!", NotificationIcon::Success)->show();
                popup->onClose(nullptr);
            } else {
                popup->showError(fmt::format("Failed to save: {}", msg));
            }
        });
    } else {
        ProfileMusicManager::get().uploadProfileMusic(m_accountID, username, config, [self](bool success, std::string const& msg) {
            auto popup = self.lock();
            if (!popup) return;

            popup->hideLoading();

            if (success) {
                PaimonNotify::create("Profile music saved!", NotificationIcon::Success)->show();
                popup->onClose(nullptr);
            } else {
                popup->showError(fmt::format("Failed to save: {}", msg));
            }
        });
    }
}

void ProfileMusicPopup::onDelete(CCObject*) {
    WeakRef<ProfileMusicPopup> self = this;

    // Create a simple confirmation
    geode::createQuickPopup(
        "Delete Music",
        "Are you sure you want to remove your profile music?",
        "Cancel",
        "Delete",
        [self](auto, bool confirmed) {
            auto popup = self.lock();
            if (!popup) return;

            if (confirmed) {
                popup->showLoading();

                auto* accountManager = GJAccountManager::get();
                if (!accountManager) {
                    popup->hideLoading();
                    PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
                    return;
                }
                std::string username = accountManager->m_username;

                ProfileMusicManager::get().deleteProfileMusic(popup->m_accountID, username, [self](bool success, std::string const& msg) {
                    auto popup = self.lock();
                    if (!popup) return;

                    popup->hideLoading();

                    if (success) {
                        PaimonNotify::create("Profile music deleted!", NotificationIcon::Success)->show();
                        popup->onClose(nullptr);
                    } else {
                        popup->showError(fmt::format("Failed to delete: {}", msg));
                    }
                });
            }
        }
    );
}

void ProfileMusicPopup::onClose(CCObject* sender) {
    ProfileMusicManager::get().stopPreview();
    Popup::onClose(sender);
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

    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    m_loadingSpinner->show(m_mainLayer, 100);
}

void ProfileMusicPopup::hideLoading() {
    if (m_loadingSpinner) {
        m_loadingSpinner->dismiss();
        m_loadingSpinner = nullptr;
    }
}

void ProfileMusicPopup::showError(std::string const& message) {
    FLAlertLayer::create(nullptr, "Error", message, "OK", nullptr)->show();
}