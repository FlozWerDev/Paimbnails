#include "ThumbnailSettingsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../visuals/ui/ExtraEffectsPopup.hpp"
#include "LocalThumbnailViewPopup.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// ── Handler independiente que vive en la escena ──
// Su CCMenu y su boton NUNCA se hacen invisibles,
// asi que siempre recibe toques.
class PeekButtonHandler : public CCNode {
public:
    ThumbnailSettingsPopup* m_popup = nullptr;

    static PeekButtonHandler* create(ThumbnailSettingsPopup* popup) {
        auto ret = new PeekButtonHandler();
        if (ret && ret->init()) {
            ret->m_popup = popup;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onPeekToggle(CCObject*) {
        if (m_popup) {
            m_popup->togglePeek();
        }
    }
};

bool ThumbnailSettingsPopup::init() {
    if (!Popup::init(260.f, 280.f)) return false;

    this->setTitle("Thumbnail Settings");

    auto content = m_mainLayer->getContentSize();

    m_styles = {
        "normal", "pixel", "blur", "paimonblur", "grayscale", "sepia",
        "vignette", "scanlines", "bloom", "chromatic",
        "radial-blur", "glitch", "posterize",
        "rain", "matrix", "neon-pulse", "wave-distortion", "crt",
        "shockwave", "vortex", "magnetic", "spotlight",
        "ripple", "plasma-cursor", "freeze", "pixelate-cursor",
        "kaleidoscope", "sonar", "electric-arc", "prism-split",
        "gravity-well", "shatter", "heat-haze", "liquify",
        "ink-spread", "hologram", "time-warp", "underwater", "neon-trail",
        "synthwave", "neon-city", "ocean", "galaxy"
    };
    m_allStyles = m_styles;

    m_popupTransitions = {
        "crossfade", "slide-left", "slide-right",
        "elastic-slide", "directional-elastic",
        "zoom-in", "zoom-out", "bounce",
        "flip-horizontal", "flip-vertical", "dissolve",
        "wave-slide", "card-flip", "spin-zoom"
    };

    m_bgTransitions = {
        "crossfade", "slide-left", "slide-right",
        "elastic-slide", "directional-elastic",
        "zoom-in", "zoom-out", "bounce",
        "flip-horizontal", "flip-vertical", "dissolve",
        "wave-slide", "card-flip", "spin-zoom"
    };

    // leer settings (Geode 5 getSettingValue is exception-safe)
    // Usar savedValue override si existe (mas robusto que setSettingValue con one-of)
    m_currentStyle = Mod::get()->getSavedValue<std::string>("levelinfo-background-style-override", "");
    if (m_currentStyle.empty()) {
        m_currentStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
    }
    m_currentIntensity = static_cast<int>(Mod::get()->getSavedValue<int>("levelinfo-effect-intensity", 4));
    m_currentDarkness = static_cast<int>(Mod::get()->getSavedValue<int>("levelinfo-bg-darkness", 27));
    m_dynamicSong = Mod::get()->getSettingValue<bool>("dynamic-song");
    m_dynamicShaders = Mod::get()->getSavedValue<bool>("levelinfo-dynamic-shaders", false);
    m_dynamicShadersDelay = Mod::get()->getSavedValue<float>("levelinfo-dynamic-shaders-delay", 0.0f);

    // Filtrar estilos si dynamic shaders esta activo (sin guardar — solo filtrar la lista)
    if (m_dynamicShaders) {
        static const std::vector<std::string> kDynamicStyles = {
            "chromatic", "radial-blur", "glitch",
            "rain", "matrix", "neon-pulse", "wave-distortion", "crt",
            "shockwave", "vortex", "magnetic", "spotlight",
            "ripple", "plasma-cursor", "freeze", "pixelate-cursor",
            "kaleidoscope", "sonar", "electric-arc", "prism-split",
            "gravity-well", "shatter", "heat-haze", "liquify",
            "ink-spread", "hologram", "time-warp", "underwater", "neon-trail"
        };
        m_styles = kDynamicStyles;
    }

    m_currentPopupTransition = Mod::get()->getSavedValue<std::string>("popup-gallery-transition", "directional-elastic");
    float popupTransDur = Mod::get()->getSavedValue<float>("popup-gallery-transition-duration", 0.45f);
    m_currentBgTransition = Mod::get()->getSavedValue<std::string>("levelinfo-bg-transition", "crossfade");
    float bgTransDur = Mod::get()->getSavedValue<float>("levelinfo-bg-transition-duration", 0.5f);

    // Buscar el estilo actual en la lista (filtrada o completa)
    m_styleIndex = 0;
    bool styleFound = false;
    for (int i = 0; i < (int)m_styles.size(); i++) {
        if (m_styles[i] == m_currentStyle) { m_styleIndex = i; styleFound = true; break; }
    }
    // Si el estilo guardado no esta en la lista actual, usar el primero sin guardar
    if (!styleFound && !m_styles.empty()) {
        m_styleIndex = 0;
        m_currentStyle = m_styles[0];
        // NO llamar saveSettings aqui — solo ajustar el display
    }

    for (int i = 0; i < (int)m_popupTransitions.size(); i++) {
        if (m_popupTransitions[i] == m_currentPopupTransition) { m_popupTransitionIndex = i; break; }
    }
    for (int i = 0; i < (int)m_bgTransitions.size(); i++) {
        if (m_bgTransitions[i] == m_currentBgTransition) { m_bgTransitionIndex = i; break; }
    }

    m_popupStyles = {
        "paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop",
        "elastic", "bounce", "slide-up", "slide-down", "slide-left",
        "slide-right", "zoom-fade", "flip", "fold", "pop-rotate",
        "elastic-drop", "glitch-shake", "card-turn", "fly-spin"
    };
    m_dynamicPopup = Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
    m_currentPopupStyle = Mod::get()->getSavedValue<std::string>("dynamic-popup-style", "paimonUI");
    m_currentPopupSpeed = Mod::get()->getSavedValue<double>("dynamic-popup-speed", 1.0);
    m_dynamicExit = Mod::get()->getSettingValue<bool>("dynamic-exit-enabled");
    m_currentExitSpeed = Mod::get()->getSavedValue<double>("dynamic-exit-speed", 1.0);

    m_popupStyleIndex = 0;
    for (int i = 0; i < (int)m_popupStyles.size(); i++) {
        if (m_popupStyles[i] == m_currentPopupStyle) { m_popupStyleIndex = i; break; }
    }

    float cx = content.width / 2.f;

    // ── Scrollable content ──
    float scrollH = content.height - 50.f;
    float totalContentH = 1000.f; // enough for all controls including dynamic exit

    auto scroll = geode::ScrollLayer::create({content.width - 20.f, scrollH});
    scroll->setPosition({10.f, 8.f});
    scroll->m_contentLayer->setContentSize({content.width - 20.f, totalContentH});
    m_mainLayer->addChild(scroll);

    // All content goes into a container node inside the scroll layer
    auto scrollContent = CCLayer::create();
    scrollContent->setContentSize({content.width - 20.f, totalContentH});
    scroll->m_contentLayer->addChild(scrollContent);

    // y starts from top of scrollContent, going down
    float y = totalContentH - 10.f;
    float scx = (content.width - 20.f) / 2.f;

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    scrollContent->addChild(navMenu, 10);

    // ── Background Style ──
    auto styleTitle = CCLabelBMFont::create("Background Style", "goldFont.fnt");
    styleTitle->setScale(0.5f);
    styleTitle->setPosition({scx, y});
    scrollContent->addChild(styleTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Background Style",
            "Visual filter applied to the level info background.\n"
            "<cy>Normal</c>: no effect.\n"
            "<cy>Blur</c>: soft gaussian blur.\n"
            "<cy>Pixel</c>: retro pixelated look.\n"
            "<cy>Neon Pulse</c>: glowing neon animation.\n"
            "<cy>CRT</c>: old TV screen effect.\n"
            "...and many more! Cycle with the arrows.", this, 0.56f);
        if (iBtn) {
            iBtn->setPosition({scx + 85.f, y});
            navMenu->addChild(iBtn);
        }
    }

    y -= 24.f;

    auto lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    lSpr->setScale(0.5f);
    auto lBtn = CCMenuItemSpriteExtra::create(lSpr, this, menu_selector(ThumbnailSettingsPopup::onStylePrev));
    lBtn->setID("style-prev-btn"_spr);
    lBtn->setPosition({scx - 80.f, y});
    navMenu->addChild(lBtn);

    auto rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    rSpr->setFlipX(true);
    rSpr->setScale(0.5f);
    auto rBtn = CCMenuItemSpriteExtra::create(rSpr, this, menu_selector(ThumbnailSettingsPopup::onStyleNext));
    rBtn->setID("style-next-btn"_spr);
    rBtn->setPosition({scx + 80.f, y});
    navMenu->addChild(rBtn);

    m_styleValueLabel = CCLabelBMFont::create(getStyleDisplayName(m_currentStyle).c_str(), "bigFont.fnt");
    m_styleValueLabel->setScale(0.4f);
    m_styleValueLabel->setPosition({scx, y});
    scrollContent->addChild(m_styleValueLabel);

    // ── Extra Effects gear button ──
    y -= 30.f;

    auto gearSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (!gearSpr) gearSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
    if (gearSpr) {
        gearSpr->setScale(0.4f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(ThumbnailSettingsPopup::onOpenExtraEffects));
        gearBtn->setID("extra-effects-btn"_spr);
        gearBtn->setPosition({scx + 80.f, y});
        navMenu->addChild(gearBtn);
    }

    auto extraLabel = CCLabelBMFont::create("Extra Effects", "bigFont.fnt");
    extraLabel->setScale(0.4f);
    extraLabel->setPosition({scx - 10.f, y});
    scrollContent->addChild(extraLabel);

    // ── Effect Intensity ──
    y -= 30.f;

    auto intTitle = CCLabelBMFont::create("Effect Intensity", "goldFont.fnt");
    intTitle->setScale(0.45f);
    intTitle->setPosition({scx, y});
    scrollContent->addChild(intTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Effect Intensity",
            "Controls how strong the background effect is.\n"
            "<cy>1</c> = very subtle.\n"
            "<cy>10</c> = maximum intensity.\n"
            "Affects blur radius, pixel size, glow strength, etc.", this, 0.48f);
        if (iBtn) {
            iBtn->setPosition({scx + 75.f, y});
            navMenu->addChild(iBtn);
        }
    }

    y -= 22.f;

    m_intensitySlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onIntensityChanged), 0.7f);
    m_intensitySlider->setPosition({scx, y});
    m_intensitySlider->setValue((m_currentIntensity - 1) / 9.0f);
    scrollContent->addChild(m_intensitySlider);

    m_intensityLabel = CCLabelBMFont::create(fmt::format("{}", m_currentIntensity).c_str(), "bigFont.fnt");
    m_intensityLabel->setScale(0.35f);
    m_intensityLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_intensityLabel);

    // ── Background Darkness ──
    y -= 30.f;

    auto darkTitle = CCLabelBMFont::create("Background Darkness", "goldFont.fnt");
    darkTitle->setScale(0.45f);
    darkTitle->setPosition({scx, y});
    scrollContent->addChild(darkTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Background Darkness",
            "Adds a dark overlay on top of the background image.\n"
            "<cy>0</c> = no darkening (bright).\n"
            "<cy>50</c> = very dark.\n"
            "Helps improve text readability over busy thumbnails.", this, 0.48f);
        if (iBtn) {
            iBtn->setPosition({scx + 95.f, y});
            navMenu->addChild(iBtn);
        }
    }

    y -= 22.f;

    m_darknessSlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onDarknessChanged), 0.7f);
    m_darknessSlider->setPosition({scx, y});
    m_darknessSlider->setValue(m_currentDarkness / 50.0f);
    scrollContent->addChild(m_darknessSlider);

    m_darknessLabel = CCLabelBMFont::create(fmt::format("{}", m_currentDarkness).c_str(), "bigFont.fnt");
    m_darknessLabel->setScale(0.35f);
    m_darknessLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_darknessLabel);

    // ── Background Transition ──
    y -= 32.f;

    auto bgTransTitle = CCLabelBMFont::create("Background Transition", "goldFont.fnt");
    bgTransTitle->setScale(0.45f);
    bgTransTitle->setPosition({scx, y});
    scrollContent->addChild(bgTransTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Background Transition",
            "Transition style when the level info background\nthumbnail changes (auto-cycle or manual).\n"
            "<cy>Directional Elastic</c>: slow start, then fast snap\nin the direction you navigate.\n"
            "<cy>Crossfade</c>: smooth opacity blend.", this, 0.48f);
        if (iBtn) {
            iBtn->setPosition({scx + 85.f, y});
            navMenu->addChild(iBtn);
        }
    }

    y -= 22.f;

    auto bglSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    bglSpr->setScale(0.5f);
    auto bglBtn = CCMenuItemSpriteExtra::create(bglSpr, this, menu_selector(ThumbnailSettingsPopup::onBgTransitionPrev));
    bglBtn->setPosition({scx - 80.f, y});
    navMenu->addChild(bglBtn);

    auto bgrSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    bgrSpr->setFlipX(true);
    bgrSpr->setScale(0.5f);
    auto bgrBtn = CCMenuItemSpriteExtra::create(bgrSpr, this, menu_selector(ThumbnailSettingsPopup::onBgTransitionNext));
    bgrBtn->setPosition({scx + 80.f, y});
    navMenu->addChild(bgrBtn);

    m_bgTransitionLabel = CCLabelBMFont::create(getBgTransitionDisplayName(m_currentBgTransition).c_str(), "bigFont.fnt");
    m_bgTransitionLabel->setScale(0.35f);
    m_bgTransitionLabel->setPosition({scx, y});
    scrollContent->addChild(m_bgTransitionLabel);

    // ── Background Transition Duration ──
    y -= 28.f;

    auto bgDurTitle = CCLabelBMFont::create("BG Transition Speed", "goldFont.fnt");
    bgDurTitle->setScale(0.4f);
    bgDurTitle->setPosition({scx, y});
    scrollContent->addChild(bgDurTitle);

    y -= 20.f;

    m_bgTransitionDurationSlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onBgTransitionDurationChanged), 0.7f);
    m_bgTransitionDurationSlider->setPosition({scx, y});
    m_bgTransitionDurationSlider->setValue((bgTransDur - 0.15f) / (1.5f - 0.15f));
    scrollContent->addChild(m_bgTransitionDurationSlider);

    m_bgTransitionDurationLabel = CCLabelBMFont::create(fmt::format("{:.2f}s", bgTransDur).c_str(), "bigFont.fnt");
    m_bgTransitionDurationLabel->setScale(0.3f);
    m_bgTransitionDurationLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_bgTransitionDurationLabel);

    // ── Popup Gallery Transition ──
    y -= 32.f;

    auto transTitle = CCLabelBMFont::create("Gallery Transition", "goldFont.fnt");
    transTitle->setScale(0.45f);
    transTitle->setPosition({scx, y});
    scrollContent->addChild(transTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Gallery Transition",
            "Transition style when navigating between thumbnails\nin the popup gallery view.\n"
            "<cy>Directional Elastic</c>: slow start, then fast snap\nin the direction you navigate.\n"
            "<cy>Elastic Slide</c>: elastic snap always from right.\n"
            "<cy>Crossfade</c>: smooth opacity blend.", this, 0.48f);
        if (iBtn) {
            iBtn->setPosition({scx + 85.f, y});
            navMenu->addChild(iBtn);
        }
    }

    y -= 22.f;

    auto tlSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    tlSpr->setScale(0.5f);
    auto tlBtn = CCMenuItemSpriteExtra::create(tlSpr, this, menu_selector(ThumbnailSettingsPopup::onPopupTransitionPrev));
    tlBtn->setPosition({scx - 80.f, y});
    navMenu->addChild(tlBtn);

    auto trSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    trSpr->setFlipX(true);
    trSpr->setScale(0.5f);
    auto trBtn = CCMenuItemSpriteExtra::create(trSpr, this, menu_selector(ThumbnailSettingsPopup::onPopupTransitionNext));
    trBtn->setPosition({scx + 80.f, y});
    navMenu->addChild(trBtn);

    m_popupTransitionLabel = CCLabelBMFont::create(getPopupTransitionDisplayName(m_currentPopupTransition).c_str(), "bigFont.fnt");
    m_popupTransitionLabel->setScale(0.35f);
    m_popupTransitionLabel->setPosition({scx, y});
    scrollContent->addChild(m_popupTransitionLabel);

    // ── Popup Transition Duration ──
    y -= 28.f;

    auto durTitle = CCLabelBMFont::create("Gallery Speed", "goldFont.fnt");
    durTitle->setScale(0.4f);
    durTitle->setPosition({scx, y});
    scrollContent->addChild(durTitle);

    y -= 20.f;

    m_popupTransitionDurationSlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onPopupTransitionDurationChanged), 0.7f);
    m_popupTransitionDurationSlider->setPosition({scx, y});
    m_popupTransitionDurationSlider->setValue((popupTransDur - 0.15f) / (1.5f - 0.15f));
    scrollContent->addChild(m_popupTransitionDurationSlider);

    m_popupTransitionDurationLabel = CCLabelBMFont::create(fmt::format("{:.2f}s", popupTransDur).c_str(), "bigFont.fnt");
    m_popupTransitionDurationLabel->setScale(0.3f);
    m_popupTransitionDurationLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_popupTransitionDurationLabel);

    // ── Toggles ──
    y -= 32.f;

    auto toggleMenu = CCMenu::create();
    toggleMenu->setPosition({0, 0});
    scrollContent->addChild(toggleMenu, 10);

    auto dynSongLabel = CCLabelBMFont::create("Dynamic Song", "bigFont.fnt");
    dynSongLabel->setScale(0.35f);
    dynSongLabel->setAnchorPoint({0.f, 0.5f});
    dynSongLabel->setPosition({scx - 90.f, y});
    scrollContent->addChild(dynSongLabel);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Dynamic Song",
            "When <cg>ON</c>, the level's song preview plays automatically\nwhen you open the level info page.\n"
            "Creates an immersive experience with the thumbnail background.", this, 0.45f);
        if (iBtn) {
            float lblW = dynSongLabel->getContentSize().width * 0.35f;
            iBtn->setPosition({scx - 90.f + lblW + 8.f, y});
            toggleMenu->addChild(iBtn);
        }
    }

    m_dynamicSongToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ThumbnailSettingsPopup::onDynamicSongToggled), 0.6f);
    m_dynamicSongToggle->setPosition({scx + 90.f, y});
    m_dynamicSongToggle->toggle(m_dynamicSong);
    toggleMenu->addChild(m_dynamicSongToggle);

    // ── Dynamic Shaders toggle ──
    y -= 28.f;

    auto dynShadersLabel = CCLabelBMFont::create("Dynamic Shaders", "bigFont.fnt");
    dynShadersLabel->setScale(0.35f);
    dynShadersLabel->setAnchorPoint({0.f, 0.5f});
    dynShadersLabel->setPosition({scx - 90.f, y});
    scrollContent->addChild(dynShadersLabel);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Dynamic Shaders",
            "When <cg>ON</c>, animated shaders (Chromatic, Radial Blur,\n"
            "Glitch, Rain, Matrix, Neon Pulse, Wave, CRT) follow\n"
            "your <cy>cursor</c> on PC or your <cy>last tap</c> on mobile.\n"
            "Only the first touch is tracked (multitouch protected).", this, 0.45f);
        if (iBtn) {
            float lblW = dynShadersLabel->getContentSize().width * 0.35f;
            iBtn->setPosition({scx - 90.f + lblW + 8.f, y});
            toggleMenu->addChild(iBtn);
        }
    }

    m_dynamicShadersToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ThumbnailSettingsPopup::onDynamicShadersToggled), 0.6f);
    m_dynamicShadersToggle->setPosition({scx + 90.f, y});
    m_dynamicShadersToggle->toggle(m_dynamicShaders);
    toggleMenu->addChild(m_dynamicShadersToggle);

    // ── Dynamic Shaders Delay ──
    y -= 26.f;

    auto delayTitle = CCLabelBMFont::create("Shader Delay", "goldFont.fnt");
    delayTitle->setScale(0.4f);
    delayTitle->setPosition({scx, y});
    scrollContent->addChild(delayTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Shader Delay",
            "Smoothing delay for the dynamic shader cursor tracking.\n"
            "<cy>0.00s</c> = instant (follows cursor exactly).\n"
            "<cy>2.00s</c> = very smooth/slow follow.\n"
            "Higher values create a more fluid, cinematic effect.", this, 0.4f);
        if (iBtn) {
            iBtn->setPosition({scx + 65.f, y});
            toggleMenu->addChild(iBtn);
        }
    }

    y -= 20.f;

    m_dynamicShadersDelaySlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onDynamicShadersDelayChanged), 0.7f);
    m_dynamicShadersDelaySlider->setPosition({scx, y});
    m_dynamicShadersDelaySlider->setValue(m_dynamicShadersDelay / 2.0f);
    scrollContent->addChild(m_dynamicShadersDelaySlider);

    m_dynamicShadersDelayLabel = CCLabelBMFont::create(fmt::format("{:.2f}s", m_dynamicShadersDelay).c_str(), "bigFont.fnt");
    m_dynamicShadersDelayLabel->setScale(0.3f);
    m_dynamicShadersDelayLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_dynamicShadersDelayLabel);

    // ── Popup Animations Settings ──
    y -= 35.f;

    auto popupTitle = CCLabelBMFont::create("Popup Animations", "goldFont.fnt");
    popupTitle->setScale(0.5f);
    popupTitle->setPosition({scx, y});
    scrollContent->addChild(popupTitle);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Popup Animations",
            "Configure beautiful, fluid entry and exit transitions\n"
            "for Paimbnails popups.\n"
            "<cy>Dynamic Popups</c>: toggles custom animations.\n"
            "<cy>Animation Style</c>: choose from 15 custom designs.\n"
            "<cy>Animation Speed</c>: adjust the animation rate.", this, 0.45f);
        if (iBtn) {
            iBtn->setPosition({scx + 85.f, y});
            navMenu->addChild(iBtn);
        }
    }

    y -= 28.f;

    auto dynPopupLabel = CCLabelBMFont::create("Dynamic Popups", "bigFont.fnt");
    dynPopupLabel->setScale(0.35f);
    dynPopupLabel->setAnchorPoint({0.f, 0.5f});
    dynPopupLabel->setPosition({scx - 90.f, y});
    scrollContent->addChild(dynPopupLabel);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Dynamic Popups",
            "When <cg>ON</c>, custom premium entry and exit\n"
            "animations are applied to all popups of the mod.", this, 0.45f);
        if (iBtn) {
            float lblW = dynPopupLabel->getContentSize().width * 0.35f;
            iBtn->setPosition({scx - 90.f + lblW + 8.f, y});
            toggleMenu->addChild(iBtn);
        }
    }

    m_dynamicPopupToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ThumbnailSettingsPopup::onDynamicPopupToggled), 0.6f);
    m_dynamicPopupToggle->setPosition({scx + 90.f, y});
    m_dynamicPopupToggle->toggle(m_dynamicPopup);
    toggleMenu->addChild(m_dynamicPopupToggle);

    // ── Popup Animation Style ──
    y -= 28.f;

    auto popupStyleTitle = CCLabelBMFont::create("Animation Style", "goldFont.fnt");
    popupStyleTitle->setScale(0.4f);
    popupStyleTitle->setPosition({scx, y});
    scrollContent->addChild(popupStyleTitle);

    y -= 20.f;

    auto stylSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    stylSpr->setScale(0.5f);
    auto stylBtn = CCMenuItemSpriteExtra::create(stylSpr, this, menu_selector(ThumbnailSettingsPopup::onPopupStylePrev));
    stylBtn->setPosition({scx - 80.f, y});
    navMenu->addChild(stylBtn);

    auto styrSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    styrSpr->setFlipX(true);
    styrSpr->setScale(0.5f);
    auto styrBtn = CCMenuItemSpriteExtra::create(styrSpr, this, menu_selector(ThumbnailSettingsPopup::onPopupStyleNext));
    styrBtn->setPosition({scx + 80.f, y});
    navMenu->addChild(styrBtn);

    m_popupStyleValueLabel = CCLabelBMFont::create(getPopupStyleDisplayName(m_currentPopupStyle).c_str(), "bigFont.fnt");
    m_popupStyleValueLabel->setScale(0.35f);
    m_popupStyleValueLabel->setPosition({scx, y});
    scrollContent->addChild(m_popupStyleValueLabel);

    // ── Popup Animation Speed ──
    y -= 30.f;

    auto speedTitle = CCLabelBMFont::create("Animation Speed", "goldFont.fnt");
    speedTitle->setScale(0.4f);
    speedTitle->setPosition({scx, y});
    scrollContent->addChild(speedTitle);

    y -= 20.f;

    m_popupSpeedSlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onPopupSpeedChanged), 0.7f);
    m_popupSpeedSlider->setPosition({scx, y});
    m_popupSpeedSlider->setValue((m_currentPopupSpeed - 0.2) / 2.8);
    scrollContent->addChild(m_popupSpeedSlider);

    m_popupSpeedLabel = CCLabelBMFont::create(fmt::format("{:.2f}x", m_currentPopupSpeed).c_str(), "bigFont.fnt");
    m_popupSpeedLabel->setScale(0.3f);
    m_popupSpeedLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_popupSpeedLabel);

    // ── Popup Animation Dynamic Exit ──
    y -= 28.f;

    auto dynExitLabel = CCLabelBMFont::create("Dynamic Exit", "bigFont.fnt");
    dynExitLabel->setScale(0.35f);
    dynExitLabel->setAnchorPoint({0.f, 0.5f});
    dynExitLabel->setPosition({scx - 90.f, y});
    scrollContent->addChild(dynExitLabel);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Dynamic Exit",
            "When <cg>ON</c>, custom premium exit animations\n"
            "are applied when closing popups.", this, 0.45f);
        if (iBtn) {
            float lblW = dynExitLabel->getContentSize().width * 0.35f;
            iBtn->setPosition({scx - 90.f + lblW + 8.f, y});
            toggleMenu->addChild(iBtn);
        }
    }

    m_dynamicExitToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ThumbnailSettingsPopup::onDynamicExitToggled), 0.6f);
    m_dynamicExitToggle->setPosition({scx + 90.f, y});
    m_dynamicExitToggle->toggle(m_dynamicExit);
    toggleMenu->addChild(m_dynamicExitToggle);

    // ── Popup Animation Exit Speed ──
    y -= 30.f;

    auto exitSpeedTitle = CCLabelBMFont::create("Exit Speed", "goldFont.fnt");
    exitSpeedTitle->setScale(0.4f);
    exitSpeedTitle->setPosition({scx, y});
    scrollContent->addChild(exitSpeedTitle);

    y -= 20.f;

    m_dynamicExitSpeedSlider = Slider::create(this, menu_selector(ThumbnailSettingsPopup::onDynamicExitSpeedChanged), 0.7f);
    m_dynamicExitSpeedSlider->setPosition({scx, y});
    m_dynamicExitSpeedSlider->setValue((m_currentExitSpeed - 0.2) / 2.8);
    scrollContent->addChild(m_dynamicExitSpeedSlider);

    m_dynamicExitSpeedLabel = CCLabelBMFont::create(fmt::format("{:.2f}x", m_currentExitSpeed).c_str(), "bigFont.fnt");
    m_dynamicExitSpeedLabel->setScale(0.3f);
    m_dynamicExitSpeedLabel->setPosition({scx + 95.f, y});
    scrollContent->addChild(m_dynamicExitSpeedLabel);

    // ── Peek Button ──
    // Usa un handler independiente en la escena para que el boton
    // SIEMPRE reciba toques incluso con los popups invisibles.
    {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto scene = CCDirector::sharedDirector()->getRunningScene();

        if (scene) {
            auto handler = PeekButtonHandler::create(this);
            handler->setID("paimon-peek-handler"_spr);

            m_peekMenu = CCMenu::create();
            m_peekMenu->setPosition({0, 0});
            m_peekMenu->setID("paimon-peek-menu"_spr);
            handler->addChild(m_peekMenu);

            auto eyeSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sMagicIcon_001.png");
            if (!eyeSpr) eyeSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_viewProfileTxt_001.png");
            if (!eyeSpr) eyeSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
            if (eyeSpr) {
                eyeSpr->setScale(1.0f);
                auto eyeBtn = CCMenuItemSpriteExtra::create(eyeSpr, handler,
                    menu_selector(PeekButtonHandler::onPeekToggle));
                eyeBtn->setID("peek-toggle-btn"_spr);
                eyeBtn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
                m_peekMenu->addChild(eyeBtn);
            }

            scene->addChild(handler, 99999);
        }
    }

    scroll->moveToTop();

    paimon::markDynamicPopup(this);
    return true;
}

void ThumbnailSettingsPopup::onStylePrev(CCObject*) {
    m_styleIndex--;
    if (m_styleIndex < 0) m_styleIndex = (int)m_styles.size() - 1;
    m_currentStyle = m_styles[m_styleIndex];
    updateStyleLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onStyleNext(CCObject*) {
    m_styleIndex++;
    if (m_styleIndex >= (int)m_styles.size()) m_styleIndex = 0;
    m_currentStyle = m_styles[m_styleIndex];
    updateStyleLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onIntensityChanged(CCObject*) {
    if (!m_intensitySlider) return;
    float val = m_intensitySlider->getThumb()->getValue();
    m_currentIntensity = static_cast<int>(std::round(val * 9.0f + 1.0f));
    m_currentIntensity = std::max(1, std::min(10, m_currentIntensity));
    if (m_intensityLabel) m_intensityLabel->setString(fmt::format("{}", m_currentIntensity).c_str());
    saveSettings();
}

void ThumbnailSettingsPopup::onDarknessChanged(CCObject*) {
    if (!m_darknessSlider) return;
    float val = m_darknessSlider->getThumb()->getValue();
    m_currentDarkness = static_cast<int>(std::round(val * 50.0f));
    m_currentDarkness = std::max(0, std::min(50, m_currentDarkness));
    if (m_darknessLabel) m_darknessLabel->setString(fmt::format("{}", m_currentDarkness).c_str());
    saveSettings();
}

void ThumbnailSettingsPopup::onDynamicSongToggled(CCObject*) {
    m_dynamicSong = !m_dynamicSongToggle->isToggled();
    saveSettings();
}

void ThumbnailSettingsPopup::onDynamicShadersToggled(CCObject*) {
    m_dynamicShaders = !m_dynamicShadersToggle->isToggled();
    updateStylesForDynamicShaders();
    saveSettings();
}

void ThumbnailSettingsPopup::onDynamicShadersDelayChanged(CCObject*) {
    if (!m_dynamicShadersDelaySlider) return;
    float val = m_dynamicShadersDelaySlider->getThumb()->getValue();
    m_dynamicShadersDelay = val * 2.0f; // 0..2 seconds
    m_dynamicShadersDelay = std::max(0.0f, std::min(2.0f, m_dynamicShadersDelay));
    if (m_dynamicShadersDelayLabel) {
        m_dynamicShadersDelayLabel->setString(fmt::format("{:.2f}s", m_dynamicShadersDelay).c_str());
    }
    Mod::get()->setSavedValue<float>("levelinfo-dynamic-shaders-delay", m_dynamicShadersDelay);
    if (m_onSettingsChanged) m_onSettingsChanged();
}

void ThumbnailSettingsPopup::onOpenExtraEffects(CCObject*) {
    auto popup = ExtraEffectsPopup::create();
    if (popup) {
        popup->setOnChanged(m_onSettingsChanged);
        popup->show();
    }
}

void ThumbnailSettingsPopup::togglePeek() {
    m_peekMode = !m_peekMode;
    bool show = !m_peekMode;

    // Ocultar/mostrar este popup entero
    this->setVisible(show);
    this->setTouchEnabled(show);

    // Buscar el LocalThumbnailViewPopup en la escena y ocultar/mostrar
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene) {
        auto* children = scene->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child || child == this) continue;
                if (auto* viewPopup = typeinfo_cast<LocalThumbnailViewPopup*>(child)) {
                    viewPopup->setVisible(show);
                    viewPopup->setTouchEnabled(show);
                    break;
                }
            }
        }
    }
}

void ThumbnailSettingsPopup::onTogglePeek(CCObject*) {
    togglePeek();
}

void ThumbnailSettingsPopup::onClose(CCObject* sender) {
    // Si estaba en peek mode, restaurar visibilidad antes de cerrar
    if (m_peekMode) {
        m_peekMode = false;
        this->setVisible(true);
        this->setTouchEnabled(true);

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (scene) {
            auto* children = scene->getChildren();
            if (children) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (!child || child == this) continue;
                    if (auto* viewPopup = typeinfo_cast<LocalThumbnailViewPopup*>(child)) {
                        viewPopup->setVisible(true);
                        viewPopup->setTouchEnabled(true);
                        break;
                    }
                }
            }
        }
    }

    // Quitar el handler (y su peek menu) de la escena
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene) {
        if (auto* handler = scene->getChildByID("paimon-peek-handler"_spr)) {
            handler->removeFromParent();
        }
    }
    m_peekMenu = nullptr;

    Popup::onClose(sender);
}

void ThumbnailSettingsPopup::updateStyleLabel() {
    if (m_styleValueLabel) {
        m_styleValueLabel->setString(getStyleDisplayName(m_currentStyle).c_str());
    }
}

void ThumbnailSettingsPopup::updateStylesForDynamicShaders() {
    // Shaders compatibles con dynamic (los que tienen u_time y variante _dynamic.glsl)
    // + shaders exclusivos de dynamic mode
    static const std::vector<std::string> kDynamicStyles = {
        "chromatic", "radial-blur", "glitch",
        "rain", "matrix", "neon-pulse", "wave-distortion", "crt",
        "shockwave", "vortex", "magnetic", "spotlight",
        "ripple", "plasma-cursor", "freeze", "pixelate-cursor",
        "kaleidoscope", "sonar", "electric-arc", "prism-split",
        "gravity-well", "shatter", "heat-haze", "liquify",
        "ink-spread", "hologram", "time-warp", "underwater", "neon-trail"
    };

    if (m_dynamicShaders) {
        m_styles = kDynamicStyles;
    } else {
        m_styles = m_allStyles;
    }

    // Buscar el estilo actual en la nueva lista
    bool found = false;
    m_styleIndex = 0;
    for (int i = 0; i < (int)m_styles.size(); i++) {
        if (m_styles[i] == m_currentStyle) {
            m_styleIndex = i;
            found = true;
            break;
        }
    }

    // Si el estilo actual no esta en la lista filtrada, seleccionar el primero
    if (!found && !m_styles.empty()) {
        m_styleIndex = 0;
        m_currentStyle = m_styles[0];
    }

    updateStyleLabel();
}

void ThumbnailSettingsPopup::saveSettings() {
    Mod::get()->setSettingValue<std::string>("levelinfo-background-style", m_currentStyle);
    // Guardar tambien como savedValue — no tiene validacion one-of y es mas robusto
    // para estilos dynamic-only que pueden no estar en el one-of del mod.json cacheado
    Mod::get()->setSavedValue<std::string>("levelinfo-background-style-override", m_currentStyle);
    Mod::get()->setSavedValue<int>("levelinfo-effect-intensity", m_currentIntensity);
    Mod::get()->setSavedValue<int>("levelinfo-bg-darkness", m_currentDarkness);
    Mod::get()->setSettingValue<bool>("dynamic-song", m_dynamicSong);
    Mod::get()->setSavedValue<bool>("levelinfo-dynamic-shaders", m_dynamicShaders);
    Mod::get()->setSavedValue<std::string>("popup-gallery-transition", m_currentPopupTransition);
    Mod::get()->setSavedValue<std::string>("levelinfo-bg-transition", m_currentBgTransition);

    Mod::get()->setSettingValue<bool>("dynamic-popup-enabled", m_dynamicPopup);
    Mod::get()->setSavedValue<std::string>("dynamic-popup-style", m_currentPopupStyle);
    Mod::get()->setSavedValue<double>("dynamic-popup-speed", m_currentPopupSpeed);
    Mod::get()->setSettingValue<bool>("dynamic-exit-enabled", m_dynamicExit);
    Mod::get()->setSavedValue<double>("dynamic-exit-speed", m_currentExitSpeed);

    if (m_onSettingsChanged) m_onSettingsChanged();
}

void ThumbnailSettingsPopup::onBgTransitionPrev(CCObject*) {
    m_bgTransitionIndex--;
    if (m_bgTransitionIndex < 0) m_bgTransitionIndex = (int)m_bgTransitions.size() - 1;
    m_currentBgTransition = m_bgTransitions[m_bgTransitionIndex];
    updateBgTransitionLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onBgTransitionNext(CCObject*) {
    m_bgTransitionIndex++;
    if (m_bgTransitionIndex >= (int)m_bgTransitions.size()) m_bgTransitionIndex = 0;
    m_currentBgTransition = m_bgTransitions[m_bgTransitionIndex];
    updateBgTransitionLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onBgTransitionDurationChanged(CCObject*) {
    if (!m_bgTransitionDurationSlider) return;
    float val = m_bgTransitionDurationSlider->getThumb()->getValue();
    float dur = 0.15f + val * (1.5f - 0.15f);
    dur = std::max(0.15f, std::min(1.5f, dur));
    if (m_bgTransitionDurationLabel) {
        m_bgTransitionDurationLabel->setString(fmt::format("{:.2f}s", dur).c_str());
    }
    Mod::get()->setSavedValue<float>("levelinfo-bg-transition-duration", dur);
    if (m_onSettingsChanged) m_onSettingsChanged();
}

void ThumbnailSettingsPopup::updateBgTransitionLabel() {
    if (m_bgTransitionLabel) {
        m_bgTransitionLabel->setString(getBgTransitionDisplayName(m_currentBgTransition).c_str());
    }
}

std::string ThumbnailSettingsPopup::getBgTransitionDisplayName(std::string const& transition) {
    return getPopupTransitionDisplayName(transition);
}

void ThumbnailSettingsPopup::onPopupTransitionPrev(CCObject*) {
    m_popupTransitionIndex--;
    if (m_popupTransitionIndex < 0) m_popupTransitionIndex = (int)m_popupTransitions.size() - 1;
    m_currentPopupTransition = m_popupTransitions[m_popupTransitionIndex];
    updatePopupTransitionLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onPopupTransitionNext(CCObject*) {
    m_popupTransitionIndex++;
    if (m_popupTransitionIndex >= (int)m_popupTransitions.size()) m_popupTransitionIndex = 0;
    m_currentPopupTransition = m_popupTransitions[m_popupTransitionIndex];
    updatePopupTransitionLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onPopupTransitionDurationChanged(CCObject*) {
    if (!m_popupTransitionDurationSlider) return;
    float val = m_popupTransitionDurationSlider->getThumb()->getValue();
    float dur = 0.15f + val * (1.5f - 0.15f);
    dur = std::max(0.15f, std::min(1.5f, dur));
    if (m_popupTransitionDurationLabel) {
        m_popupTransitionDurationLabel->setString(fmt::format("{:.2f}s", dur).c_str());
    }
    Mod::get()->setSavedValue<float>("popup-gallery-transition-duration", dur);
    if (m_onSettingsChanged) m_onSettingsChanged();
}

void ThumbnailSettingsPopup::updatePopupTransitionLabel() {
    if (m_popupTransitionLabel) {
        m_popupTransitionLabel->setString(getPopupTransitionDisplayName(m_currentPopupTransition).c_str());
    }
}

std::string ThumbnailSettingsPopup::getPopupTransitionDisplayName(std::string const& transition) {
    if (transition == "crossfade") return "Crossfade";
    if (transition == "slide-left") return "Slide Left";
    if (transition == "slide-right") return "Slide Right";
    if (transition == "elastic-slide") return "Elastic Slide";
    if (transition == "directional-elastic") return "Dir. Elastic";
    if (transition == "zoom-in") return "Zoom In";
    if (transition == "zoom-out") return "Zoom Out";
    if (transition == "bounce") return "Bounce";
    if (transition == "flip-horizontal") return "Flip H";
    if (transition == "flip-vertical") return "Flip V";
    if (transition == "dissolve") return "Dissolve";
    if (transition == "wave-slide") return "Wave Slide";
    if (transition == "card-flip") return "Card Flip";
    if (transition == "spin-zoom") return "Spin Zoom";
    return transition;
}

std::string ThumbnailSettingsPopup::getStyleDisplayName(std::string const& style) {
    if (style == "normal") return "Normal";
    if (style == "pixel") return "Pixel";
    if (style == "blur") return "Blur";
    if (style == "grayscale") return "Grayscale";
    if (style == "sepia") return "Sepia";
    if (style == "vignette") return "Vignette";
    if (style == "scanlines") return "Scanlines";
    if (style == "bloom") return "Bloom";
    if (style == "chromatic") return "Chromatic";
    if (style == "radial-blur") return "Radial Blur";
    if (style == "glitch") return "Glitch";
    if (style == "posterize") return "Posterize";
    if (style == "rain") return "Rain";
    if (style == "matrix") return "Matrix";
    if (style == "neon-pulse") return "Neon Pulse";
    if (style == "wave-distortion") return "Wave";
    if (style == "crt") return "CRT";
    // Dynamic-only shaders
    if (style == "shockwave") return "Shockwave";
    if (style == "vortex") return "Vortex";
    if (style == "magnetic") return "Magnetic";
    if (style == "spotlight") return "Spotlight";
    if (style == "ripple") return "Ripple";
    if (style == "plasma-cursor") return "Plasma";
    if (style == "freeze") return "Freeze";
    if (style == "pixelate-cursor") return "Pixelate+";
    // New dynamic-only shaders
    if (style == "kaleidoscope") return "Kaleidoscope";
    if (style == "sonar") return "Sonar";
    if (style == "electric-arc") return "Electric Arc";
    if (style == "prism-split") return "Prism Split";
    if (style == "gravity-well") return "Gravity Well";
    if (style == "shatter") return "Shatter";
    if (style == "heat-haze") return "Heat Haze";
    if (style == "liquify") return "Liquify";
    if (style == "ink-spread") return "Ink Spread";
    if (style == "hologram") return "Hologram";
    if (style == "time-warp") return "Time Warp";
    if (style == "underwater") return "Underwater";
    if (style == "neon-trail") return "Neon Trail";
    if (style == "paimonblur") return "Paimon Blur";
    if (style == "synthwave") return "Synthwave";
    if (style == "neon-city") return "Neon City";
    if (style == "ocean") return "Ocean";
    if (style == "galaxy") return "Galaxy";
    return style;
}

void ThumbnailSettingsPopup::onDynamicPopupToggled(CCObject*) {
    m_dynamicPopup = !m_dynamicPopupToggle->isToggled();
    saveSettings();
}

void ThumbnailSettingsPopup::onPopupStylePrev(CCObject*) {
    m_popupStyleIndex--;
    if (m_popupStyleIndex < 0) m_popupStyleIndex = (int)m_popupStyles.size() - 1;
    m_currentPopupStyle = m_popupStyles[m_popupStyleIndex];
    updatePopupStyleLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onPopupStyleNext(CCObject*) {
    m_popupStyleIndex++;
    if (m_popupStyleIndex >= (int)m_popupStyles.size()) m_popupStyleIndex = 0;
    m_currentPopupStyle = m_popupStyles[m_popupStyleIndex];
    updatePopupStyleLabel();
    saveSettings();
}

void ThumbnailSettingsPopup::onPopupSpeedChanged(CCObject*) {
    if (!m_popupSpeedSlider) return;
    float val = m_popupSpeedSlider->getThumb()->getValue();
    m_currentPopupSpeed = 0.2 + val * 2.8; // 0.2x to 3.0x
    m_currentPopupSpeed = std::max(0.2, std::min(3.0, m_currentPopupSpeed));
    if (m_popupSpeedLabel) {
        m_popupSpeedLabel->setString(fmt::format("{:.2f}x", m_currentPopupSpeed).c_str());
    }
    saveSettings();
}

void ThumbnailSettingsPopup::onDynamicExitToggled(CCObject*) {
    m_dynamicExit = !m_dynamicExitToggle->isToggled();
    saveSettings();
}

void ThumbnailSettingsPopup::onDynamicExitSpeedChanged(CCObject*) {
    if (!m_dynamicExitSpeedSlider) return;
    float val = m_dynamicExitSpeedSlider->getThumb()->getValue();
    m_currentExitSpeed = 0.2 + val * 2.8; // 0.2x to 3.0x
    m_currentExitSpeed = std::max(0.2, std::min(3.0, m_currentExitSpeed));
    if (m_dynamicExitSpeedLabel) {
        m_dynamicExitSpeedLabel->setString(fmt::format("{:.2f}x", m_currentExitSpeed).c_str());
    }
    saveSettings();
}

void ThumbnailSettingsPopup::updatePopupStyleLabel() {
    if (m_popupStyleValueLabel) {
        m_popupStyleValueLabel->setString(getPopupStyleDisplayName(m_currentPopupStyle).c_str());
    }
}

std::string ThumbnailSettingsPopup::getPopupStyleDisplayName(std::string const& style) {
    if (style == "paimonUI") return "Paimon UI";
    if (style == "jelly") return "Jelly Wobble";
    if (style == "spiral") return "Spiral Swirl";
    if (style == "drop-bounce") return "Drop Bounce";
    if (style == "skew-pop") return "Skew Pop";
    if (style == "elastic") return "Snappy Elastic";
    if (style == "bounce") return "Bouncy Physics";
    if (style == "slide-up") return "Slide Up";
    if (style == "slide-down") return "Slide Down";
    if (style == "slide-left") return "Slide Left";
    if (style == "slide-right") return "Slide Right";
    if (style == "zoom-fade") return "Zoom Fade";
    if (style == "flip") return "Card Flip H";
    if (style == "fold") return "Card Flip V";
    if (style == "pop-rotate") return "Pop Rotate";
    return style;
}

ThumbnailSettingsPopup* ThumbnailSettingsPopup::create() {
    auto ret = new ThumbnailSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
