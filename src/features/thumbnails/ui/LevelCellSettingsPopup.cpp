#include "LevelCellSettingsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/InfoButton.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// ────────────────────────────────────────────────────────────
// helpers
// ────────────────────────────────────────────────────────────

std::string LevelCellSettingsPopup::getBgTypeDisplayName(std::string const& type) {
    if (type == "gradient") return "Gradient";
    if (type == "thumbnail") return "Thumbnail";
    return type;
}

std::string LevelCellSettingsPopup::getAnimTypeDisplayName(std::string const& type) {
    if (type == "none") return "None";
    if (type == "zoom-slide") return "Zoom Slide";
    if (type == "zoom") return "Zoom";
    if (type == "slide") return "Slide";
    if (type == "bounce") return "Bounce";
    if (type == "rotate") return "Rotate";
    if (type == "rotate-content") return "Rotate Content";
    if (type == "shake") return "Shake";
    if (type == "pulse") return "Pulse";
    if (type == "swing") return "Swing";
    return type;
}

std::string LevelCellSettingsPopup::getAnimEffectDisplayName(std::string const& effect) {
    if (effect == "none") return "None";
    if (effect == "brightness") return "Brightness";
    if (effect == "darken") return "Darken";
    if (effect == "sepia") return "Sepia";
    if (effect == "red") return "Red";
    if (effect == "blue") return "Blue";
    if (effect == "gold") return "Gold";
    if (effect == "fade") return "Fade";
    if (effect == "grayscale") return "Grayscale";
    if (effect == "blur") return "Blur";
    if (effect == "invert") return "Invert";
    if (effect == "glitch") return "Glitch";
    if (effect == "sharpen") return "Sharpen";
    if (effect == "edge-detection") return "Edge Detection";
    if (effect == "vignette") return "Vignette";
    if (effect == "pixelate") return "Pixelate";
    if (effect == "posterize") return "Posterize";
    if (effect == "chromatic") return "Chromatic";
    if (effect == "scanlines") return "Scanlines";
    if (effect == "solarize") return "Solarize";
    if (effect == "rainbow") return "Rainbow";
    return effect;
}

void LevelCellSettingsPopup::onExit() {
    this->unschedule(schedule_selector(LevelCellSettingsPopup::checkScrollPosition));
    if (m_scrollArrow) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->setPosition(m_scrollArrowBasePos);
    }
    m_scrollArrowBouncing = false;
    Popup::onExit();
}


// ────────────────────────────────────────────────────────────
// load / save
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::loadSettings() {
    // Geode 5 getSettingValue is exception-safe — no try/catch needed
    m_currentBgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type");
    m_currentThumbWidth = static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width"));
    m_currentBlur = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-blur"));
    m_currentDarkness = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-darkness"));
    m_showSeparator = Mod::get()->getSettingValue<bool>("levelcell-show-separator");
    m_showViewButton = Mod::get()->getSettingValue<bool>("levelcell-show-view-button");
    m_compactMode = Mod::get()->getSettingValue<bool>("compact-list-mode");
    m_transparentMode = Mod::get()->getSettingValue<bool>("transparent-list-mode");
    m_hoverEffects = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
    m_currentAnimType = Mod::get()->getSettingValue<std::string>("levelcell-anim-type");
    m_currentAnimSpeed = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-anim-speed"));
    m_currentAnimEffect = Mod::get()->getSettingValue<std::string>("levelcell-anim-effect");
    m_effectOnGradient = Mod::get()->getSettingValue<bool>("levelcell-effect-on-gradient");
    m_mythicParticles = Mod::get()->getSettingValue<bool>("levelcell-mythic-particles");
    m_animatedGradient = Mod::get()->getSettingValue<bool>("levelcell-animated-gradient");

    // indices
    for (int i = 0; i < (int)m_bgTypes.size(); i++) {
        if (m_bgTypes[i] == m_currentBgType) { m_bgTypeIndex = i; break; }
    }
    for (int i = 0; i < (int)m_animTypes.size(); i++) {
        if (m_animTypes[i] == m_currentAnimType) { m_animTypeIndex = i; break; }
    }
    for (int i = 0; i < (int)m_animEffects.size(); i++) {
        if (m_animEffects[i] == m_currentAnimEffect) { m_animEffectIndex = i; break; }
    }
}

void LevelCellSettingsPopup::saveSettings() {
    Mod::get()->setSettingValue<std::string>("levelcell-background-type", m_currentBgType);
    Mod::get()->setSettingValue<float>("level-thumb-width", m_currentThumbWidth);
    Mod::get()->setSettingValue<float>("levelcell-background-blur", m_currentBlur);
    Mod::get()->setSettingValue<float>("levelcell-background-darkness", m_currentDarkness);
    Mod::get()->setSettingValue<bool>("levelcell-show-separator", m_showSeparator);
    Mod::get()->setSettingValue<bool>("levelcell-show-view-button", m_showViewButton);
    Mod::get()->setSettingValue<bool>("compact-list-mode", m_compactMode);
    Mod::get()->setSettingValue<bool>("transparent-list-mode", m_transparentMode);
    Mod::get()->setSettingValue<bool>("levelcell-hover-effects", m_hoverEffects);
    Mod::get()->setSettingValue<std::string>("levelcell-anim-type", m_currentAnimType);
    Mod::get()->setSettingValue<float>("levelcell-anim-speed", m_currentAnimSpeed);
    Mod::get()->setSettingValue<std::string>("levelcell-anim-effect", m_currentAnimEffect);
    Mod::get()->setSettingValue<bool>("levelcell-effect-on-gradient", m_effectOnGradient);
    Mod::get()->setSettingValue<bool>("levelcell-mythic-particles", m_mythicParticles);
    Mod::get()->setSettingValue<bool>("levelcell-animated-gradient", m_animatedGradient);

    if (m_onSettingsChanged) m_onSettingsChanged();

    // incrementar version global pa que LevelCell detecte el cambio en tiempo real
    s_settingsVersion++;
}

// ────────────────────────────────────────────────────────────
// scroll indicator
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::checkScrollPosition(float dt) {
    if (!m_scrollArrow || !m_scrollLayer) return;

    // ver si el scroll llego cerca del fondo
    float minY = m_scrollLayer->m_contentLayer->getContentSize().height -
                 m_scrollLayer->getContentSize().height;
    float currentY = m_scrollLayer->m_contentLayer->getPositionY();

    // si el offset es cercano al minimo (abajo), ocultar la flecha
    bool nearBottom = (currentY <= -minY + 20.f);

    if (nearBottom) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->setPosition(m_scrollArrowBasePos);
        m_scrollArrowBouncing = false;
        if (m_scrollArrow->getOpacity() > 0) {
            m_scrollArrow->runAction(CCFadeTo::create(0.3f, 0));
        }
    } else {
        if (!m_scrollArrowBouncing) {
            m_scrollArrow->stopAllActions();
            m_scrollArrow->setPosition(m_scrollArrowBasePos);
            auto moveUp = CCMoveBy::create(0.5f, {0, 3.f});
            auto moveDown = CCMoveBy::create(0.5f, {0, -3.f});
            auto seq = CCSequence::create(moveUp, moveDown, nullptr);
            auto bounce = CCRepeatForever::create(seq);
            if (m_scrollArrow->getOpacity() < 150) {
                auto fadeIn = CCFadeTo::create(0.3f, 150);
                auto spawn = CCSpawn::create(fadeIn, bounce, nullptr);
                m_scrollArrow->runAction(spawn);
            } else {
                m_scrollArrow->runAction(bounce);
            }
            m_scrollArrowBouncing = true;
        }
    }
}

// ────────────────────────────────────────────────────────────
// setup
// ────────────────────────────────────────────────────────────

bool LevelCellSettingsPopup::init() {
    if (!Popup::init(260.f, 240.f)) return false;

    this->setTitle("LevelCell Settings");

    auto content = m_mainLayer->getContentSize();

    // option arrays
    m_bgTypes = {"gradient", "thumbnail"};
    m_animTypes = {
        "none", "zoom-slide", "zoom", "slide", "bounce",
        "rotate", "rotate-content", "shake", "pulse", "swing"
    };
    m_animEffects = {
        "none", "brightness", "darken", "sepia", "red", "blue", "gold",
        "fade", "grayscale", "blur", "invert", "glitch", "sharpen",
        "edge-detection", "vignette", "pixelate", "posterize", "chromatic",
        "scanlines", "solarize", "rainbow"
    };

    loadSettings();

    // ── ScrollLayer para todo el contenido ──
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 42.f; // espacio para titulo
    float totalH = 580.f; // alto total del contenido scroll

    m_scrollLayer = geode::ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_mainLayer->addChild(m_scrollLayer, 5);

    auto* scrollContent = m_scrollLayer->m_contentLayer;
    scrollContent->setContentSize({scrollW, totalH});

    // menu para botones interactivos
    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    scrollContent->addChild(navMenu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 8.f;

    // helper lambdas
    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cx, y});
        scrollContent->addChild(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.48f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cx + halfW + 12.f, y});
                navMenu->addChild(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label, float value, float maxVal, SEL_MenuHandler callback) {
        slider = Slider::create(this, callback, 0.65f);
        slider->setPosition({cx, y});
        slider->setValue(value / maxVal);
        scrollContent->addChild(slider);

        std::string valStr = fmt::format("{:.2f}", value);
        label = CCLabelBMFont::create(valStr.c_str(), "bigFont.fnt");
        label->setScale(0.35f);
        label->setPosition({cx + 95.f, y});
        scrollContent->addChild(label);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle, bool value, SEL_MenuHandler callback, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 90.f, y});
        scrollContent->addChild(lbl);

        if (info) {
            auto iBtn = PaimonInfo::createInfoBtn(text, info, this, 0.4f);
            if (iBtn) {
                float lblW = lbl->getContentSize().width * 0.35f;
                iBtn->setPosition({cx - 90.f + lblW + 8.f, y});
                navMenu->addChild(iBtn);
            }
        }

        toggle = CCMenuItemToggler::createWithStandardSprites(this, callback, 0.35f);
        toggle->setPosition({cx + 90.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
    };

    auto addSelector = [&](CCLabelBMFont*& label, std::string const& displayText,
                          SEL_MenuHandler prevCb, SEL_MenuHandler nextCb) {
        auto lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        lSpr->setScale(0.4f);
        auto lBtn = CCMenuItemSpriteExtra::create(lSpr, this, prevCb);
        lBtn->setPosition({cx - 70.f, y});
        navMenu->addChild(lBtn);

        auto rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        rSpr->setFlipX(true);
        rSpr->setScale(0.4f);
        auto rBtn = CCMenuItemSpriteExtra::create(rSpr, this, nextCb);
        rBtn->setPosition({cx + 70.f, y});
        navMenu->addChild(rBtn);

        label = CCLabelBMFont::create(displayText.c_str(), "bigFont.fnt");
        label->setScale(0.3f);
        label->setPosition({cx, y});
        scrollContent->addChild(label);
    };

    // ═══════════════════════════════════════════
    // BACKGROUND SECTION
    // ═══════════════════════════════════════════
    addTitle("Background Style",
        "Choose how the cell background is rendered.\n"
        "<cy>Gradient</c>: uses level colors as a gradient.\n"
        "<cy>Thumbnail</c>: shows the level thumbnail as background.");
    y -= 18.f;

    addSelector(m_bgTypeLabel, getBgTypeDisplayName(m_currentBgType),
        menu_selector(LevelCellSettingsPopup::onBgTypePrev),
        menu_selector(LevelCellSettingsPopup::onBgTypeNext));
    y -= 22.f;

    // Thumbnail Size
    addTitle("Thumbnail Size",
        "Controls how much of the cell width the thumbnail covers.\n"
        "<cy>Lower values</c> = smaller thumbnail on the right.\n"
        "<cy>Higher values</c> = thumbnail fills more of the cell.");
    y -= 16.f;
    {
        m_thumbWidthSlider = Slider::create(this, menu_selector(LevelCellSettingsPopup::onThumbWidthChanged), 0.6f);
        m_thumbWidthSlider->setPosition({cx - 5.f, y});
        m_thumbWidthSlider->setValue((m_currentThumbWidth - 0.2f) / (0.95f - 0.2f));
        scrollContent->addChild(m_thumbWidthSlider);

        m_thumbWidthLabel = CCLabelBMFont::create(fmt::format("{:.2f}", m_currentThumbWidth).c_str(), "bigFont.fnt");
        m_thumbWidthLabel->setScale(0.25f);
        m_thumbWidthLabel->setPosition({cx + 82.f, y});
        scrollContent->addChild(m_thumbWidthLabel);
    }
    y -= 22.f;

    // Background Blur
    addTitle("Background Blur",
        "Applies a gaussian blur to the thumbnail background.\n"
        "<cy>0</c> = no blur (sharp image).\n"
        "<cy>10</c> = maximum blur (very soft/dreamy).");
    y -= 16.f;
    addSlider(m_blurSlider, m_blurLabel, m_currentBlur, 10.0f,
        menu_selector(LevelCellSettingsPopup::onBlurChanged));
    y -= 22.f;

    // Background Darkness
    addTitle("Background Darkness",
        "Darkens the thumbnail background with a semi-transparent overlay.\n"
        "<cy>0</c> = no darkening.\n"
        "<cy>1</c> = fully dark. Helps text readability.");
    y -= 16.f;
    addSlider(m_darknessSlider, m_darknessLabel, m_currentDarkness, 1.0f,
        menu_selector(LevelCellSettingsPopup::onDarknessChanged));
    y -= 24.f;

    // ═══════════════════════════════════════════
    // TOGGLES SECTION
    // ═══════════════════════════════════════════
    addTitle("Display Options");
    y -= 18.f;

    addToggle("Show Separator Line", m_separatorToggle, m_showSeparator,
        menu_selector(LevelCellSettingsPopup::onSeparatorToggled),
        "Shows a thin line between the cell content and the thumbnail area.\nHelps visually separate text from the background image.");
    y -= 20.f;

    addToggle("Show View Button", m_viewButtonToggle, m_showViewButton,
        menu_selector(LevelCellSettingsPopup::onViewButtonToggled),
        "When <cr>OFF</c>, the View button is hidden and replaced with an invisible full-cell click area.\nThe entire cell becomes clickable to open the level.");
    y -= 20.f;

    addToggle("Compact Mode (Lists)", m_compactToggle, m_compactMode,
        menu_selector(LevelCellSettingsPopup::onCompactToggled),
        "Makes level cells in <cy>list views</c> shorter/more compact.\nFits more levels on screen at once.\nInspired by <cy>CompactLists</c> mod.");
    y -= 20.f;

    addToggle("Transparent Lists", m_transparentToggle, m_transparentMode,
        menu_selector(LevelCellSettingsPopup::onTransparentToggled),
        "Makes list cell backgrounds <cy>transparent</c> for a cleaner look.\nRemoves the brown background from level cells.\nInspired by <cy>Transparent Lists</c> mod.");
    y -= 20.f;

    addToggle("Mythic Particles", m_mythicParticlesToggle, m_mythicParticles,
        menu_selector(LevelCellSettingsPopup::onMythicParticlesToggled),
        "Adds floating particle effects to <cy>Mythic/Legendary</c> rated levels.\nParticles use the level's dominant colors for a unique look.");
    y -= 20.f;

    addToggle("Animated Gradient", m_animatedGradientToggle, m_animatedGradient,
        menu_selector(LevelCellSettingsPopup::onAnimatedGradientToggled),
        "Enables a smooth color-shifting animation on the gradient background.\nThe colors gently cycle based on the level's palette.");
    y -= 24.f;

    // ═══════════════════════════════════════════
    // ANIMATION SECTION
    // ═══════════════════════════════════════════
    addTitle("Hover & Animation");
    y -= 18.f;

    addToggle("Hover Animation", m_hoverToggle, m_hoverEffects,
        menu_selector(LevelCellSettingsPopup::onHoverToggled),
        "Enables animations when you hover over a level cell with the mouse.\nThe cell will react with the selected animation type.");
    y -= 22.f;

    // Animation Type
    addTitle("Animation Type",
        "The animation played when hovering over a cell.\n"
        "<cy>Zoom Slide</c>: subtle zoom + slide.\n"
        "<cy>Bounce</c>: springy bounce effect.\n"
        "<cy>Rotate</c>: slight 3D rotation.\n"
        "<cy>Pulse</c>: gentle pulse effect.\n"
        "...and more!");
    y -= 18.f;
    addSelector(m_animTypeLabel, getAnimTypeDisplayName(m_currentAnimType),
        menu_selector(LevelCellSettingsPopup::onAnimTypePrev),
        menu_selector(LevelCellSettingsPopup::onAnimTypeNext));
    y -= 22.f;

    // Animation Speed
    addTitle("Animation Speed",
        "How fast the hover animation plays.\n"
        "<cy>0.1</c> = very slow, smooth.\n"
        "<cy>5.0</c> = very fast, snappy.");
    y -= 16.f;
    {
        m_animSpeedSlider = Slider::create(this, menu_selector(LevelCellSettingsPopup::onAnimSpeedChanged), 0.6f);
        m_animSpeedSlider->setPosition({cx - 5.f, y});
        m_animSpeedSlider->setValue((m_currentAnimSpeed - 0.1f) / (5.0f - 0.1f));
        scrollContent->addChild(m_animSpeedSlider);

        m_animSpeedLabel = CCLabelBMFont::create(fmt::format("{:.1f}", m_currentAnimSpeed).c_str(), "bigFont.fnt");
        m_animSpeedLabel->setScale(0.25f);
        m_animSpeedLabel->setPosition({cx + 82.f, y});
        scrollContent->addChild(m_animSpeedLabel);
    }
    y -= 22.f;

    // Color Effect
    addTitle("Color Effect",
        "Applies a color/visual filter when hovering.\n"
        "<cy>Brightness</c>: lightens the cell.\n"
        "<cy>Sepia</c>: warm vintage tone.\n"
        "<cy>Grayscale</c>: removes color.\n"
        "<cy>Rainbow</c>: cycles through colors.\n"
        "...and 15+ more effects!");
    y -= 18.f;
    addSelector(m_animEffectLabel, getAnimEffectDisplayName(m_currentAnimEffect),
        menu_selector(LevelCellSettingsPopup::onAnimEffectPrev),
        menu_selector(LevelCellSettingsPopup::onAnimEffectNext));
    y -= 22.f;

    addToggle("Apply Effect to BG", m_effectOnGradientToggle, m_effectOnGradient,
        menu_selector(LevelCellSettingsPopup::onEffectOnGradientToggled),
        "When <cg>ON</c>, the color effect is also applied to the gradient background, not just the thumbnail.\nCreates a more immersive hover effect.");

    // scroll al inicio (arriba)
    m_scrollLayer->moveToTop();

    // ── Indicador de scroll (flecha animada abajo) ──
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f); // apuntar hacia abajo
        scrollArrow->setScale(0.35f);
        scrollArrow->setOpacity(150);
        m_scrollArrowBasePos = ccp(content.width / 2.f, 18.f);
        scrollArrow->setPosition(m_scrollArrowBasePos);
        scrollArrow->setID("scroll-hint-arrow"_spr);
        m_mainLayer->addChild(scrollArrow, 20);

        // animacion bounce para indicar que se puede scrollear
        auto moveUp = CCMoveBy::create(0.5f, {0, 3.f});
        auto moveDown = CCMoveBy::create(0.5f, {0, -3.f});
        auto seq = CCSequence::create(moveUp, moveDown, nullptr);
        auto repeat = CCRepeatForever::create(seq);
        scrollArrow->runAction(repeat);
        m_scrollArrowBouncing = true;

        // ocultar flecha cuando el usuario scrollea hasta abajo
        m_scrollArrow = scrollArrow;
        this->unschedule(schedule_selector(LevelCellSettingsPopup::checkScrollPosition));
        this->schedule(schedule_selector(LevelCellSettingsPopup::checkScrollPosition), 0.2f);
    }

    paimon::markDynamicPopup(this);
    return true;
}

// ────────────────────────────────────────────────────────────
// callbacks: Background Type
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::onBgTypePrev(CCObject*) {
    m_bgTypeIndex--;
    if (m_bgTypeIndex < 0) m_bgTypeIndex = (int)m_bgTypes.size() - 1;
    m_currentBgType = m_bgTypes[m_bgTypeIndex];
    if (m_bgTypeLabel) m_bgTypeLabel->setString(getBgTypeDisplayName(m_currentBgType).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onBgTypeNext(CCObject*) {
    m_bgTypeIndex++;
    if (m_bgTypeIndex >= (int)m_bgTypes.size()) m_bgTypeIndex = 0;
    m_currentBgType = m_bgTypes[m_bgTypeIndex];
    if (m_bgTypeLabel) m_bgTypeLabel->setString(getBgTypeDisplayName(m_currentBgType).c_str());
    saveSettings();
}

// ────────────────────────────────────────────────────────────
// callbacks: Sliders
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::onThumbWidthChanged(CCObject*) {
    if (!m_thumbWidthSlider) return;
    float val = m_thumbWidthSlider->getThumb()->getValue();
    // rango 0.2 - 0.95
    m_currentThumbWidth = 0.2f + val * (0.95f - 0.2f);
    m_currentThumbWidth = std::max(0.2f, std::min(0.95f, m_currentThumbWidth));
    if (m_thumbWidthLabel) m_thumbWidthLabel->setString(fmt::format("{:.2f}", m_currentThumbWidth).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onBlurChanged(CCObject*) {
    if (!m_blurSlider) return;
    float val = m_blurSlider->getThumb()->getValue();
    m_currentBlur = val * 10.0f;
    m_currentBlur = std::max(0.0f, std::min(10.0f, m_currentBlur));
    if (m_blurLabel) m_blurLabel->setString(fmt::format("{:.1f}", m_currentBlur).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onDarknessChanged(CCObject*) {
    if (!m_darknessSlider) return;
    float val = m_darknessSlider->getThumb()->getValue();
    m_currentDarkness = val * 1.0f;
    m_currentDarkness = std::max(0.0f, std::min(1.0f, m_currentDarkness));
    if (m_darknessLabel) m_darknessLabel->setString(fmt::format("{:.2f}", m_currentDarkness).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onAnimSpeedChanged(CCObject*) {
    if (!m_animSpeedSlider) return;
    float val = m_animSpeedSlider->getThumb()->getValue();
    // rango 0.1 - 5.0
    m_currentAnimSpeed = 0.1f + val * (5.0f - 0.1f);
    m_currentAnimSpeed = std::max(0.1f, std::min(5.0f, m_currentAnimSpeed));
    if (m_animSpeedLabel) m_animSpeedLabel->setString(fmt::format("{:.1f}", m_currentAnimSpeed).c_str());
    saveSettings();
}

// ────────────────────────────────────────────────────────────
// callbacks: Toggles
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::onSeparatorToggled(CCObject*) {
    m_showSeparator = !m_separatorToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onViewButtonToggled(CCObject*) {
    m_showViewButton = !m_viewButtonToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onCompactToggled(CCObject*) {
    m_compactMode = !m_compactToggle->isToggled();
    saveSettings();
    // List relayout is handled by LevelCell::update() detecting the settings
    // version change and calling setupList() on the parent BoomListView.
    // Calling reloadAll()/reloadData() from the popup crashes because it
    // destroys cells while the toggle's touch is still being processed.
}

void LevelCellSettingsPopup::onTransparentToggled(CCObject*) {
    m_transparentMode = !m_transparentToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onHoverToggled(CCObject*) {
    m_hoverEffects = !m_hoverToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onEffectOnGradientToggled(CCObject*) {
    m_effectOnGradient = !m_effectOnGradientToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onMythicParticlesToggled(CCObject*) {
    m_mythicParticles = !m_mythicParticlesToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onAnimatedGradientToggled(CCObject*) {
    m_animatedGradient = !m_animatedGradientToggle->isToggled();
    saveSettings();
}

// ────────────────────────────────────────────────────────────
// callbacks: Animation Type
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::onAnimTypePrev(CCObject*) {
    m_animTypeIndex--;
    if (m_animTypeIndex < 0) m_animTypeIndex = (int)m_animTypes.size() - 1;
    m_currentAnimType = m_animTypes[m_animTypeIndex];
    if (m_animTypeLabel) m_animTypeLabel->setString(getAnimTypeDisplayName(m_currentAnimType).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onAnimTypeNext(CCObject*) {
    m_animTypeIndex++;
    if (m_animTypeIndex >= (int)m_animTypes.size()) m_animTypeIndex = 0;
    m_currentAnimType = m_animTypes[m_animTypeIndex];
    if (m_animTypeLabel) m_animTypeLabel->setString(getAnimTypeDisplayName(m_currentAnimType).c_str());
    saveSettings();
}

// ────────────────────────────────────────────────────────────
// callbacks: Color Effect
// ────────────────────────────────────────────────────────────

void LevelCellSettingsPopup::onAnimEffectPrev(CCObject*) {
    m_animEffectIndex--;
    if (m_animEffectIndex < 0) m_animEffectIndex = (int)m_animEffects.size() - 1;
    m_currentAnimEffect = m_animEffects[m_animEffectIndex];
    if (m_animEffectLabel) m_animEffectLabel->setString(getAnimEffectDisplayName(m_currentAnimEffect).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onAnimEffectNext(CCObject*) {
    m_animEffectIndex++;
    if (m_animEffectIndex >= (int)m_animEffects.size()) m_animEffectIndex = 0;
    m_currentAnimEffect = m_animEffects[m_animEffectIndex];
    if (m_animEffectLabel) m_animEffectLabel->setString(getAnimEffectDisplayName(m_currentAnimEffect).c_str());
    saveSettings();
}

// ────────────────────────────────────────────────────────────
// create
// ────────────────────────────────────────────────────────────

LevelCellSettingsPopup* LevelCellSettingsPopup::create() {
    auto ret = new LevelCellSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}









