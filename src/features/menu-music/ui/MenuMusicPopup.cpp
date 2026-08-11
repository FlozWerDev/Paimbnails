#include "MenuMusicPopup.hpp"

#include "components/VinylDisc.hpp"
#include "components/CoverBlurBackground.hpp"
#include "components/CoverHero.hpp"
#include "components/NowPlayingToast.hpp"
#include "MenuMusicLibraryPopup.hpp"
#include "MenuMusicAddPopup.hpp"
#include "MenuMusicPlaylistsPopup.hpp"
#include "ExternalSongsPopup.hpp"
#include "MusicTagsPopup.hpp"
#include "MenuMusicSettingsPopup.hpp"

#include "../services/MenuMusicCopy.hpp"
#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../services/SongCoverCache.hpp"
#include "../services/MenuMusicCoverLog.hpp"

#include "../../menu-loop/services/MenuLoopManager.hpp"
#include "../../menu-loop/services/MenuLoopControl.hpp"
#include "../../menu-loop/ui/NowPlayingCard.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/TextureBudget.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../blur/BlurSystem.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/InfoAlertButton.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::menumusic {

static constexpr float kPopupW = 460.f;
static constexpr float kPopupH = 225.f;
static constexpr float kHeroWidthRatio = 0.44f;
static constexpr float kHeroSkew = 20.f;

// Use a geometric fallback so the button never has a 0x0 size.
static CCSprite* createIconSpriteWithFallback(
    std::initializer_list<const char*> frames,
    float fallbackSize,
    ccColor4F fallbackColor,
    bool fallbackTriangle = false,
    bool flipX = false
) {
    for (auto* name : frames) {
        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName(name)) {
            if (flipX) spr->setFlipX(true);
            return spr;
        }
    }
    auto node = CCSprite::create();
    if (!node) return nullptr;
    node->setContentSize({fallbackSize, fallbackSize});

    auto draw = PaimonDrawNode::create();
    if (fallbackTriangle) {
        float x = flipX ? fallbackSize : 0.f;
        float w = flipX ? -fallbackSize : fallbackSize;
        CCPoint verts[3] = {
            ccp(x, 0.f),
            ccp(x, fallbackSize),
            ccp(x + w, fallbackSize / 2.f)
        };
        draw->drawPolygon(verts, 3, fallbackColor, 0, ccc4f(0, 0, 0, 0));
    } else {
        CCPoint r[4] = {
            ccp(0, 0), ccp(fallbackSize, 0),
            ccp(fallbackSize, fallbackSize), ccp(0, fallbackSize)
        };
        draw->drawPolygon(r, 4, fallbackColor, 0, ccc4f(0, 0, 0, 0));
    }
    node->addChild(draw);
    return node;
}

MenuMusicPopup* MenuMusicPopup::create() {
    auto ret = new MenuMusicPopup();
    if (ret && ret->init(kPopupW, kPopupH)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuMusicPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);

    MenuMusicLibrary::get().load();
    MenuMusicLibrary::get().syncDownloadedSongs();

    buildFullscreenBackdrop();

    buildContentClipper();
    buildBlurBackground();
    buildTopBar();
    buildVinyl();
    buildInfoColumn();
    buildModeSelector();
    buildTransport();
    buildSeekBar();
    buildActions();
    buildBottomBar();

    coverlog::info("[MenuMusicCover] popup opened - log file: {}",
        geode::utils::string::pathToString(coverlog::logFilePath()));
    refreshFromState();

    return true;
}

void MenuMusicPopup::onEnterTransitionDidFinish() {
    Popup::onEnterTransitionDidFinish();

    if (coverlog::isEnabled()) {
        static bool s_shownLogHint = false;
        if (!s_shownLogHint) {
            s_shownLogHint = true;
            auto const logPath = geode::utils::string::pathToString(coverlog::logFilePath());
            PaimonNotify::create(
                fmt::format("Cover debug log:\n{}", logPath),
                NotificationIcon::Info,
                4.f
            )->show();
        }
    }

    m_libListenerToken = MenuMusicLibrary::get().addListener([this]() {
        this->onLibraryChanged();
    });
    m_playerListenerToken = MenuMusicPlayer::get().addListener(
        [this](const std::string& trackId) {
            this->onTrackChanged(trackId);
        });

    auto& player = MenuMusicPlayer::get();
    if (!player.isPaused() && player.currentTrack()) {
        if (m_hero) m_hero->startSpinning();
        if (m_disc) m_disc->startSpinning();
    }

    this->schedule(
        schedule_selector(MenuMusicPopup::pollExternalSong), 0.5f);

    this->schedule(
        schedule_selector(MenuMusicPopup::tickSeekUpdate), 0.06f);

    this->schedule(
        schedule_selector(MenuMusicPopup::pressAndHoldSeek), 0.125f);

    this->schedule(
        schedule_selector(MenuMusicPopup::syncCoverChrome), 1.f);
}

void MenuMusicPopup::onExit() {
    SongCoverCache::get().cancelAllPending();
    m_pendingSongCoverID = 0;
    this->unschedule(schedule_selector(MenuMusicPopup::pollExternalSong));
    this->unschedule(schedule_selector(MenuMusicPopup::tickSeekUpdate));
    this->unschedule(schedule_selector(MenuMusicPopup::pressAndHoldSeek));
    this->unschedule(schedule_selector(MenuMusicPopup::syncCoverChrome));
    if (m_libListenerToken) {
        MenuMusicLibrary::get().removeListener(m_libListenerToken);
        m_libListenerToken = 0;
    }
    if (m_playerListenerToken) {
        MenuMusicPlayer::get().removeListener(m_playerListenerToken);
        m_playerListenerToken = 0;
    }
    Popup::onExit();
}

void MenuMusicPopup::buildFullBackground() {
}

void MenuMusicPopup::buildContentClipper() {
    auto size = m_mainLayer->getContentSize();

    const float inset = 3.f;
    const float cornerRadius = 3.f;

    CCSize clipSize{size.width - inset * 2.f, size.height - inset * 2.f};

    auto stencil = paimon::SpriteHelper::createRoundedRectStencil(
        clipSize.width, clipSize.height, cornerRadius);
    m_contentClip = CCClippingNode::create();
    if (!m_contentClip) return;
    m_contentClip->setStencil(stencil);
    m_contentClip->setAlphaThreshold(0.05f);
    m_contentClip->setContentSize(clipSize);
    m_contentClip->setAnchorPoint({0.5f, 0.5f});
    m_contentClip->setPosition({size.width / 2.f, size.height / 2.f});
    m_contentClip->setID("mm-content-clip"_spr);
    m_mainLayer->addChild(m_contentClip, 1);
}

void MenuMusicPopup::buildBlurBackground() {
    if (!m_contentClip) return;
    auto size = m_contentClip->getContentSize();
    m_bg = CoverBlurBackground::create(size);
    if (!m_bg) return;
    m_bg->setAnchorPoint({0.5f, 0.5f});
    m_bg->setPosition(size / 2);
    m_contentClip->addChild(m_bg, 0);
}

void MenuMusicPopup::buildFullscreenBackdrop() {
    auto winSize = CCDirector::get()->getWinSize();

    m_fullscreenBackdrop = CCNode::create();
    if (!m_fullscreenBackdrop) return;
    m_fullscreenBackdrop->setContentSize(winSize);
    m_fullscreenBackdrop->setAnchorPoint({0.f, 0.f});
    m_fullscreenBackdrop->setPosition({0.f, 0.f});
    m_fullscreenBackdrop->setID("mm-fullscreen-backdrop"_spr);
    this->addChild(m_fullscreenBackdrop, -1);
}

void MenuMusicPopup::applyFullscreenCover(const std::string& coverPath) {
    if (!m_fullscreenBackdrop) return;

    // Invalidate in-flight blur jobs.
    m_fullscreenBlurGen++;
    auto gen = m_fullscreenBlurGen;

    if (coverPath.empty()) {
        if (m_fullscreenBlur) {
            m_fullscreenBlur->removeFromParent();
            m_fullscreenBlur = nullptr;
        }
        m_lastCoverPath.clear();
        return;
    }

    if (coverPath == m_lastCoverPath && m_fullscreenBlur) return;
    m_lastCoverPath = coverPath;

    auto* source = paimon::image::loadBudgeted(coverPath);
    if (!source) return;

    auto winSize = CCDirector::get()->getWinSize();

    float intensity = 7.f;
    std::string cacheKey = fmt::format(
        "menumusic_fullscreen::{}::{}x{}",
        coverPath,
        static_cast<int>(winSize.width),
        static_cast<int>(winSize.height));

    // Generation and backdrop existence reject stale callbacks.
    BlurSystem::getInstance()->buildPaimonBlurPriority(
        source, winSize, intensity, cacheKey,
        [this, gen](CCSprite* blurred) {
            if (!blurred) return;
            if (!m_fullscreenBackdrop || !m_fullscreenBackdrop->getParent()) return;
            if (gen != m_fullscreenBlurGen) return;

            auto win = CCDirector::get()->getWinSize();
            const CCSize ts = blurred->getContentSize();
            float sx = win.width / ts.width;
            float sy = win.height / ts.height;
            float scale = std::max(sx, sy);
            blurred->setScale(scale);
            blurred->setAnchorPoint({0.5f, 0.5f});
            blurred->setPosition({win.width / 2.f, win.height / 2.f});
            blurred->setOpacity(0);
            m_fullscreenBackdrop->addChild(blurred, 0);

            blurred->runAction(CCFadeTo::create(0.3f, 255));
            if (m_fullscreenBlur) {
                auto* old = m_fullscreenBlur;
                old->runAction(CCSequence::create(
                    CCFadeTo::create(0.3f, 0),
                    CCCallFunc::create(old, callfunc_selector(CCNode::removeFromParent)),
                    nullptr));
            }
            m_fullscreenBlur = blurred;
        });
}

void MenuMusicPopup::buildTopBar() {
    this->setTitle("Menu Music");
    if (m_title) {
        m_title->setPositionY(m_title->getPositionY() - 2.f);
    }

    auto size = m_mainLayer->getContentSize();

    auto topMenu = CCMenu::create();
    if (!topMenu) return;
    topMenu->setID("top-right-menu"_spr);
    topMenu->setContentSize({100.f, 26.f});
    topMenu->setAnchorPoint({1.f, 0.5f});
    topMenu->ignoreAnchorPointForPosition(false);
    topMenu->setPosition({size.width - 8.f, size.height - 18.f});

    {
        auto infoBtn = InfoAlertButton::create(
            "How Menu Music works",
            "<cj>1.</c> <cg>Add Music</c>: link (YouTube...) or file.\n"
            "<cj>2.</c> <cy>Mode</c>: <cl>Off</c>=vanilla | <cl>All Songs</c>=all | <cl>Playlist</c>=active\n"
            "<cj>3.</c> <cg>My Songs</c> > play any. <cg>Folder</c>=bulk import, <cg>GD</c>=sync GD songs.\n\n"
            "<cy>Actions:</c> hold, fav, block, copy, +List, replay toast.\n"
            "<cy>Hotkeys:</c> <cj>0-9</c> jump | <cj>Ctrl+Left/Right</c> prev/next | <cj>J/L</c> seek | <cj>Ctrl+K</c> songs | <cj>Ctrl+H</c> hold",
            0.6f);
        if (infoBtn) {
            infoBtn->setID("mm-info-btn"_spr);
            topMenu->addChild(infoBtn);
        }
    }

    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_optionsBtn02_001.png", "GJ_optionsBtn_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f));
        if (spr) {
            spr->setScale(0.6f);
            if (auto btn = CCMenuItemSpriteExtra::create(
                    spr, this, menu_selector(MenuMusicPopup::onOpenSettings))) {
                btn->setID("mm-settings-btn"_spr);
                topMenu->addChild(btn);
            }
        }
    }

    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_reloadBtn_001.png", "GJ_updateBtn_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f));
        if (spr) {
            spr->setScale(0.55f);
            if (auto btn = CCMenuItemSpriteExtra::create(
                    spr, this, menu_selector(MenuMusicPopup::onReplayToast))) {
                btn->setID("mm-replay-toast-btn"_spr);
                topMenu->addChild(btn);
            }
        }
    }

    topMenu->setLayout(RowLayout::create()
        ->setGap(5.f)
        ->setAxisAlignment(AxisAlignment::End)
        ->setCrossAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.4f, 1.0f));
    topMenu->updateLayout();
    m_mainLayer->addChild(topMenu, 20);
}

void MenuMusicPopup::onOpenSettings(cocos2d::CCObject*) {
    if (auto* popup = MenuMusicSettingsPopup::create()) {
        popup->show();
    }
}

void MenuMusicPopup::buildVinyl() {
    if (!m_contentClip) return;
    auto clipSize = m_contentClip->getContentSize();

    const float heroW = clipSize.width * kHeroWidthRatio;
    const float heroH = clipSize.height;

    m_hero = CoverHero::create({heroW, heroH}, kHeroSkew);
    if (!m_hero) return;
    m_hero->setAnchorPoint({0.f, 0.f});
    m_hero->setPosition({0.f, 0.f});
    m_hero->setID("music-hero"_spr);
    m_contentClip->addChild(m_hero, 5);

    // Keep the play/pause menu outside CCClippingNode so it receives touches.
    if (auto* disc = m_hero->getDisc()) {
        auto dummy = cocos2d::CCSprite::create();
        if (dummy) {
            dummy->setContentSize(disc->getContentSize());
            auto btn = CCMenuItemSpriteExtra::create(
                dummy, this, menu_selector(MenuMusicPopup::onPlayPause));
            if (btn) {
                btn->setContentSize(disc->getContentSize());
                btn->setAnchorPoint({0.5f, 0.5f});
                CCPoint clipperBL =
                    m_contentClip->getPosition()
                    - CCPoint(m_contentClip->getContentSize().width / 2.f,
                              m_contentClip->getContentSize().height / 2.f);
                btn->setPosition(clipperBL + m_hero->getPosition() + disc->getPosition());

                auto menu = CCMenu::create();
                menu->setContentSize(m_mainLayer->getContentSize());
                menu->setPosition({0.f, 0.f});
                menu->setAnchorPoint({0.f, 0.f});
                menu->ignoreAnchorPointForPosition(false);
                menu->addChild(btn);
                menu->setID("hero-disc-menu"_spr);
                m_mainLayer->addChild(menu, 9);
            }
        }
    }

    m_disc = nullptr;
}

void MenuMusicPopup::buildInfoColumn() {
    auto size = m_mainLayer->getContentSize();
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 12.f;
    const float colWidth = size.width - colX - 14.f;

    const float titleBoxW = colWidth;
    const float titleBoxH = 22.f;

    auto titleStencil = paimon::SpriteHelper::createRoundedRectStencil(
        titleBoxW, titleBoxH, 3.f);
    m_trackClip = cocos2d::CCClippingNode::create();
    m_trackClipWidth = titleBoxW;
    if (m_trackClip && titleStencil) {
        m_trackClip->setStencil(titleStencil);
        m_trackClip->setAlphaThreshold(0.05f);
        m_trackClip->setContentSize({titleBoxW, titleBoxH});
        m_trackClip->setAnchorPoint({0.f, 0.5f});
        m_trackClip->setPosition({colX, size.height * 0.86f - 13.f});
        m_trackClip->setID("track-clip"_spr);
        m_mainLayer->addChild(m_trackClip, 6);
    }

    m_trackLabel = CCLabelBMFont::create("No track selected", "bigFont.fnt");
    if (m_trackLabel) {
        m_trackLabel->setAnchorPoint({0.f, 0.5f});
        m_trackLabel->setPosition({6.f, titleBoxH / 2.f});
        m_trackLabel->setColor({255, 255, 255});
        m_trackLabel->setID("track-label"_spr);
        m_trackLabel->setScale(0.55f);
        if (m_trackClip) {
            m_trackClip->addChild(m_trackLabel, 1);
        } else {
            m_trackLabel->limitLabelWidth(colWidth, 0.55f, 0.3f);
            m_trackLabel->setPosition({colX, size.height * 0.80f});
            m_mainLayer->addChild(m_trackLabel, 6);
        }
    }

    m_subtitleLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_subtitleLabel) {
        m_subtitleLabel->setScale(0.5f);
        m_subtitleLabel->setColor({215, 215, 235});
        m_subtitleLabel->setAnchorPoint({0.f, 0.5f});
        m_subtitleLabel->setPosition({colX, size.height * 0.74f - 6.f});
        m_subtitleLabel->setID("subtitle-label"_spr);
        m_mainLayer->addChild(m_subtitleLabel, 6);
    }
}

void MenuMusicPopup::buildTransport() {
    auto size = m_mainLayer->getContentSize();
    auto menu = CCMenu::create();
    menu->setContentSize({size.width * 0.55f, 44.f});

    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_arrow_02_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/true, /*flipX*/false);
        if (spr) {
            spr->setScale(0.7f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MenuMusicPopup::onPrev));
            if (btn) {
                btn->setID("prev-btn"_spr);
                menu->addChild(btn);
            }
        }
    }

    {
        m_playSprite = createIconSpriteWithFallback(
            {"GJ_playBtn2_001.png", "GJ_playBtn_001.png"},
            30.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/true);
        if (!m_playSprite) m_playSprite = CCSprite::create();
        const float playBaseScale = m_playSprite->getContentSize().width > 10.f ? 0.7f : 1.f;
        m_playSprite->setScale(playBaseScale);

        const cocos2d::CCSize playVisual = {
            m_playSprite->getContentSize().width * playBaseScale,
            m_playSprite->getContentSize().height * playBaseScale
        };

        m_playBtn = CCMenuItemSpriteExtra::create(
            m_playSprite, this, menu_selector(MenuMusicPopup::onPlayPause));
        if (m_playBtn) {
            m_playBtn->setID("play-pause-btn"_spr);
            menu->addChild(m_playBtn);

            m_pauseSprite = createIconSpriteWithFallback(
                {"GJ_pauseBtn_001.png", "GJ_pauseEditorBtn_001.png"},
                22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/false);
            if (m_pauseSprite) {
                const auto pauseCS = m_pauseSprite->getContentSize();
                const float pauseRef = std::max(pauseCS.width, pauseCS.height);
                const float playRef = std::max(playVisual.width, playVisual.height);
                if (pauseRef > 0.f) {
                    m_pauseSprite->setScale(playRef / pauseRef * 0.95f);
                }
                const auto btnCS = m_playBtn->getContentSize();
                m_pauseSprite->setAnchorPoint({0.5f, 0.5f});
                m_pauseSprite->setPosition({btnCS.width / 2.f, btnCS.height / 2.f});
                m_pauseSprite->setVisible(false);
                m_playBtn->addChild(m_pauseSprite, 1);
            }
        }
    }

    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_arrow_02_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/true, /*flipX*/true);
        if (spr) {
            spr->setScale(0.7f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MenuMusicPopup::onNext));
            if (btn) {
                btn->setID("next-btn"_spr);
                menu->addChild(btn);
            }
        }
    }

    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_chanceBtn_001.png", "GJ_reloadBtn_001.png", "GJ_updateBtn_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/false);
        if (spr) {
            spr->setScale(0.75f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MenuMusicPopup::onShuffle));
            if (btn) {
                btn->setID("shuffle-btn"_spr);
                menu->addChild(btn);
            }
        }
    }

    menu->setLayout(RowLayout::create()
        ->setGap(14.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center));
    menu->setID("transport-menu"_spr);
    const float heroW = size.width * kHeroWidthRatio;
    menu->setPosition({
        heroW + (size.width - heroW) * 0.5f,
        size.height * 0.48f});
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);
}

void MenuMusicPopup::buildSeekBar() {
    auto size = m_mainLayer->getContentSize();
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 14.f;
    const float colW = size.width - colX - 14.f;
    const float rowY = size.height * 0.33f;

    m_seekRow = CCNode::create();
    if (!m_seekRow) return;
    m_seekRow->setContentSize({colW, 24.f});
    m_seekRow->setAnchorPoint({0.f, 0.5f});
    m_seekRow->setPosition({colX, rowY});
    m_seekRow->setID("seek-row"_spr);
    m_mainLayer->addChild(m_seekRow, 6);

    const float buttonsZone = 64.f;
    const float leftLabelZone = 22.f;
    const float rightLabelZone = 22.f;
    const float sliderZoneX = leftLabelZone;
    const float sliderZoneW = colW - leftLabelZone - rightLabelZone - buttonsZone;
    const float barY = 12.f;

    static constexpr float kSliderBaseWidth = 230.f;
    const float sliderScale = std::clamp(sliderZoneW / kSliderBaseWidth, 0.3f, 1.2f);

    m_seekSlider = Slider::create(this,
        menu_selector(MenuMusicPopup::onSeekSliderChanged),
        sliderScale);
    if (m_seekSlider) {
        m_seekSlider->setAnchorPoint({0.5f, 0.5f});
        m_seekSlider->setPosition({sliderZoneX + sliderZoneW / 2.f, barY});
        m_seekSlider->setValue(0.f);
        m_seekSlider->setID("seek-slider"_spr);
 // Give the slider priority over the close button and main menu.
        m_seekSlider->setTouchEnabled(true);
        if (m_seekSlider->m_touchLogic) {
            m_seekSlider->m_touchLogic->setTouchPriority(-600);
        }
        m_seekRow->addChild(m_seekSlider, 1);
    }

    m_seekCurLabel = CCLabelBMFont::create("0:00", "chatFont.fnt");
    if (m_seekCurLabel) {
        m_seekCurLabel->setScale(0.4f);
        m_seekCurLabel->setAnchorPoint({1.f, 0.5f});
        m_seekCurLabel->setPosition({sliderZoneX - 3.f, barY});
        m_seekCurLabel->setColor({235, 235, 250});
        m_seekRow->addChild(m_seekCurLabel, 2);
    }
    m_seekTotalLabel = CCLabelBMFont::create("0:00", "chatFont.fnt");
    if (m_seekTotalLabel) {
        m_seekTotalLabel->setScale(0.4f);
        m_seekTotalLabel->setAnchorPoint({0.f, 0.5f});
        m_seekTotalLabel->setPosition({sliderZoneX + sliderZoneW + 3.f, barY});
        m_seekTotalLabel->setColor({235, 235, 250});
        m_seekRow->addChild(m_seekTotalLabel, 2);
    }

    auto skipMenu = CCMenu::create();
    skipMenu->setContentSize({buttonsZone, 24.f});
    skipMenu->setPosition({
        sliderZoneX + sliderZoneW + rightLabelZone + buttonsZone / 2.f,
        barY});
    skipMenu->ignoreAnchorPointForPosition(false);

    const int seekMs = std::clamp<int>(
        static_cast<int>(Mod::get()->getSettingValue<int64_t>("menuLoopSeekAmountMs")),
        100, 30000);
    const int seekSec = std::max(1, (seekMs + 500) / 1000);
    const std::string bkwdLbl = fmt::format("-{}s", seekSec);
    const std::string fwrdLbl = fmt::format("+{}s", seekSec);

    auto makeLabeledBtn = [&](const std::string& label, SEL_MenuHandler selector)
        -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(
            label.c_str(), 28, true, "bigFont.fnt",
            "GJ_button_01.png", 14.f, 0.5f);
        if (!spr) return nullptr;
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, selector);
        return btn;
    };

    m_seekBkwdBtn = makeLabeledBtn(bkwdLbl, menu_selector(MenuMusicPopup::onSeekBackward));
    if (m_seekBkwdBtn) {
        m_seekBkwdBtn->setID("seek-bkwd-btn"_spr);
        skipMenu->addChild(m_seekBkwdBtn);
    }
    m_seekFwrdBtn = makeLabeledBtn(fwrdLbl, menu_selector(MenuMusicPopup::onSeekForward));
    if (m_seekFwrdBtn) {
        m_seekFwrdBtn->setID("seek-fwrd-btn"_spr);
        skipMenu->addChild(m_seekFwrdBtn);
    }
    skipMenu->setLayout(RowLayout::create()
        ->setGap(3.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center));
    skipMenu->setID("seek-skip-menu"_spr);
    skipMenu->updateLayout();
    m_seekRow->addChild(skipMenu, 3);
}

void MenuMusicPopup::buildActions() {
    auto size = m_mainLayer->getContentSize();
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 14.f;
    const float colW = size.width - colX - 14.f;

    auto menu = CCMenu::create();
    if (!menu) return;
    menu->setContentSize({colW, 20.f});
    menu->setPosition({colX + colW / 2.f, size.height * 0.225f});
    menu->setID("quick-actions-menu"_spr);

    auto addAction = [&](const char* text, const char* bg, SEL_MenuHandler handler,
                         const char* id, ButtonSprite** out = nullptr) {
        auto* spr = ButtonSprite::create(text, 42, true, "bigFont.fnt", bg, 14.f, 0.32f);
        if (!spr) return;
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        if (!btn) return;
        btn->setID(id);
        menu->addChild(btn);
        if (out) *out = spr;
    };

    addAction("Hold", "GJ_button_04.png", menu_selector(MenuMusicPopup::onHold),
        "hold-action-btn", &m_holdActionSpr);
    addAction("Fav", "GJ_button_05.png", menu_selector(MenuMusicPopup::onFavorite),
        "favorite-action-btn", &m_favoriteActionSpr);
    addAction("Block", "GJ_button_06.png", menu_selector(MenuMusicPopup::onBlacklist),
        "blacklist-action-btn", &m_blacklistActionSpr);
    addAction("Copy", "GJ_button_01.png", menu_selector(MenuMusicPopup::onCopy),
        "copy-action-btn");
    addAction("+ List", "GJ_button_02.png", menu_selector(MenuMusicPopup::onAddCurrentToPlaylist),
        "playlist-add-action-btn");

    menu->setLayout(RowLayout::create()
        ->setGap(4.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.45f, 1.f));
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);
}

 // Text mode selector replacing the old unlabeled icon row.
void MenuMusicPopup::buildModeSelector() {
    auto size = m_mainLayer->getContentSize();
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 14.f;
    const float colW = size.width - colX - 14.f;

    auto menu = CCMenu::create();
    if (!menu) return;
    menu->setContentSize({colW, 22.f});
    menu->setAnchorPoint({0.5f, 0.5f});
    menu->setPosition({colX + colW / 2.f, size.height * 0.64f});
    menu->setID("mode-selector-menu"_spr);

    auto makeModeBtn = [&](const char* text, int width, SEL_MenuHandler handler,
                           const char* id, ButtonSprite** sprOut) {
        auto spr = ButtonSprite::create(text, width, true, "bigFont.fnt",
            "GJ_button_01.png", 16.f, 0.38f);
        if (!spr) return;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        if (!btn) return;
        btn->setID(id);
        menu->addChild(btn);
        *sprOut = spr;
    };

    makeModeBtn("Off", 34, menu_selector(MenuMusicPopup::onModeDisabled),
        "mode-off-btn", &m_modeOffSpr);
    makeModeBtn("All Songs", 62, menu_selector(MenuMusicPopup::onModeLibrary),
        "mode-all-btn", &m_modeAllSpr);
    makeModeBtn("Playlist", 54, menu_selector(MenuMusicPopup::onModePlaylist),
        "mode-playlist-btn", &m_modePlaylistSpr);

    menu->setLayout(RowLayout::create()
        ->setGap(6.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.4f, 1.0f));
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);

    updateModeSelector();
}

void MenuMusicPopup::updateModeSelector() {
    const auto mode = MenuMusicLibrary::get().mode();

    auto tint = [](ButtonSprite* spr, bool active) {
        if (!spr) return;
        const ccColor3B c = active
            ? ccColor3B{255, 255, 255}
            : ccColor3B{110, 110, 110};
        if (spr->m_BGSprite) spr->m_BGSprite->setColor(c);
        if (spr->m_label) spr->m_label->setColor(c);
    };

 // A manual queue counts as active custom music and displays as "All Songs".
    tint(m_modeOffSpr, mode == PlaybackMode::Disabled);
    tint(m_modeAllSpr, mode == PlaybackMode::Library || mode == PlaybackMode::Queue);
    tint(m_modePlaylistSpr, mode == PlaybackMode::Playlist);
}

void MenuMusicPopup::buildBottomBar() {
    auto size = m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    menu->setContentSize({size.width * 0.95f, 32.f});
    menu->setPosition({size.width / 2.f, 22.f});

    auto makeBtn = [&](const char* text, const char* bg, SEL_MenuHandler handler) -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(text, 70, true, "bigFont.fnt", bg, 18.f, 0.45f);
        if (!spr) return nullptr;
        return CCMenuItemSpriteExtra::create(spr, this, handler);
    };

    if (auto b = makeBtn("My Songs", "GJ_button_01.png", menu_selector(MenuMusicPopup::onOpenLibrary))) {
        b->setID("lib-btn"_spr); menu->addChild(b);
    }
    if (auto b = makeBtn("Playlists", "GJ_button_04.png", menu_selector(MenuMusicPopup::onOpenPlaylists))) {
        b->setID("pl-btn"_spr); menu->addChild(b);
    }
    if (auto b = makeBtn("Folders", "GJ_button_02.png", menu_selector(MenuMusicPopup::onOpenExternalSongs))) {
        b->setID("external-btn"_spr); menu->addChild(b);
    }
    if (auto b = makeBtn("Tags", "GJ_button_04.png", menu_selector(MenuMusicPopup::onOpenTags))) {
        b->setID("tags-btn"_spr); menu->addChild(b);
    }
    if (auto b = makeBtn("Add Music", "GJ_button_05.png", menu_selector(MenuMusicPopup::onOpenAdd))) {
        b->setID("add-btn"_spr); menu->addChild(b);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(6.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.5f, 1.0f));
    menu->setID("bottom-menu"_spr);
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);
}

MenuMusicPopup::DetectedSong MenuMusicPopup::detectActiveSong(bool logResult) const {
    DetectedSong out;

    auto& player = MenuMusicPlayer::get();
    if (auto* t = player.currentTrack()) {
        out.displayName = t->displayName.empty()
            ? geode::utils::string::pathToString(std::filesystem::path(t->audioPath).stem())
            : t->displayName;
        out.artist = t->artist.empty()
            ? (t->source == TrackSource::Downloaded ? "Downloaded track" : "Local track")
            : t->artist;
        out.coverPath = t->coverPath;
        if (!t->coverPath.empty()) out.coverPaths.push_back(t->coverPath);
        out.audioPath = t->audioPath;
        out.isPaimonTrack = true;
        out.hasAnything = true;
        if (out.songID <= 0) {
            if (auto songId = resolveGDSongId(t->audioPath, t->sourceUrl)) {
                out.songID = *songId;
            } else if (logResult) {
                coverlog::warn("[MenuMusicCover] could not resolve songID from paimon track path='{}' sourceUrl='{}'",
                    t->audioPath, t->sourceUrl);
            }
        }
        if (out.coverPaths.empty() && out.songID > 0) {
            out.coverPaths = SongCoverCache::get().getCachedCoverPaths(out.songID);
            if (!out.coverPaths.empty()) out.coverPath = out.coverPaths.front();
        }
        if (logResult) {
            coverlog::info("[MenuMusicCover] detectActiveSong (paimon) songID={} covers={} path='{}'",
                out.songID, out.coverPaths.size(), out.audioPath);
        }
        if (out.songID > 0) {
            if (auto* mdm = MusicDownloadManager::sharedState()) {
                if (auto* info = mdm->getSongInfoObject(out.songID)) {
                    if (out.displayName.empty() && !info->m_songName.empty()) {
                        out.displayName = info->m_songName;
                    }
                    if (out.artist.empty() && !info->m_artistName.empty()) {
                        out.artist = info->m_artistName;
                    }
                }
            }
        }
        return out;
    }

    auto& loop = paimon::menuloop::MenuLoopManager::get();
    std::string current = resolveActiveMenuMusicPath();
    out.audioPath = current;

    if (current.empty() || isVanillaMenuLoopPath(current)) {
        if (!loop.isOverride()) {
            out.displayName = "Menu Loop (vanilla)";
            out.artist = "by RobTop";
            out.hasAnything = false;
            if (logResult) {
                coverlog::info("[MenuMusicCover] detectActiveSong: vanilla menu loop");
            }
            return out;
        }
        current = loop.getCurrentSong();
        out.audioPath = current;
    }

    std::string resolvedName = loop.getCurrentSongDisplayName();
    auto p = std::filesystem::path(current);

    if (out.songID <= 0) {
        if (auto songId = resolveGDSongId(current)) {
            out.songID = *songId;
        } else if (logResult) {
            coverlog::warn("[MenuMusicCover] could not resolve songID from menu path='{}'", current);
        }
    }
    if (out.coverPaths.empty() && out.songID > 0) {
        out.coverPaths = SongCoverCache::get().getCachedCoverPaths(out.songID);
        if (!out.coverPaths.empty()) out.coverPath = out.coverPaths.front();
    }
    if (logResult) {
        coverlog::info("[MenuMusicCover] detectActiveSong (external) songID={} covers={} path='{}'",
            out.songID, out.coverPaths.size(), out.audioPath);
    }
    if (out.songID > 0) {
        if (auto* mdm = MusicDownloadManager::sharedState()) {
            if (auto* info = mdm->getSongInfoObject(out.songID)) {
                if (out.displayName.empty() && !info->m_songName.empty()) {
                    out.displayName = info->m_songName;
                }
                if (out.artist.empty() && !info->m_artistName.empty()) {
                    out.artist = info->m_artistName;
                }
            }
        }
    }

    if (out.displayName.empty()) {
        out.displayName = !resolvedName.empty()
            ? resolvedName
            : geode::utils::string::pathToString(p.filename());
    }
    if (out.artist.empty()) {
        out.artist = loop.isOverride() ? "Override" : "External track";
    }

    out.hasAnything = !current.empty() && !isVanillaMenuLoopPath(current);
    return out;
}

void MenuMusicPopup::refreshFromState() {
    auto& player = MenuMusicPlayer::get();

    updateModeSelector();

    auto detected = detectActiveSong();
    if (detected.audioPath != m_lastDetectedPath) {
        m_failedSongCoverID = 0;
    }

    const std::string displayTitle = detected.displayName.empty()
        ? "No track selected" : detected.displayName;
    const std::string subtitle = detected.hasAnything
        ? detected.artist : "GD's normal music is playing";

    if (m_trackLabel) {
        m_trackLabel->setString(displayTitle.c_str());
        if (m_trackClip && m_trackClipWidth > 0.f) {
            m_trackLabel->setScale(1.f);
            const float available = m_trackClipWidth - 12.f;
            const float rawW = m_trackLabel->getContentSize().width;
            if (rawW > available) {
                const float s = std::max(0.35f, available / rawW);
                m_trackLabel->setScale(s);
            } else {
                m_trackLabel->setScale(0.55f);
            }
        } else {
            m_trackLabel->limitLabelWidth(
                m_mainLayer->getContentSize().width * 0.48f, 0.7f, 0.3f);
        }
    }
    if (m_subtitleLabel) m_subtitleLabel->setString(subtitle.c_str());

    std::vector<std::string> coverPaths = detected.coverPaths;
    if (coverPaths.empty() && !detected.coverPath.empty()) {
        coverPaths.push_back(detected.coverPath);
    }

    {
        std::error_code coverEc;
        std::size_t const beforeFilter = coverPaths.size();
        coverPaths.erase(
            std::remove_if(coverPaths.begin(), coverPaths.end(),
                [&](std::string const& p) {
                    if (p.empty()) return true;
                    if (!std::filesystem::exists(p, coverEc) || coverEc) {
                        coverlog::warn("[MenuMusicCover] cover file missing: '{}'", p);
                        coverEc.clear();
                        return true;
                    }
                    return false;
                }),
            coverPaths.end()
        );
        if (beforeFilter > coverPaths.size()) {
            coverlog::warn("[MenuMusicCover] filtered {} missing cover files songID={}",
                beforeFilter - coverPaths.size(), detected.songID);
        }
    }

    if (coverPaths.empty() && detected.songID > 0) {
        coverPaths = SongCoverCache::get().getCachedCoverPaths(detected.songID);
        if (!coverPaths.empty()) {
            coverlog::info("[MenuMusicCover] loaded {} covers from SongCoverCache songID={}",
                coverPaths.size(), detected.songID);
        }
    }

    if (coverPaths.empty() && detected.songID > 0) {
        if (m_pendingSongCoverID != detected.songID &&
            m_failedSongCoverID != detected.songID) {
            m_pendingSongCoverID = detected.songID;
            Ref<MenuMusicPopup> self = this;
            const int requestedSongID = detected.songID;
            coverlog::info("[MenuMusicCover] requesting covers songID={} title='{}'",
                requestedSongID, detected.displayName);
            SongCoverCache::get().requestCovers(requestedSongID,
                [self, requestedSongID](std::vector<std::string> const& paths, bool success) {
                    if (!(self->getParent() || self->isRunning())) {
                        coverlog::info("[MenuMusicCover] callback ignored: popup closed songID={}",
                            requestedSongID);
                        return;
                    }
                    if (success && !paths.empty()) {
                        self->m_failedSongCoverID = 0;
                        coverlog::info("[MenuMusicCover] covers ready songID={} count={}",
                            requestedSongID, paths.size());
                        self->refreshFromState();
                        return;
                    }
                    coverlog::warn("[MenuMusicCover] cover request failed songID={} success={} paths={}",
                        requestedSongID, success, paths.size());
                    if (self->m_pendingSongCoverID == requestedSongID) {
                        self->m_pendingSongCoverID = 0;
                    }
                    self->m_failedSongCoverID = requestedSongID;
                });
        } else {
            coverlog::info("[MenuMusicCover] cover request already pending songID={}", detected.songID);
        }
    } else if (detected.songID <= 0) {
        if (!detected.hasAnything) {
            coverlog::info("[MenuMusicCover] no custom track, skipping cover fetch");
        } else {
            coverlog::warn("[MenuMusicCover] active track has no songID path='{}'", detected.audioPath);
        }
        m_pendingSongCoverID = 0;
        m_failedSongCoverID = 0;
    } else if (!coverPaths.empty()) {
        m_failedSongCoverID = 0;
        coverlog::info("[MenuMusicCover] using {} local covers songID={}",
            coverPaths.size(), detected.songID);
    }

    applyCovers(coverPaths);

    if (m_hero) {
        const bool isPlaying = detected.hasAnything && !player.isPaused();
        m_hero->setPausedAppearance(!isPlaying);
    }
    if (m_disc) {
        if (detected.hasAnything && !player.isPaused()) {
            m_disc->startSpinning();
        } else {
            m_disc->stopSpinning();
        }
    }

    m_lastDetectedPath = detected.audioPath;

    const bool showPause = detected.hasAnything && !player.isPaused();
    if (m_playSprite) m_playSprite->setVisible(!showPause);
    if (m_pauseSprite) m_pauseSprite->setVisible(showPause);

    auto tintAction = [](ButtonSprite* spr, ccColor3B color) {
        if (!spr) return;
        if (spr->m_BGSprite) spr->m_BGSprite->setColor(color);
        if (spr->m_label) spr->m_label->setColor(color);
    };
    auto* track = player.currentTrack();
    tintAction(m_holdActionSpr, player.heldTrackId().empty()
        ? ccColor3B{180, 180, 190} : ccColor3B{130, 210, 255});
    tintAction(m_favoriteActionSpr, track && track->favorite
        ? ccColor3B{255, 220, 80} : ccColor3B{190, 190, 200});
    tintAction(m_blacklistActionSpr, track && track->blacklisted
        ? ccColor3B{255, 105, 105} : ccColor3B{190, 190, 200});
}

void MenuMusicPopup::applyCovers(std::vector<std::string> const& coverPaths) {
    m_activeCoverPaths = coverPaths;

    if (m_hero) {
        m_hero->setCoversFromPaths(coverPaths);
    }

    std::string chromePath = coverPaths.empty()
        ? std::string{}
        : (m_hero ? m_hero->getCurrentCoverPath() : coverPaths.front());

    if (m_bg) m_bg->setCoverFromPath(chromePath);
    if (m_disc) m_disc->setCoverFromPath(chromePath);
    applyFullscreenCover(chromePath);
    m_lastChromeCoverPath = chromePath;
}

void MenuMusicPopup::syncCoverChrome(float) {
    if (!m_hero || m_activeCoverPaths.size() <= 1) return;

    std::string current = m_hero->getCurrentCoverPath();
    if (current.empty() || current == m_lastChromeCoverPath) return;

    m_lastChromeCoverPath = current;
    if (m_bg) m_bg->setCoverFromPath(current);
    if (m_disc) m_disc->setCoverFromPath(current);
    applyFullscreenCover(current);
}

void MenuMusicPopup::onTrackChanged(const std::string&) { refreshFromState(); }
void MenuMusicPopup::onLibraryChanged() { refreshFromState(); }

void MenuMusicPopup::pollExternalSong(float) {
    auto detected = detectActiveSong(false);
    if (detected.audioPath == m_lastDetectedPath) {
        if (!detected.coverPaths.empty() || detected.songID <= 0) return;
        if (m_pendingSongCoverID == detected.songID) return;
        if (m_failedSongCoverID == detected.songID) return;
    }

    refreshFromState();
}

void MenuMusicPopup::onPlayPause(CCObject*) {
    auto& player = MenuMusicPlayer::get();
    if (!player.currentTrack()) {
        auto detected = detectActiveSong();
        if (detected.hasAnything) {
            if (player.isPaused()) player.resume();
            else player.pause();
            refreshFromState();
            return;
        }
        auto& lib = MenuMusicLibrary::get();
        if (!lib.hasTracks()) {
            Notification::create("Your music library is empty - use 'Add Music' first.",
                NotificationIcon::Info)->show();
            return;
        }
        if (lib.mode() == PlaybackMode::Disabled) lib.setMode(PlaybackMode::Library);
        player.playNext();
        return;
    }
    if (player.isPaused()) player.resume();
    else player.pause();
    refreshFromState();
}

void MenuMusicPopup::onPrev(CCObject*) {
    auto& player = MenuMusicPlayer::get();
    if (player.isManagingPlayback()) {
        if (player.playPrevious()) return;
        Notification::create("No previous track in history.", NotificationIcon::Info)->show();
        return;
    }
    paimon::menuloop::MenuLoopControl::previousSong();
    refreshFromState();
}

void MenuMusicPopup::onNext(CCObject*) {
    auto& lib = MenuMusicLibrary::get();
    if (lib.mode() == PlaybackMode::Disabled) {
        paimon::menuloop::MenuLoopControl::shuffleSong();
        refreshFromState();
        return;
    }
    if (!MenuMusicPlayer::get().playNext()) {
        Notification::create("No available songs in this mode.", NotificationIcon::Warning)->show();
    }
}

void MenuMusicPopup::onShuffle(CCObject*) {
    onNext(nullptr);
}

void MenuMusicPopup::onModeLibrary(CCObject*) {
    auto& lib = MenuMusicLibrary::get();
    if (!lib.hasTracks()) {
        Notification::create("Your library is empty - press 'Add Music' first.",
            NotificationIcon::Info)->show();
        return;
    }
    MenuMusicPlayer::get().setMode(PlaybackMode::Library, true);
    refreshFromState();
}
void MenuMusicPopup::onModePlaylist(CCObject*) {
    auto& lib = MenuMusicLibrary::get();
    if (lib.playlists().empty()) {
        Notification::create("Create a playlist first (Playlists button).",
            NotificationIcon::Warning)->show();
        return;
    }
    if (lib.activePlaylistId().empty()) {
        lib.setActivePlaylistId(lib.playlists().front().id);
    }
    MenuMusicPlayer::get().setMode(PlaybackMode::Playlist, true);
    refreshFromState();
}
void MenuMusicPopup::onModeDisabled(CCObject*) {
    MenuMusicPlayer::get().setMode(PlaybackMode::Disabled, false);
    refreshFromState();
}

void MenuMusicPopup::onOpenLibrary(CCObject*) {
    if (auto p = MenuMusicLibraryPopup::create()) p->show();
}
void MenuMusicPopup::onOpenPlaylists(CCObject*) {
    if (auto p = MenuMusicPlaylistsPopup::create()) p->show();
}
void MenuMusicPopup::onOpenExternalSongs(CCObject*) {
    if (auto* popup = ExternalSongsPopup::create()) popup->show();
}
void MenuMusicPopup::onOpenTags(CCObject*) {
    if (auto* popup = MusicTagsPopup::create()) popup->show();
}
void MenuMusicPopup::onOpenAdd(CCObject*) {
    if (auto p = MenuMusicAddPopup::create()) p->show();
}

void MenuMusicPopup::onHold(CCObject*) {
    auto& player = MenuMusicPlayer::get();
    if (player.isManagingPlayback()) {
        const bool hadHeldTrack = !player.heldTrackId().empty();
        if (!player.swapHeldTrack()) {
            Notification::create("Hold needs another available song.", NotificationIcon::Info)->show();
        } else {
            Notification::create(
                hadHeldTrack ? "Song swapped with the held track."
                             : "Song held. Playing another one.",
                NotificationIcon::Success)->show();
        }
    } else {
        paimon::menuloop::MenuLoopControl::holdSong();
    }
    refreshFromState();
}

void MenuMusicPopup::onFavorite(CCObject*) {
    auto& player = MenuMusicPlayer::get();
    if (auto* track = player.currentTrack()) {
        const bool favorite = !track->favorite;
        const std::string name = track->displayName;
        MenuMusicLibrary::get().setFavorite(track->id, favorite);
        Notification::create(
            fmt::format("{} {}", favorite ? "Favorited" : "Unfavorited", name),
            favorite ? NotificationIcon::Success : NotificationIcon::Info)->show();
    } else {
        paimon::menuloop::MenuLoopControl::favoriteSong();
    }
    refreshFromState();
}

void MenuMusicPopup::onBlacklist(CCObject*) {
    auto& player = MenuMusicPlayer::get();
    if (auto* track = player.currentTrack()) {
        const std::string id = track->id;
        const std::string name = track->displayName;
        MenuMusicLibrary::get().setBlacklisted(id, true);
        if (!player.playNext()) player.setMode(PlaybackMode::Disabled, false);
        Notification::create(fmt::format("Blocked {}", name), NotificationIcon::Success)->show();
    } else {
        paimon::menuloop::MenuLoopControl::blacklistSong();
    }
    refreshFromState();
}

void MenuMusicPopup::onCopy(CCObject*) {
    paimon::menuloop::MenuLoopControl::copySong();
}

void MenuMusicPopup::onAddCurrentToPlaylist(CCObject*) {
    auto& lib = MenuMusicLibrary::get();
    if (lib.playlists().empty()) {
        Notification::create("Create a playlist first.", NotificationIcon::Info)->show();
        if (auto* popup = MenuMusicPlaylistsPopup::create()) popup->show();
        return;
    }

    std::string trackId;
    if (auto* track = MenuMusicPlayer::get().currentTrack()) {
        trackId = track->id;
    } else {
        auto path = resolveActiveMenuMusicPath();
        if (path.empty()) {
            Notification::create("No custom song is playing.", NotificationIcon::Info)->show();
            return;
        }
        if (auto* existing = lib.findTrackByAudioPath(path)) {
            trackId = existing->id;
        } else {
            MusicTrack track;
            track.id = lib.generateId("active");
            track.audioPath = path;
            track.displayName = fallbackPathLabel(
                path, paimon::menuloop::MenuLoopManager::get().getCurrentSongDisplayName());
            track.source = TrackSource::Local;
            track.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            trackId = track.id;
            lib.addTrack(track);
        }
    }

    const std::string playlistId = lib.activePlaylistId().empty()
        ? lib.playlists().front().id
        : lib.activePlaylistId();
    lib.addTrackToPlaylist(playlistId, trackId);
    auto* playlist = lib.findPlaylist(playlistId);
    Notification::create(
        fmt::format("Added to {}", playlist ? playlist->name : "playlist"),
        NotificationIcon::Success)->show();
}

void MenuMusicPopup::onReplayToast(CCObject*) {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (MenuMusicPlayer::get().currentTrack()) {
        NowPlayingToast::showForCurrent(scene);
    } else {
        paimon::menuloop::NowPlayingCard::showForCurrentSong(scene);
    }
}

void MenuMusicPopup::onSeekBackward(CCObject*) {
    if (!Mod::get()->getSettingValue<bool>("menuLoopShowPlaybackProgress")) return;
    paimon::menuloop::MenuLoopControl::skipBackward();
}

void MenuMusicPopup::onSeekForward(CCObject*) {
    if (!Mod::get()->getSettingValue<bool>("menuLoopShowPlaybackProgress")) return;
    paimon::menuloop::MenuLoopControl::skipForward();
}

void MenuMusicPopup::onSeekSliderChanged(CCObject*) {
    if (!m_seekSlider) return;
    if (!Mod::get()->getSettingValue<bool>("menuLoopShowPlaybackProgress")) return;

    m_seekSliderDragging = true;

    auto* thumb = m_seekSlider->getThumb();
    const float v = thumb ? thumb->getValue() : m_seekSlider->getValue();
    const int pct = static_cast<int>(std::round(std::clamp(v, 0.f, 1.f) * 100.f));
    paimon::menuloop::MenuLoopControl::setSongPercentage(pct);
}

 // Hold -/+ to repeat seek after 0.5 s, then every 0.125 s.
void MenuMusicPopup::pressAndHoldSeek(float dt) {
    m_seekHoldTime += dt;
    if (!m_seekFwrdBtn || !m_seekBkwdBtn) {
        m_seekHoldTime = 0.f;
        return;
    }
    if (m_seekHoldTime < 0.5f) return;

    if (m_seekFwrdBtn->isSelected() && m_seekFwrdBtn->isVisible()
        && m_seekFwrdBtn->isEnabled()) {
        this->onSeekForward(m_seekFwrdBtn);
    } else if (m_seekBkwdBtn->isSelected() && m_seekBkwdBtn->isVisible()
               && m_seekBkwdBtn->isEnabled()) {
        this->onSeekBackward(m_seekBkwdBtn);
    } else {
        m_seekHoldTime = 0.f;
    }
}

void MenuMusicPopup::tickSeekUpdate(float) {
    const bool show = Mod::get()->getSettingValue<bool>("menuLoopShowPlaybackProgress");
    if (m_seekRow) m_seekRow->setVisible(show);
    if (!show) return;

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (!fmod || !fmod->m_backgroundMusicChannel) return;

    auto& loop = paimon::menuloop::MenuLoopManager::get();
    const std::string& current = loop.getCurrentSong();

    auto resetVisuals = [&]() {
        if (m_seekCurLabel) m_seekCurLabel->setString("--:--");
        if (m_seekTotalLabel) m_seekTotalLabel->setString("--:--");
        if (m_seekSlider && !m_seekSliderDragging) m_seekSlider->setValue(0.f);
    };

    if (fmod->getActiveMusic(0) != current) { resetVisuals(); return; }

    const int pos = static_cast<int>(fmod->getMusicTimeMS(0));
    const int total = fmod->getMusicLengthMS(0);
    if (total <= 0) { resetVisuals(); return; }

    const float ratio = std::clamp(
        static_cast<float>(pos) / static_cast<float>(total), 0.f, 1.f);

    if (m_seekSlider) {
        if (m_seekSliderDragging) {
            auto* thumb = m_seekSlider->getThumb();
            const float thumbVal = thumb ? thumb->getValue() : m_seekSlider->getValue();
            if (std::abs(thumbVal - ratio) < 0.02f) {
                m_seekSliderDragging = false;
            }
        } else {
            m_seekSlider->setValue(ratio);
        }
    }

    auto fmtTime = [](int ms) {
        int secs = ms / 1000;
        return fmt::format("{}:{:02}", secs / 60, secs % 60);
    };
    if (m_seekCurLabel) m_seekCurLabel->setString(fmtTime(pos).c_str());
    if (m_seekTotalLabel) m_seekTotalLabel->setString(fmtTime(total).c_str());

    if (auto* track = MenuMusicPlayer::get().currentTrack();
        track && total > 0 && track->durationMs != total) {
        auto updated = *track;
        updated.durationMs = total;
        MenuMusicLibrary::get().updateTrack(updated);
    }
}

void MenuMusicPopup::keyDown(cocos2d::enumKeyCodes key, double p1) {
    using cocos2d::enumKeyCodes;
    if (key == enumKeyCodes::KEY_Escape) { this->onClose(nullptr); return; }
    if (key == enumKeyCodes::KEY_Space) return;

    const bool shortcutsEnabled =
        Mod::get()->getSettingValue<bool>("menuLoopEnableKeyboardShortcuts");
    if (!shortcutsEnabled) { Popup::keyDown(key, p1); return; }

#ifdef GEODE_IS_DESKTOP
    auto* kd = cocos2d::CCKeyboardDispatcher::get();
    if (!kd) { Popup::keyDown(key, p1); return; }
    const bool isShift = kd->getShiftKeyPressed();
#ifdef GEODE_IS_MACOS
    const bool ctrlOrCmd = kd->getCommandKeyPressed();
#else
    const bool ctrlOrCmd = kd->getControlKeyPressed();
#endif

    if (key >= enumKeyCodes::KEY_Zero && key <= enumKeyCodes::KEY_Nine) {
        int pct = 10 * (static_cast<int>(key) - static_cast<int>(enumKeyCodes::KEY_Zero));
        paimon::menuloop::MenuLoopControl::setSongPercentage(pct);
        return;
    }
    if (key >= enumKeyCodes::KEY_NumPad0 && key <= enumKeyCodes::KEY_NumPad9) {
        int pct = 10 * (static_cast<int>(key) - static_cast<int>(enumKeyCodes::KEY_NumPad0));
        paimon::menuloop::MenuLoopControl::setSongPercentage(pct);
        return;
    }
    if (ctrlOrCmd && key == enumKeyCodes::KEY_R) {
        paimon::menuloop::MenuLoopControl::setSongPercentage(0);
        return;
    }
    if ((ctrlOrCmd && key == enumKeyCodes::KEY_S) ||
        (isShift && key == enumKeyCodes::KEY_N) ||
        (ctrlOrCmd && (key == enumKeyCodes::KEY_ArrowRight || key == enumKeyCodes::KEY_Right))) {
        onShuffle(nullptr);
        refreshFromState();
        return;
    }
    if ((isShift && key == enumKeyCodes::KEY_P) ||
        (ctrlOrCmd && (key == enumKeyCodes::KEY_ArrowLeft || key == enumKeyCodes::KEY_Left))) {
        onPrev(nullptr);
        refreshFromState();
        return;
    }
    if (ctrlOrCmd && key == enumKeyCodes::KEY_K) {
        this->onOpenLibrary(nullptr);
        return;
    }
    if (ctrlOrCmd && key == enumKeyCodes::KEY_H) {
        onHold(nullptr);
        return;
    }
    if (ctrlOrCmd && key == enumKeyCodes::KEY_C) {
        onCopy(nullptr);
        return;
    }
    if (isShift && key == enumKeyCodes::KEY_B) {
        onFavorite(nullptr);
        return;
    }
#else
    (void)key;
#endif
    if (Mod::get()->getSettingValue<bool>("menuLoopShowPlaybackProgress")) {
        if (key == enumKeyCodes::KEY_Right || key == enumKeyCodes::KEY_ArrowRight ||
            key == enumKeyCodes::KEY_L) {
            paimon::menuloop::MenuLoopControl::skipForward();
            return;
        }
        if (key == enumKeyCodes::KEY_Left || key == enumKeyCodes::KEY_ArrowLeft ||
            key == enumKeyCodes::KEY_J) {
            paimon::menuloop::MenuLoopControl::skipBackward();
            return;
        }
    }
    Popup::keyDown(key, p1);
}

}
