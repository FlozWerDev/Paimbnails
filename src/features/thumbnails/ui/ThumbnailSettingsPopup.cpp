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
        "rain", "matrix", "neon-pulse", "wave-distortion", "crt"
    };

    m_popupTransitions = {
        "crossfade", "slide-left", "slide-right",
        "elastic-slide", "directional-elastic",
        "zoom-in", "zoom-out", "bounce",
        "flip-horizontal", "flip-vertical", "dissolve"
    };

    m_bgTransitions = {
        "crossfade", "slide-left", "slide-right",
        "elastic-slide", "directional-elastic",
        "zoom-in", "zoom-out", "bounce",
        "flip-horizontal", "flip-vertical", "dissolve"
    };

    // leer settings (Geode 5 getSettingValue is exception-safe)
    m_currentStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
    m_currentIntensity = static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity"));
    m_currentDarkness = static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-bg-darkness"));
    m_dynamicSong = Mod::get()->getSettingValue<bool>("dynamic-song");
    m_currentPopupTransition = Mod::get()->getSettingValue<std::string>("popup-gallery-transition");
    float popupTransDur = Mod::get()->getSettingValue<float>("popup-gallery-transition-duration");
    m_currentBgTransition = Mod::get()->getSettingValue<std::string>("levelinfo-bg-transition");
    float bgTransDur = Mod::get()->getSettingValue<float>("levelinfo-bg-transition-duration");

    for (int i = 0; i < (int)m_styles.size(); i++) {
        if (m_styles[i] == m_currentStyle) { m_styleIndex = i; break; }
    }
    for (int i = 0; i < (int)m_popupTransitions.size(); i++) {
        if (m_popupTransitions[i] == m_currentPopupTransition) { m_popupTransitionIndex = i; break; }
    }
    for (int i = 0; i < (int)m_bgTransitions.size(); i++) {
        if (m_bgTransitions[i] == m_currentBgTransition) { m_bgTransitionIndex = i; break; }
    }

    float cx = content.width / 2.f;

    // ── Scrollable content ──
    float scrollH = content.height - 50.f;
    float totalContentH = 580.f; // enough for all controls

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

void ThumbnailSettingsPopup::saveSettings() {
    Mod::get()->setSettingValue<std::string>("levelinfo-background-style", m_currentStyle);
    Mod::get()->setSettingValue<int64_t>("levelinfo-effect-intensity", static_cast<int64_t>(m_currentIntensity));
    Mod::get()->setSettingValue<int64_t>("levelinfo-bg-darkness", static_cast<int64_t>(m_currentDarkness));
    Mod::get()->setSettingValue<bool>("dynamic-song", m_dynamicSong);
    Mod::get()->setSettingValue<std::string>("popup-gallery-transition", m_currentPopupTransition);
    Mod::get()->setSettingValue<std::string>("levelinfo-bg-transition", m_currentBgTransition);

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
    Mod::get()->setSettingValue<float>("levelinfo-bg-transition-duration", dur);
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
    Mod::get()->setSettingValue<float>("popup-gallery-transition-duration", dur);
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
