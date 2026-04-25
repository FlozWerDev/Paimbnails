#include "VideoSettingsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../services/LayerBackgroundManager.hpp"
#include "../../../video/VideoDiskCache.hpp"
#include "../../../video/VideoPlayer.hpp"
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

VideoSettingsPopup* VideoSettingsPopup::create() {
    auto ret = new VideoSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VideoSettingsPopup::init() {
    if (!Popup::init(340.f, 220.f)) return false;

    this->setTitle("Video Settings");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // ── Restore indices from saved values ─────────────────────────────

    int currentFps = paimon::settings::video::fpsLimit();
    m_fpsIndex = 2;
    for (int i = 0; i < (int)FPS_OPTIONS.size(); i++) {
        if (FPS_OPTIONS[i] == currentFps) { m_fpsIndex = i; break; }
    }

    int currentQuality = paimon::settings::video::videoQuality();
    m_qualityIndex = 0;
    for (int i = 0; i < (int)QUALITY_OPTIONS.size(); i++) {
        if (QUALITY_OPTIONS[i] == currentQuality) { m_qualityIndex = i; break; }
    }

    std::string currentBlurType = paimon::settings::video::videoBlurType();
    m_blurTypeIndex = 0;
    for (int i = 0; i < (int)BLUR_TYPE_OPTIONS.size(); i++) {
        if (BLUR_TYPE_OPTIONS[i] == currentBlurType) { m_blurTypeIndex = i; break; }
    }

    float currentIntensity = paimon::settings::video::videoBlurIntensity();
    m_blurIntensityIndex = 4;
    float minDiff = 1.0f;
    for (int i = 0; i < (int)BLUR_INTENSITY_OPTIONS.size(); i++) {
        float diff = std::abs(BLUR_INTENSITY_OPTIONS[i] - currentIntensity);
        if (diff < minDiff) { minDiff = diff; m_blurIntensityIndex = i; }
    }

    // ── Shared menu ────────────────────────────────────────────────────
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    // helper lambdas for arrow buttons
    auto makeArrowL = [&](float x, float y, SEL_MenuHandler sel) {
        auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        spr->setFlipX(true);
        spr->setScale(0.35f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setPosition({x, y});
        menu->addChild(btn);
    };
    auto makeArrowR = [&](float x, float y, SEL_MenuHandler sel) {
        auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        spr->setScale(0.35f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setPosition({x, y});
        menu->addChild(btn);
    };

    // ── Two-column layout ──────────────────────────────────────────────
    //  Left column: Playback (FPS, Quality, Audio)
    //  Right column: Filters (Blur Type, Blur Intensity)
    //  Bottom row: Cache (Clear RAM, Clear Cache, RAM label)

    const float colSpacing = 170.f;
    float leftCX  = cx - colSpacing / 2.f;
    float rightCX = cx + colSpacing / 2.f;

    const float rowSpacing = 34.f;
    float topY = content.height - 48.f;

    // layout constants per column
    auto colLayout = [&](float colCX) {
        float titleX = colCX - 55.f;
        float prevX  = colCX - 7.f;
        float valX   = colCX + 33.f;
        float nextX  = colCX + 71.f;
        float toggleX = colCX + 48.f;
        return std::make_tuple(titleX, prevX, valX, nextX, toggleX);
    };

    // ── LEFT COLUMN: Playback ──────────────────────────────────────────
    {
        auto [titleX, prevX, valX, nextX, toggleX] = colLayout(leftCX);

        // Section header
        auto header = CCLabelBMFont::create("Playback", "bigFont.fnt");
        header->setScale(0.35f);
        header->setColor({255, 220, 100});
        header->setPosition({leftCX, topY});
        m_mainLayer->addChild(header, 1);

        float fpsY     = topY - rowSpacing;
        float qualityY = fpsY - rowSpacing;
        float audioY   = qualityY - rowSpacing;

        // FPS
        auto fpsTitle = CCLabelBMFont::create("FPS:", "bigFont.fnt");
        fpsTitle->setScale(0.30f);
        fpsTitle->setPosition({titleX, fpsY});
        m_mainLayer->addChild(fpsTitle, 1);
        makeArrowL(prevX, fpsY, menu_selector(VideoSettingsPopup::onFpsPrev));
        m_fpsLabel = CCLabelBMFont::create("30", "bigFont.fnt");
        m_fpsLabel->setScale(0.38f);
        m_fpsLabel->setColor({100, 255, 100});
        m_fpsLabel->setPosition({valX, fpsY});
        m_mainLayer->addChild(m_fpsLabel, 1);
        makeArrowR(nextX, fpsY, menu_selector(VideoSettingsPopup::onFpsNext));
        updateFpsLabel();

        // Quality
        auto qualityTitle = CCLabelBMFont::create("Quality:", "bigFont.fnt");
        qualityTitle->setScale(0.30f);
        qualityTitle->setPosition({titleX, qualityY});
        m_mainLayer->addChild(qualityTitle, 1);
        makeArrowL(prevX, qualityY, menu_selector(VideoSettingsPopup::onQualityPrev));
        m_qualityLabel = CCLabelBMFont::create("Auto", "bigFont.fnt");
        m_qualityLabel->setScale(0.38f);
        m_qualityLabel->setColor({100, 200, 255});
        m_qualityLabel->setPosition({valX, qualityY});
        m_mainLayer->addChild(m_qualityLabel, 1);
        makeArrowR(nextX, qualityY, menu_selector(VideoSettingsPopup::onQualityNext));
        updateQualityLabel();

        // Audio toggle
        auto audioTitle = CCLabelBMFont::create("Audio:", "bigFont.fnt");
        audioTitle->setScale(0.30f);
        audioTitle->setPosition({titleX, audioY});
        m_mainLayer->addChild(audioTitle, 1);
        m_audioToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(VideoSettingsPopup::onAudioToggle), 0.55f);
        m_audioToggle->setPosition({toggleX, audioY});
        m_audioToggle->toggle(paimon::settings::video::audioEnabled());
        menu->addChild(m_audioToggle);
    }

    // ── RIGHT COLUMN: Filters ──────────────────────────────────────────
    {
        auto [titleX, prevX, valX, nextX, toggleX] = colLayout(rightCX);

        // Section header
        auto header = CCLabelBMFont::create("Filters", "bigFont.fnt");
        header->setScale(0.35f);
        header->setColor({255, 160, 255});
        header->setPosition({rightCX, topY});
        m_mainLayer->addChild(header, 1);

        float blurTypeY = topY - rowSpacing;
        float blurIntY  = blurTypeY - rowSpacing;

        // Blur Type
        auto blurTitle = CCLabelBMFont::create("Blur:", "bigFont.fnt");
        blurTitle->setScale(0.30f);
        blurTitle->setPosition({titleX, blurTypeY});
        m_mainLayer->addChild(blurTitle, 1);
        makeArrowL(prevX, blurTypeY, menu_selector(VideoSettingsPopup::onBlurTypePrev));
        m_blurTypeLabel = CCLabelBMFont::create("None", "bigFont.fnt");
        m_blurTypeLabel->setScale(0.35f);
        m_blurTypeLabel->setColor({180, 180, 180});
        m_blurTypeLabel->setPosition({valX, blurTypeY});
        m_mainLayer->addChild(m_blurTypeLabel, 1);
        makeArrowR(nextX, blurTypeY, menu_selector(VideoSettingsPopup::onBlurTypeNext));
        updateBlurTypeLabel();

        // Blur Intensity (hidden when blur = "none")
        m_blurIntensityTitleLabel = CCLabelBMFont::create("Intensity:", "bigFont.fnt");
        m_blurIntensityTitleLabel->setScale(0.30f);
        m_blurIntensityTitleLabel->setPosition({titleX, blurIntY});
        m_mainLayer->addChild(m_blurIntensityTitleLabel, 1);

        m_blurIntensityMenu = CCMenu::create();
        m_blurIntensityMenu->setPosition({0, 0});
        m_mainLayer->addChild(m_blurIntensityMenu, 10);

        {
            auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
            spr->setFlipX(true);
            spr->setScale(0.35f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(VideoSettingsPopup::onBlurIntensityPrev));
            btn->setPosition({prevX, blurIntY});
            m_blurIntensityMenu->addChild(btn);
        }
        m_blurIntensityLabel = CCLabelBMFont::create("50%", "bigFont.fnt");
        m_blurIntensityLabel->setScale(0.38f);
        m_blurIntensityLabel->setColor({255, 200, 100});
        m_blurIntensityLabel->setPosition({valX, blurIntY});
        m_mainLayer->addChild(m_blurIntensityLabel, 1);
        {
            auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
            spr->setScale(0.35f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(VideoSettingsPopup::onBlurIntensityNext));
            btn->setPosition({nextX, blurIntY});
            m_blurIntensityMenu->addChild(btn);
        }
        updateBlurIntensityLabel();
        updateBlurIntensityVisibility();
    }

    // ── BOTTOM ROW: Cache ──────────────────────────────────────────────
    float cacheY = topY - rowSpacing * 3.f - 14.f;

    auto ramBtnSpr = ButtonSprite::create("Clear RAM", 0.32f);
    auto ramBtn = CCMenuItemSpriteExtra::create(ramBtnSpr, this, menu_selector(VideoSettingsPopup::onClearRAM));
    ramBtn->setPosition({cx - 85.f, cacheY});
    menu->addChild(ramBtn);

    m_ramLabel = CCLabelBMFont::create("0 MB", "chatFont.fnt");
    m_ramLabel->setScale(0.48f);
    m_ramLabel->setColor({100, 255, 100});
    m_ramLabel->setPosition({cx - 15.f, cacheY});
    m_mainLayer->addChild(m_ramLabel, 1);
    updateRAMLabel();

    auto cacheBtnSpr = ButtonSprite::create("Clear Cache", 0.32f);
    auto cacheBtn = CCMenuItemSpriteExtra::create(cacheBtnSpr, this, menu_selector(VideoSettingsPopup::onClearDiskCache));
    cacheBtn->setPosition({cx + 75.f, cacheY});
    menu->addChild(cacheBtn);

    // ── Info label ─────────────────────────────────────────────────────
    auto infoLabel = CCLabelBMFont::create(
        "Lower FPS = less CPU usage. Changes apply instantly.",
        "chatFont.fnt");
    infoLabel->setScale(0.48f);
    infoLabel->setColor({140, 140, 140});
    infoLabel->setAlignment(kCCTextAlignmentCenter);
    infoLabel->setPosition({cx, cacheY - 20.f});
    m_mainLayer->addChild(infoLabel, 1);

    paimon::markDynamicPopup(this);
    return true;
}

// ── FPS ──────────────────────────────────────────────────────────────────

void VideoSettingsPopup::onFpsPrev(CCObject*) {
    if (--m_fpsIndex < 0) m_fpsIndex = (int)FPS_OPTIONS.size() - 1;
    int newFPS = FPS_OPTIONS[m_fpsIndex];
    Mod::get()->setSavedValue("video-fps-limit", newFPS);
    paimon::requestDeferredModSave();
    LayerBackgroundManager::get().broadcastFPSUpdate(newFPS);
    updateFpsLabel();
}

void VideoSettingsPopup::onFpsNext(CCObject*) {
    if (++m_fpsIndex >= (int)FPS_OPTIONS.size()) m_fpsIndex = 0;
    int newFPS = FPS_OPTIONS[m_fpsIndex];
    Mod::get()->setSavedValue("video-fps-limit", newFPS);
    paimon::requestDeferredModSave();
    LayerBackgroundManager::get().broadcastFPSUpdate(newFPS);
    updateFpsLabel();
}

void VideoSettingsPopup::updateFpsLabel() {
    if (!m_fpsLabel) return;
    m_fpsLabel->setString(fmt::format("{}", FPS_OPTIONS[m_fpsIndex]).c_str());
}

// ── Quality ───────────────────────────────────────────────────────────────

void VideoSettingsPopup::onQualityPrev(CCObject*) {
    if (--m_qualityIndex < 0) m_qualityIndex = (int)QUALITY_OPTIONS.size() - 1;
    Mod::get()->setSavedValue("video-quality", QUALITY_OPTIONS[m_qualityIndex]);
    paimon::requestDeferredModSave();
    updateQualityLabel();
}

void VideoSettingsPopup::onQualityNext(CCObject*) {
    if (++m_qualityIndex >= (int)QUALITY_OPTIONS.size()) m_qualityIndex = 0;
    Mod::get()->setSavedValue("video-quality", QUALITY_OPTIONS[m_qualityIndex]);
    paimon::requestDeferredModSave();
    updateQualityLabel();
}

void VideoSettingsPopup::updateQualityLabel() {
    if (!m_qualityLabel) return;
    m_qualityLabel->setString(QUALITY_NAMES[m_qualityIndex]);
    switch (m_qualityIndex) {
        case 0: m_qualityLabel->setColor({100, 200, 255}); break;
        case 1: m_qualityLabel->setColor({255, 100, 100}); break;
        case 2: m_qualityLabel->setColor({255, 220, 100}); break;
        case 3: m_qualityLabel->setColor({100, 255, 100}); break;
    }
}

// ── Audio ─────────────────────────────────────────────────────────────────

void VideoSettingsPopup::onAudioToggle(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    bool enabled = !toggle->isToggled();
    Mod::get()->setSavedValue("video-audio-enabled", enabled);
    paimon::requestDeferredModSave();
}

// ── Blur Type ─────────────────────────────────────────────────────────────

void VideoSettingsPopup::onBlurTypePrev(CCObject*) {
    if (--m_blurTypeIndex < 0) m_blurTypeIndex = (int)BLUR_TYPE_OPTIONS.size() - 1;
    Mod::get()->setSavedValue("video-blur-type", BLUR_TYPE_OPTIONS[m_blurTypeIndex]);
    paimon::requestDeferredModSave();
    updateBlurTypeLabel();
    updateBlurIntensityVisibility();
}

void VideoSettingsPopup::onBlurTypeNext(CCObject*) {
    if (++m_blurTypeIndex >= (int)BLUR_TYPE_OPTIONS.size()) m_blurTypeIndex = 0;
    Mod::get()->setSavedValue("video-blur-type", BLUR_TYPE_OPTIONS[m_blurTypeIndex]);
    paimon::requestDeferredModSave();
    updateBlurTypeLabel();
    updateBlurIntensityVisibility();
}

void VideoSettingsPopup::updateBlurTypeLabel() {
    if (!m_blurTypeLabel) return;
    m_blurTypeLabel->setString(BLUR_TYPE_NAMES[m_blurTypeIndex]);
    switch (m_blurTypeIndex) {
        case 0: m_blurTypeLabel->setColor({180, 180, 180}); break;
        case 1: m_blurTypeLabel->setColor({100, 200, 255}); break;
        case 2: m_blurTypeLabel->setColor({255, 160, 255}); break;
    }
}

void VideoSettingsPopup::updateBlurIntensityVisibility() {
    bool show = (m_blurTypeIndex != 0);
    if (m_blurIntensityMenu)       m_blurIntensityMenu->setVisible(show);
    if (m_blurIntensityLabel)      m_blurIntensityLabel->setVisible(show);
    if (m_blurIntensityTitleLabel) m_blurIntensityTitleLabel->setVisible(show);
}

// ── Blur Intensity ────────────────────────────────────────────────────────

void VideoSettingsPopup::onBlurIntensityPrev(CCObject*) {
    if (--m_blurIntensityIndex < 0) m_blurIntensityIndex = (int)BLUR_INTENSITY_OPTIONS.size() - 1;
    Mod::get()->setSavedValue("video-blur-intensity", BLUR_INTENSITY_OPTIONS[m_blurIntensityIndex]);
    paimon::requestDeferredModSave();
    updateBlurIntensityLabel();
}

void VideoSettingsPopup::onBlurIntensityNext(CCObject*) {
    if (++m_blurIntensityIndex >= (int)BLUR_INTENSITY_OPTIONS.size()) m_blurIntensityIndex = 0;
    Mod::get()->setSavedValue("video-blur-intensity", BLUR_INTENSITY_OPTIONS[m_blurIntensityIndex]);
    paimon::requestDeferredModSave();
    updateBlurIntensityLabel();
}

void VideoSettingsPopup::updateBlurIntensityLabel() {
    if (!m_blurIntensityLabel) return;
    m_blurIntensityLabel->setString(BLUR_INTENSITY_NAMES[m_blurIntensityIndex]);
}

// ── Clear RAM ────────────────────────────────────────────────────────────

void VideoSettingsPopup::onClearRAM(CCObject*) {
    LayerBackgroundManager::get().releaseAllSharedVideos();
    updateRAMLabel();
    FLAlertLayer::create("RAM Cleared", "All video decode buffers have been released.", "OK")->show();
}

void VideoSettingsPopup::updateRAMLabel() {
    if (!m_ramLabel) return;
    size_t totalBytes = LayerBackgroundManager::get().getTotalVideoRAMBytes();
    float mb = static_cast<float>(totalBytes) / (1024.0f * 1024.0f);
    if (mb < 0.1f) {
        m_ramLabel->setString("0 MB");
        m_ramLabel->setColor({100, 255, 100});
    } else {
        m_ramLabel->setString(fmt::format("{:.0f} MB", mb).c_str());
        m_ramLabel->setColor(mb > 200.0f ? ccColor3B{255, 100, 100} : ccColor3B{100, 255, 100});
    }
}

// ── Clear Disk Cache ────────────────────────────────────────────────────

void VideoSettingsPopup::onClearDiskCache(CCObject*) {
    int removed = paimon::video::VideoDiskCache::deleteAllCaches();
    FLAlertLayer::create("Cache Cleared",
        fmt::format("Removed {} cached file(s).\nNext playback will rebuild the cache.", removed).c_str(),
        "OK")->show();
}