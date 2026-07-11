#include "LevelCellSettingsPopup.hpp"
#include "../services/CompactListRefresh.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/BreakLine.hpp>

using namespace geode::prelude;
using namespace cocos2d;

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

namespace {
// BlurAPI (thesillydoggo.blur-api) marks nodes with this user-object key.
// malikhw47.blur-behind-popups calls BlurAPI::addBlur(popup) on every
// FLAlertLayer — that blur is independent of our PaiblurNode and is what
// kept the list unreadable while dragging sliders.
constexpr char const* kBlurApiTag = "thesillydoggo.blur-api/blur-options";
}

void LevelCellSettingsPopup::onExit() {
    this->unschedule(schedule_selector(LevelCellSettingsPopup::checkScrollPosition));
    this->unschedule(schedule_selector(LevelCellSettingsPopup::checkDragState));
    // If the popup closes mid-drag, restore blur so it can fade out cleanly
    // with the popup (DynamicPopupHook cleanup) instead of staying at 0 opacity.
    if (m_dragHiding) {
        m_dragHiding = false;
        m_activeDragSlider = nullptr;
        paimon::popupblur::setLivePreviewMode(this, false, 0.f);
        if (m_savedBlurApiOptions) {
            this->setUserObject(kBlurApiTag, m_savedBlurApiOptions.data());
            m_savedBlurApiOptions = nullptr;
        }
    }
    if (m_scrollArrow) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->setPosition(m_scrollArrowBasePos);
    }
    m_scrollArrowBouncing = false;
    Popup::onExit();
}

void LevelCellSettingsPopup::loadSettings() {
    m_currentBgType = Mod::get()->getSavedValue<std::string>("levelcell-background-type", "thumbnail");
    m_currentThumbWidth = static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width"));
    m_currentBlur = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-background-blur", 3.0));
    m_currentDarkness = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-background-darkness", 0.2));
    m_showSeparator = Mod::get()->getSavedValue<bool>("levelcell-show-separator", true);
    m_showViewButton = Mod::get()->getSavedValue<bool>("levelcell-show-view-button", true);
    m_compactMode = Mod::get()->getSettingValue<bool>("compact-list-mode");
    m_compactShowQuickToggle = Mod::get()->getSavedValue<bool>("compact-list-show-toggle", true);
    m_transparentMode = Mod::get()->getSavedValue<bool>("transparent-list-mode", false);
    m_hoverEffects = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
    m_currentAnimType = Mod::get()->getSavedValue<std::string>("levelcell-anim-type", "zoom-slide");
    m_currentAnimSpeed = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-anim-speed", 1.0));
    m_currentAnimEffect = Mod::get()->getSavedValue<std::string>("levelcell-anim-effect", "none");
    m_effectOnGradient = Mod::get()->getSavedValue<bool>("levelcell-effect-on-gradient", false);
    m_mythicParticles = Mod::get()->getSavedValue<bool>("levelcell-mythic-particles", true);
    m_animatedGradient = Mod::get()->getSavedValue<bool>("levelcell-animated-gradient", true);

    // Sync indices with loaded values
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
    // Always persist as the same types LevelCell / Settings.hpp read
    // (double for numeric saved values) so real-time previews pick up
    // the new values instead of falling back to defaults.
    Mod::get()->setSavedValue<std::string>("levelcell-background-type", m_currentBgType);
    Mod::get()->setSettingValue<double>("level-thumb-width", static_cast<double>(m_currentThumbWidth));
    Mod::get()->setSavedValue<double>("levelcell-background-blur", static_cast<double>(m_currentBlur));
    Mod::get()->setSavedValue<double>("levelcell-background-darkness", static_cast<double>(m_currentDarkness));
    Mod::get()->setSavedValue<bool>("levelcell-show-separator", m_showSeparator);
    Mod::get()->setSavedValue<bool>("levelcell-show-view-button", m_showViewButton);
    Mod::get()->setSettingValue<bool>("compact-list-mode", m_compactMode);
    Mod::get()->setSavedValue<bool>("compact-list-show-toggle", m_compactShowQuickToggle);
    Mod::get()->setSavedValue<bool>("transparent-list-mode", m_transparentMode);
    Mod::get()->setSettingValue<bool>("levelcell-hover-effects", m_hoverEffects);
    Mod::get()->setSavedValue<std::string>("levelcell-anim-type", m_currentAnimType);
    Mod::get()->setSavedValue<double>("levelcell-anim-speed", static_cast<double>(m_currentAnimSpeed));
    Mod::get()->setSavedValue<std::string>("levelcell-anim-effect", m_currentAnimEffect);
    Mod::get()->setSavedValue<bool>("levelcell-effect-on-gradient", m_effectOnGradient);
    Mod::get()->setSavedValue<bool>("levelcell-mythic-particles", m_mythicParticles);
    Mod::get()->setSavedValue<bool>("levelcell-animated-gradient", m_animatedGradient);

    // Bump both counters: LevelCell watches s_settingsVersion for popup
    // changes, and g_settingsVersion invalidates the shared settings cache.
    paimon::settings::internal::invalidateSettingsCache();
    s_settingsVersion++;

    if (m_onSettingsChanged) m_onSettingsChanged();
}

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

// Polls every registered slider's live-drag state each frame. Slider's
// getLiveDragging() reflects SliderTouchLogic's touch state directly, so
// this is a cheap, hook-free way to know when a drag starts/stops.
void LevelCellSettingsPopup::checkDragState(float dt) {
    Slider* dragging = nullptr;
    for (auto& row : m_sliderRows) {
        if (row.slider && row.slider->getLiveDragging()) {
            dragging = row.slider;
            break;
        }
    }

    if (dragging != m_activeDragSlider) {
        m_activeDragSlider = dragging;
        applyDragVisibility(dragging);
    }

    if (dragging) {
        updateDragCaption(dragging);
    }
}

// Keeps the floating caption glued just above the thumb of the slider being
// dragged, with live text, so the value stays readable while the rest of
// the popup is hidden.
void LevelCellSettingsPopup::updateDragCaption(Slider* active) {
    if (!m_dragCaptionPill || !active) return;

    std::string title;
    std::string valueText = "0.00";
    for (auto& row : m_sliderRows) {
        if (row.slider == active) {
            title = row.title;
            if (row.valueLabel) valueText = row.valueLabel->getString();
            break;
        }
    }
    if (m_dragCaptionLabel) {
        m_dragCaptionLabel->setString(fmt::format("{}: {}", title, valueText).c_str());
    }

    auto* thumb = active->getThumb();
    CCPoint worldPos = thumb
        ? thumb->convertToWorldSpace({thumb->getContentSize().width * 0.5f, thumb->getContentSize().height})
        : active->convertToWorldSpace({0.f, 0.f});
    CCPoint localPos = m_mainLayer->convertToNodeSpace(worldPos);
    localPos.y += 20.f;

    // clamp so the pill doesn't drift off the popup while near an edge
    auto size = m_mainLayer->getContentSize();
    float halfW = m_dragCaptionPill->getContentSize().width * 0.5f;
    localPos.x = std::clamp(localPos.x, halfW + 4.f, size.width - halfW - 4.f);
    localPos.y = std::clamp(localPos.y, 20.f, size.height - 10.f);

    m_dragCaptionPill->setPosition(localPos);
}

void LevelCellSettingsPopup::applyDragVisibility(Slider* active) {
    bool hiding = (active != nullptr);
    m_dragHiding = hiding;

    // Plain setVisible toggling: Slider, ScrollLayer and BreakLine don't
    // implement CCRGBAProtocol, so animating opacity on them isn't safe.
    // setVisible is a base CCNode method and works uniformly on everything
    // in this list, including the sliders themselves.
    for (auto* node : m_hideOnDragNodes) {
        if (!node) continue;
        if (active && node == static_cast<CCNode*>(active)) continue; // keep the active slider on screen
        node->setVisible(!hiding);
    }

    // FLAlertLayer (this popup's own base class) is a CCLayerColor: its solid
    // color quad is the dark dim covering the whole screen behind the popup.
    // Fading that away during drag lets the level list underneath act as a
    // live preview instead of staying obscured. The original opacity is
    // captured on first hide so it's restored exactly, whatever it was.
    if (hiding && m_dimOriginalOpacity == 0) {
        m_dimOriginalOpacity = this->getOpacity();
        if (m_dimOriginalOpacity == 0) m_dimOriginalOpacity = 150;
    }
    this->stopActionByTag(9912);
    auto dimFade = CCFadeTo::create(0.22f, hiding ? 0 : m_dimOriginalOpacity);
    dimFade->setTag(9912);
    this->runAction(dimFade);

    // 1) Our PaiblurNode (if active).
    constexpr float kBlurFade = 0.22f;
    paimon::popupblur::setLivePreviewMode(this, hiding, kBlurFade);

    // 2) External BlurAPI used by malikhw47.blur-behind-popups. Detach the
    // marker so its CCNode::visit hook stops drawing blur; keep a Ref so we
    // can put it back when the slider is released.
    if (hiding) {
        if (!m_savedBlurApiOptions) {
            if (auto* opts = this->getUserObject(kBlurApiTag)) {
                m_savedBlurApiOptions = opts;
                this->setUserObject(kBlurApiTag, nullptr);
            }
        }
    } else if (m_savedBlurApiOptions) {
        this->setUserObject(kBlurApiTag, m_savedBlurApiOptions.data());
        m_savedBlurApiOptions = nullptr;
    }

    if (m_dragCaptionPill) {
        m_dragCaptionPill->stopAllActions();
        if (hiding) {
            m_dragCaptionPill->setVisible(true);
            m_dragCaptionPill->setScale(0.85f);
            m_dragCaptionPill->setOpacity(0);
            m_dragCaptionPill->runAction(CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)),
                CCFadeTo::create(0.14f, 255),
                nullptr));
        } else {
            auto fadeOut = CCFadeTo::create(0.14f, 0);
            auto hide = CCCallFunc::create(this, callfunc_selector(LevelCellSettingsPopup::onDragCaptionHidden));
            m_dragCaptionPill->runAction(CCSequence::create(fadeOut, hide, nullptr));
        }
    }

    if (hiding && active) {
        updateDragCaption(active);
    }
}

void LevelCellSettingsPopup::onDragCaptionHidden() {
    if (m_dragCaptionPill) m_dragCaptionPill->setVisible(false);
}

void LevelCellSettingsPopup::registerSliderRow(Slider* slider, CCLabelBMFont* valueLabel, std::string title) {
    if (!slider) return;
    m_sliderRows.push_back({slider, valueLabel, std::move(title)});
}

bool LevelCellSettingsPopup::init() {
    // Standard Geode popup (default GJ_square01 brown frame).
    if (!Popup::init(280.f, 250.f)) return false;

    this->setTitle("LevelCell Settings");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    if (m_title) m_hideOnDragNodes.push_back(m_title);
    if (m_closeBtn) m_hideOnDragNodes.push_back(m_closeBtn);
    if (m_bgSprite) m_hideOnDragNodes.push_back(m_bgSprite);

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

    float scrollW = content.width - 16.f;
    float scrollH = content.height - 42.f;
    float totalH = 560.f;

    m_scrollLayer = geode::ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_mainLayer->addChild(m_scrollLayer, 5);
    // ScrollLayer is not in m_hideOnDragNodes: hiding the parent would hide
    // the active slider. Chrome nodes inside are hidden individually.

    auto* scrollContent = m_scrollLayer->m_contentLayer;
    scrollContent->setContentSize({scrollW, totalH});

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    // Size menu to full scroll content so GenericContentLayer cull doesn't
    // hide toggles/arrows when their menu bbox leaves the viewport.
    navMenu->ignoreAnchorPointForPosition(true);
    navMenu->setContentSize({scrollW, totalH});
    scrollContent->addChild(navMenu, 10);

    float cxs = scrollW / 2.f;
    float y = totalH - 10.f;

    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cxs, y});
        scrollContent->addChild(label);
        m_hideOnDragNodes.push_back(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.48f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cxs + halfW + 12.f, y});
                navMenu->addChild(btn);
                m_hideOnDragNodes.push_back(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label, float value, float maxVal,
                          SEL_MenuHandler callback, char const* rowTitle, int precision = 2) {
        slider = Slider::create(this, callback, 0.65f);
        slider->setPosition({cxs - 10.f, y});
        slider->setValue(value / maxVal);
        scrollContent->addChild(slider);
        m_hideOnDragNodes.push_back(slider);

        std::string valStr = precision == 1
            ? fmt::format("{:.1f}", value)
            : fmt::format("{:.2f}", value);
        label = CCLabelBMFont::create(valStr.c_str(), "bigFont.fnt");
        label->setScale(0.32f);
        label->setPosition({cxs + 95.f, y});
        scrollContent->addChild(label);
        m_hideOnDragNodes.push_back(label);

        registerSliderRow(slider, label, rowTitle);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle, bool value,
                         SEL_MenuHandler callback, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cxs - 90.f, y});
        scrollContent->addChild(lbl);
        m_hideOnDragNodes.push_back(lbl);

        if (info) {
            auto iBtn = PaimonInfo::createInfoBtn(text, info, this, 0.4f);
            if (iBtn) {
                float lblW = lbl->getContentSize().width * 0.35f;
                iBtn->setPosition({cxs - 90.f + lblW + 8.f, y});
                navMenu->addChild(iBtn);
                m_hideOnDragNodes.push_back(iBtn);
            }
        }

        toggle = CCMenuItemToggler::createWithStandardSprites(this, callback, 0.55f);
        toggle->setScale(0.55f);
        toggle->setPosition({cxs + 90.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
        m_hideOnDragNodes.push_back(toggle);
    };

    auto addSelector = [&](CCLabelBMFont*& label, std::string const& displayText,
                          SEL_MenuHandler prevCb, SEL_MenuHandler nextCb) {
        auto lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        lSpr->setScale(0.4f);
        auto lBtn = CCMenuItemSpriteExtra::create(lSpr, this, prevCb);
        lBtn->setPosition({cxs - 70.f, y});
        navMenu->addChild(lBtn);
        m_hideOnDragNodes.push_back(lBtn);

        auto rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        rSpr->setFlipX(true);
        rSpr->setScale(0.4f);
        auto rBtn = CCMenuItemSpriteExtra::create(rSpr, this, nextCb);
        rBtn->setPosition({cxs + 70.f, y});
        navMenu->addChild(rBtn);
        m_hideOnDragNodes.push_back(rBtn);

        label = CCLabelBMFont::create(displayText.c_str(), "bigFont.fnt");
        label->setScale(0.3f);
        label->setPosition({cxs, y});
        scrollContent->addChild(label);
        m_hideOnDragNodes.push_back(label);
    };

    addTitle("Background Style",
        "Choose how the cell background is rendered.\n"
        "<cy>Gradient</c>: uses level colors as a gradient.\n"
        "<cy>Thumbnail</c>: shows the level thumbnail as background.");
    y -= 18.f;
    addSelector(m_bgTypeLabel, getBgTypeDisplayName(m_currentBgType),
        menu_selector(LevelCellSettingsPopup::onBgTypePrev),
        menu_selector(LevelCellSettingsPopup::onBgTypeNext));
    y -= 24.f;

    addTitle("Thumbnail Size",
        "Controls how much of the cell width the thumbnail covers.");
    y -= 16.f;
    {
        m_thumbWidthSlider = Slider::create(
            this, menu_selector(LevelCellSettingsPopup::onThumbWidthChanged), 0.6f);
        m_thumbWidthSlider->setPosition({cxs - 10.f, y});
        m_thumbWidthSlider->setValue((m_currentThumbWidth - 0.2f) / (0.95f - 0.2f));
        scrollContent->addChild(m_thumbWidthSlider);
        m_hideOnDragNodes.push_back(m_thumbWidthSlider);

        m_thumbWidthLabel = CCLabelBMFont::create(
            fmt::format("{:.2f}", m_currentThumbWidth).c_str(), "bigFont.fnt");
        m_thumbWidthLabel->setScale(0.28f);
        m_thumbWidthLabel->setPosition({cxs + 90.f, y});
        scrollContent->addChild(m_thumbWidthLabel);
        m_hideOnDragNodes.push_back(m_thumbWidthLabel);

        registerSliderRow(m_thumbWidthSlider, m_thumbWidthLabel, "Thumbnail Size");
    }
    y -= 24.f;

    addTitle("Background Blur",
        "Gaussian blur on the thumbnail background.\n"
        "<cy>0</c> = sharp, <cy>10</c> = max blur.");
    y -= 16.f;
    addSlider(m_blurSlider, m_blurLabel, m_currentBlur, 10.0f,
        menu_selector(LevelCellSettingsPopup::onBlurChanged), "Background Blur", 1);
    y -= 24.f;

    addTitle("Background Darkness",
        "Dark overlay on the thumbnail background.\n"
        "<cy>0</c> = none, <cy>1</c> = fully dark.");
    y -= 16.f;
    addSlider(m_darknessSlider, m_darknessLabel, m_currentDarkness, 1.0f,
        menu_selector(LevelCellSettingsPopup::onDarknessChanged), "Background Darkness");
    y -= 26.f;

    if (auto* sep = geode::BreakLine::create(scrollW - 20.f, 1.f, {1.f, 1.f, 1.f, 0.15f})) {
        sep->setPosition({10.f, y + 6.f});
        sep->setAnchorPoint({0.f, 0.5f});
        scrollContent->addChild(sep);
        m_hideOnDragNodes.push_back(sep);
    }

    addTitle("Display Options");
    y -= 18.f;

    addToggle("Show Separator Line", m_separatorToggle, m_showSeparator,
        menu_selector(LevelCellSettingsPopup::onSeparatorToggled),
        "Thin line between cell content and the thumbnail area.");
    y -= 20.f;

    addToggle("Show View Button", m_viewButtonToggle, m_showViewButton,
        menu_selector(LevelCellSettingsPopup::onViewButtonToggled),
        "When OFF, the View button is hidden and the whole cell is clickable.");
    y -= 20.f;

    addToggle("Compact Mode (Lists)", m_compactToggle, m_compactMode,
        menu_selector(LevelCellSettingsPopup::onCompactToggled),
        "Shorter level cells in list views.");
    y -= 20.f;

    addToggle("Show Compact Toggle", m_compactShowToggle, m_compactShowQuickToggle,
        menu_selector(LevelCellSettingsPopup::onCompactShowToggleToggled),
        "Quick compact-mode button in the level browser.");
    y -= 20.f;

    addToggle("Transparent Lists", m_transparentToggle, m_transparentMode,
        menu_selector(LevelCellSettingsPopup::onTransparentToggled),
        "Transparent list cell backgrounds.");
    y -= 20.f;

    addToggle("Mythic Particles", m_mythicParticlesToggle, m_mythicParticles,
        menu_selector(LevelCellSettingsPopup::onMythicParticlesToggled),
        "Particles on Mythic/Legendary rated levels.");
    y -= 20.f;

    addToggle("Animated Gradient", m_animatedGradientToggle, m_animatedGradient,
        menu_selector(LevelCellSettingsPopup::onAnimatedGradientToggled),
        "Color-shifting animation on gradient backgrounds.");
    y -= 26.f;

    if (auto* sep = geode::BreakLine::create(scrollW - 20.f, 1.f, {1.f, 1.f, 1.f, 0.15f})) {
        sep->setPosition({10.f, y + 6.f});
        sep->setAnchorPoint({0.f, 0.5f});
        scrollContent->addChild(sep);
        m_hideOnDragNodes.push_back(sep);
    }

    addTitle("Hover & Animation");
    y -= 18.f;

    addToggle("Hover Animation", m_hoverToggle, m_hoverEffects,
        menu_selector(LevelCellSettingsPopup::onHoverToggled),
        "Animate cells when hovering with the mouse.");
    y -= 22.f;

    addTitle("Animation Type",
        "Animation played when hovering over a cell.");
    y -= 18.f;
    addSelector(m_animTypeLabel, getAnimTypeDisplayName(m_currentAnimType),
        menu_selector(LevelCellSettingsPopup::onAnimTypePrev),
        menu_selector(LevelCellSettingsPopup::onAnimTypeNext));
    y -= 24.f;

    addTitle("Animation Speed",
        "How fast the hover animation plays.");
    y -= 16.f;
    {
        m_animSpeedSlider = Slider::create(
            this, menu_selector(LevelCellSettingsPopup::onAnimSpeedChanged), 0.6f);
        m_animSpeedSlider->setPosition({cxs - 10.f, y});
        m_animSpeedSlider->setValue((m_currentAnimSpeed - 0.1f) / (5.0f - 0.1f));
        scrollContent->addChild(m_animSpeedSlider);
        m_hideOnDragNodes.push_back(m_animSpeedSlider);

        m_animSpeedLabel = CCLabelBMFont::create(
            fmt::format("{:.1f}", m_currentAnimSpeed).c_str(), "bigFont.fnt");
        m_animSpeedLabel->setScale(0.28f);
        m_animSpeedLabel->setPosition({cxs + 90.f, y});
        scrollContent->addChild(m_animSpeedLabel);
        m_hideOnDragNodes.push_back(m_animSpeedLabel);

        registerSliderRow(m_animSpeedSlider, m_animSpeedLabel, "Animation Speed");
    }
    y -= 24.f;

    addTitle("Color Effect",
        "Color/visual filter when hovering.");
    y -= 18.f;
    addSelector(m_animEffectLabel, getAnimEffectDisplayName(m_currentAnimEffect),
        menu_selector(LevelCellSettingsPopup::onAnimEffectPrev),
        menu_selector(LevelCellSettingsPopup::onAnimEffectNext));
    y -= 22.f;

    addToggle("Apply Effect to BG", m_effectOnGradientToggle, m_effectOnGradient,
        menu_selector(LevelCellSettingsPopup::onEffectOnGradientToggled),
        "Also apply the hover color effect to the gradient background.");

    m_scrollLayer->moveToTop();

    // Scroll hint arrow
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f);
        scrollArrow->setScale(0.35f);
        scrollArrow->setOpacity(150);
        m_scrollArrowBasePos = ccp(content.width / 2.f, 18.f);
        scrollArrow->setPosition(m_scrollArrowBasePos);
        scrollArrow->setID("scroll-hint-arrow"_spr);
        m_mainLayer->addChild(scrollArrow, 20);
        m_hideOnDragNodes.push_back(scrollArrow);

        auto moveUp = CCMoveBy::create(0.5f, {0, 3.f});
        auto moveDown = CCMoveBy::create(0.5f, {0, -3.f});
        scrollArrow->runAction(CCRepeatForever::create(
            CCSequence::create(moveUp, moveDown, nullptr)));
        m_scrollArrowBouncing = true;
        m_scrollArrow = scrollArrow;
        this->unschedule(schedule_selector(LevelCellSettingsPopup::checkScrollPosition));
        this->schedule(schedule_selector(LevelCellSettingsPopup::checkScrollPosition), 0.2f);
    }

    // Floating caption while dragging a slider
    {
        auto pill = paimon::SpriteHelper::createColorPanel(160.f, 24.f, {0, 0, 0}, 200, 6.f);
        if (pill) {
            pill->setAnchorPoint({0.5f, 0.5f});
            pill->setPosition({content.width / 2.f, content.height + 24.f});
            pill->setVisible(false);
            m_mainLayer->addChild(pill, 50);
            m_dragCaptionPill = pill;

            m_dragCaptionLabel = CCLabelBMFont::create("", "bigFont.fnt");
            m_dragCaptionLabel->setScale(0.38f);
            m_dragCaptionLabel->setPosition({80.f, 12.f});
            pill->addChild(m_dragCaptionLabel);
        }
    }

    this->unschedule(schedule_selector(LevelCellSettingsPopup::checkDragState));
    this->schedule(schedule_selector(LevelCellSettingsPopup::checkDragState), 0.f);

    paimon::markDynamicPopup(this);
    return true;
}

void LevelCellSettingsPopup::onBgTypePrev(CCObject*) {
    m_bgTypeIndex--;
    if (m_bgTypeIndex < 0) m_bgTypeIndex = (int)m_bgTypes.size() - 1;
    m_currentBgType = m_bgTypes[m_bgTypeIndex];
    if (m_bgTypeLabel) m_bgTypeLabel->setString(getBgTypeDisplayName(m_currentBgType).c_str());
    saveSettings();
    // Background style swap rebuilds gradient vs thumbnail path on each cell.
}

void LevelCellSettingsPopup::onBgTypeNext(CCObject*) {
    m_bgTypeIndex++;
    if (m_bgTypeIndex >= (int)m_bgTypes.size()) m_bgTypeIndex = 0;
    m_currentBgType = m_bgTypes[m_bgTypeIndex];
    if (m_bgTypeLabel) m_bgTypeLabel->setString(getBgTypeDisplayName(m_currentBgType).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onThumbWidthChanged(CCObject*) {
    if (!m_thumbWidthSlider) return;
    float val = m_thumbWidthSlider->getThumb()->getValue();
    // Map [0,1] slider to [0.2, 0.95]
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
    // Slider maps [0,1] to [0.1, 5.0]
    m_currentAnimSpeed = 0.1f + val * (5.0f - 0.1f);
    m_currentAnimSpeed = std::max(0.1f, std::min(5.0f, m_currentAnimSpeed));
    if (m_animSpeedLabel) m_animSpeedLabel->setString(fmt::format("{:.1f}", m_currentAnimSpeed).c_str());
    saveSettings();
}

void LevelCellSettingsPopup::onSeparatorToggled(CCObject*) {
    m_showSeparator = !m_separatorToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onViewButtonToggled(CCObject*) {
    m_showViewButton = !m_viewButtonToggle->isToggled();
    saveSettings();
    // Restoring the vanilla View button (after it was made invisible) needs a
    // full list rebuild — live cell reapply can only hide it, not un-hide it.
    paimon::thumbnails::refreshActiveLevelBrowserForCompactToggle();
}

void LevelCellSettingsPopup::onCompactToggled(CCObject*) {
    m_compactMode = !m_compactToggle->isToggled();
    saveSettings();
    paimon::thumbnails::refreshActiveLevelBrowserForCompactToggle();
}

void LevelCellSettingsPopup::onCompactShowToggleToggled(CCObject*) {
    m_compactShowQuickToggle = !m_compactShowToggle->isToggled();
    saveSettings();
}

void LevelCellSettingsPopup::onTransparentToggled(CCObject*) {
    m_transparentMode = !m_transparentToggle->isToggled();
    saveSettings();
    // Transparent mode changes the base cell background structure; rebuild.
    paimon::thumbnails::refreshActiveLevelBrowserForCompactToggle();
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

LevelCellSettingsPopup* LevelCellSettingsPopup::create() {
    auto ret = new LevelCellSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
