#include "GradientAnimationPopup.hpp"

#include "CustomAnimationPopup.hpp"

#include "../GradientUtils.hpp"

#include <Geode/binding/SliderThumb.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

namespace paimon::icon_gradients {

namespace {

constexpr float kSpeedMin = 0.1f;
constexpr float kSpeedMax = 4.f;

constexpr std::array kEffects = {
    GradientAnimationType::Flow,
    GradientAnimationType::Pulse,
    GradientAnimationType::Spin,
    GradientAnimationType::Orbit,
    GradientAnimationType::Swing,
    GradientAnimationType::Custom,
};

// Effect buttons share one row, so the spacing follows the count.
constexpr float kEffectFirstX = 50.f;
constexpr float kEffectStepX = 66.f;

struct PreviewIcon {
    IconType type;
    char const* name;
};

constexpr std::array kPreviewIcons = {
    PreviewIcon{IconType::Cube, "Cube"},
    PreviewIcon{IconType::Ship, "Ship"},
    PreviewIcon{IconType::Ball, "Ball"},
    PreviewIcon{IconType::Ufo, "UFO"},
    PreviewIcon{IconType::Wave, "Wave"},
    PreviewIcon{IconType::Robot, "Robot"},
    PreviewIcon{IconType::Spider, "Spider"},
    PreviewIcon{IconType::Swing, "Swing"},
    PreviewIcon{IconType::Jetpack, "Jetpack"},
};

float speedFromSlider(float value) {
    return kSpeedMin + std::clamp(value, 0.f, 1.f) * (kSpeedMax - kSpeedMin);
}

float speedToSlider(float speed) {
    return std::clamp((speed - kSpeedMin) / (kSpeedMax - kSpeedMin), 0.f, 1.f);
}

CCLabelBMFont* addLabel(CCNode* parent, char const* text, CCPoint position,
                        float scale = 0.34f, char const* font = "bigFont.fnt") {
    auto label = CCLabelBMFont::create(text, font);
    label->setScale(scale);
    label->setPosition(position);
    parent->addChild(label, 2);
    return label;
}

void addPanel(CCNode* parent, CCPoint position, CCSize size, char const* id) {
    auto panel = CCScale9Sprite::create("square02b_001.png");
    panel->setContentSize(size);
    panel->setColor({0, 0, 0});
    panel->setOpacity(72);
    panel->setPosition(position);
    panel->setID(id);
    parent->addChild(panel);
}

} // namespace

GradientAnimationPopup* GradientAnimationPopup::create(IconType initialType, bool secondPlayer) {
    auto ret = new GradientAnimationPopup();
    if (ret->init(initialType, secondPlayer)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool GradientAnimationPopup::init(IconType initialType, bool secondPlayer) {
    if (!Popup::init(430.f, 290.f)) return false;

    m_secondPlayer = secondPlayer;

    setTitle("Gradient Animations", "goldFont.fnt", 0.72f, 18.f);
    setID("gradient-animation-popup");

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    addPanel(m_mainLayer, {125.f, 200.f}, {220.f, 100.f}, "animation-preview-panel");
    addPanel(m_mainLayer, {335.f, 200.f}, {170.f, 100.f}, "animation-options-panel");
    addPanel(m_mainLayer, {215.f, 125.f}, {410.f, 44.f}, "animation-effects-panel");
    addPanel(m_mainLayer, {215.f, 59.f}, {410.f, 78.f}, "animation-controls-panel");

    addLabel(m_mainLayer, "LIVE PREVIEW", {125.f, 240.f}, 0.3f, "goldFont.fnt");
    addLabel(m_mainLayer, "OPTIONS", {335.f, 240.f}, 0.3f, "goldFont.fnt");

    m_previewHost = CCNode::create();
    m_previewHost->setContentSize({160.f, 66.f});
    m_previewHost->setAnchorPoint({0.5f, 0.5f});
    m_previewHost->ignoreAnchorPointForPosition(false);
    m_previewHost->setPosition({125.f, 202.f});
    m_previewHost->setID("animation-preview-icon");
    m_mainLayer->addChild(m_previewHost, 3);

    auto arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    arrow->setScale(0.45f);
    auto previous = CCMenuItemSpriteExtra::create(
        arrow, this, menu_selector(GradientAnimationPopup::onPreviousIcon)
    );
    previous->setPosition({35.f, 200.f});
    previous->setID("previous-preview-icon");
    menu->addChild(previous);

    auto nextArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    nextArrow->setScale(0.45f);
    nextArrow->setFlipX(true);
    auto next = CCMenuItemSpriteExtra::create(
        nextArrow, this, menu_selector(GradientAnimationPopup::onNextIcon)
    );
    next->setPosition({215.f, 200.f});
    next->setID("next-preview-icon");
    menu->addChild(next);

    m_iconLabel = addLabel(m_mainLayer, "Cube", {125.f, 162.f}, 0.28f);

    auto enabledLabel = addLabel(m_mainLayer, "Enabled", {270.f, 216.f}, 0.32f);
    enabledLabel->setAnchorPoint({0.f, 0.5f});
    m_enabledToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(GradientAnimationPopup::onEnabled), 0.55f
    );
    m_enabledToggle->setPosition({398.f, 216.f});
    m_enabledToggle->setID("animation-enabled-toggle");
    menu->addChild(m_enabledToggle);

    auto reverseLabel = addLabel(m_mainLayer, "Reverse", {270.f, 190.f}, 0.32f);
    reverseLabel->setAnchorPoint({0.f, 0.5f});
    m_reverseToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(GradientAnimationPopup::onReverse), 0.55f
    );
    m_reverseToggle->setPosition({398.f, 190.f});
    m_reverseToggle->setID("animation-reverse-toggle");
    menu->addChild(m_reverseToggle);

    auto resetSprite = ButtonSprite::create(
        "Reset", 62, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.42f
    );
    resetSprite->setScale(0.68f);
    auto reset = CCMenuItemSpriteExtra::create(
        resetSprite, this, menu_selector(GradientAnimationPopup::onReset)
    );
    reset->setPosition({296.f, 165.f});
    reset->setID("reset-animation-button");
    menu->addChild(reset);

    auto editSprite = ButtonSprite::create(
        "Edit", 62, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.42f
    );
    editSprite->setScale(0.68f);
    auto edit = CCMenuItemSpriteExtra::create(
        editSprite, this, menu_selector(GradientAnimationPopup::onCustomize)
    );
    edit->setPosition({374.f, 165.f});
    edit->setID("edit-custom-animation-button");
    menu->addChild(edit);

    for (size_t i = 0; i < kEffects.size(); ++i) {
        auto type = kEffects[i];
        auto sprite = ButtonSprite::create(
            GradientAnimationManager::nameFor(type), 70, true,
            "bigFont.fnt", "GJ_button_04.png", 24.f, 0.36f
        );
        sprite->setScale(0.72f);

        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(GradientAnimationPopup::onEffect)
        );
        button->setPosition({
            kEffectFirstX + kEffectStepX * static_cast<float>(i), 131.f
        });
        button->setTag(static_cast<int>(type));
        button->setID(fmt::format("animation-effect-{}", static_cast<int>(type)));
        menu->addChild(button);
        m_effectButtons.emplace_back(type, button);
    }

    m_descriptionLabel = addLabel(
        m_mainLayer, "", {215.f, 111.f}, 0.42f, "chatFont.fnt"
    );
    m_descriptionLabel->setOpacity(190);
    m_descriptionLabel->limitLabelWidth(380.f, 0.42f, 0.28f);

    auto speedTitle = addLabel(m_mainLayer, "SPEED", {30.f, 78.f}, 0.28f, "goldFont.fnt");
    speedTitle->setAnchorPoint({0.f, 0.5f});
    m_speedSlider = Slider::create(this, menu_selector(GradientAnimationPopup::onSpeed), 0.82f);
    m_speedSlider->setPosition({220.f, 78.f});
    m_mainLayer->addChild(m_speedSlider, 3);
    m_speedLabel = addLabel(m_mainLayer, "1.0x", {400.f, 78.f}, 0.32f);
    m_speedLabel->setAnchorPoint({1.f, 0.5f});

    auto intensityTitle = addLabel(
        m_mainLayer, "INTENSITY", {30.f, 48.f}, 0.28f, "goldFont.fnt"
    );
    intensityTitle->setAnchorPoint({0.f, 0.5f});
    m_intensitySlider = Slider::create(
        this, menu_selector(GradientAnimationPopup::onIntensity), 0.82f
    );
    m_intensitySlider->setPosition({220.f, 48.f});
    m_mainLayer->addChild(m_intensitySlider, 3);
    m_intensityLabel = addLabel(m_mainLayer, "60%", {400.f, 48.f}, 0.32f);
    m_intensityLabel->setAnchorPoint({1.f, 0.5f});

    auto note = addLabel(
        m_mainLayer, "Changes are saved automatically", {215.f, 27.f}, 0.38f, "chatFont.fnt"
    );
    note->setOpacity(150);

    for (size_t i = 0; i < kPreviewIcons.size(); ++i) {
        if (kPreviewIcons[i].type == initialType) {
            m_previewIndex = i;
            break;
        }
    }

    rebuildPreview();
    refreshControls();
    return true;
}

void GradientAnimationPopup::rebuildPreview() {
    m_previewHost->removeAllChildrenWithCleanup(true);

    auto size = m_previewHost->getContentSize();
    auto shadow = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
    shadow->setColor({0, 0, 0});
    shadow->setOpacity(55);
    shadow->setScaleX(1.35f);
    shadow->setScaleY(0.45f);
    shadow->setPosition({size.width / 2.f, 12.f});
    m_previewHost->addChild(shadow);

    auto const& preview = kPreviewIcons[m_previewIndex];
    auto previewIcon = GradientUtils::createIcon(preview.type, m_secondPlayer);
    GradientUtils::fitIcon(
        previewIcon, {54.f, 44.f}, {size.width / 2.f, size.height / 2.f + 2.f}
    );
    m_previewHost->addChild(previewIcon, 2);

    GradientUtils::applyGradient(
        previewIcon,
        GradientUtils::getGradient(preview.type, m_secondPlayer),
        false,
        m_secondPlayer,
        807
    );

    m_iconLabel->setString(
        fmt::format("{} - P{}", preview.name, m_secondPlayer ? 2 : 1).c_str()
    );
}

void GradientAnimationPopup::refreshControls() {
    auto& manager = GradientAnimationManager::get();
    auto const& config = manager.config();

    m_enabledToggle->toggle(manager.isEnabled());
    m_reverseToggle->toggle(config.reverse);
    m_speedSlider->setValue(speedToSlider(config.speed));
    m_speedSlider->updateBar();
    m_intensitySlider->setValue(config.intensity);
    m_intensitySlider->updateBar();

    refreshEffectSelection();
    refreshLabels();
}

void GradientAnimationPopup::refreshEffectSelection() {
    auto selected = GradientAnimationManager::get().config().type;
    for (auto const& [type, button] : m_effectButtons) {
        bool active = type == selected;
        button->setScale(active ? 1.08f : 0.94f);
        if (auto sprite = typeinfo_cast<ButtonSprite*>(button->getNormalImage())) {
            sprite->updateBGImage(active ? "GJ_button_01.png" : "GJ_button_04.png");
        }
    }

    m_descriptionLabel->setString(GradientAnimationManager::descriptionFor(selected));
    m_descriptionLabel->limitLabelWidth(380.f, 0.42f, 0.28f);
}

void GradientAnimationPopup::refreshLabels() {
    auto const& config = GradientAnimationManager::get().config();
    m_speedLabel->setString(fmt::format("{:.1f}x", config.speed).c_str());
    m_intensityLabel->setString(
        fmt::format("{}%", static_cast<int>(std::lround(config.intensity * 100.f))).c_str()
    );
}

void GradientAnimationPopup::onEffect(CCObject* sender) {
    auto button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;

    auto type = static_cast<GradientAnimationType>(button->getTag());

    GradientAnimationManager::get().setType(type);
    refreshEffectSelection();

    // Custom is the only effect that shows nothing until it's been built, so
    // picking it goes straight to the editor.
    if (type == GradientAnimationType::Custom) openCustomEditor();
}

void GradientAnimationPopup::onCustomize(CCObject*) {
    openCustomEditor();
}

void GradientAnimationPopup::openCustomEditor() {
    auto& manager = GradientAnimationManager::get();

    if (manager.config().type != GradientAnimationType::Custom) {
        manager.setType(GradientAnimationType::Custom);
        refreshEffectSelection();
    }

    // Both popups write the same config, so pull the sliders back in sync once
    // the editor closes.
    Ref<GradientAnimationPopup> self = this;
    auto popup = CustomAnimationPopup::create(
        kPreviewIcons[m_previewIndex].type,
        m_secondPlayer,
        [self] {
            if (self && self->getParent()) self->refreshControls();
        }
    );

    if (popup) popup->show();
}

void GradientAnimationPopup::onEnabled(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    GradientAnimationManager::get().setEnabled(!toggle->isToggled());
}

void GradientAnimationPopup::onReverse(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    GradientAnimationManager::get().setReverse(!toggle->isToggled());
}

void GradientAnimationPopup::onSpeed(CCObject* sender) {
    auto thumb = typeinfo_cast<SliderThumb*>(sender);
    if (!thumb) return;
    GradientAnimationManager::get().setSpeed(speedFromSlider(thumb->getValue()));
    refreshLabels();
}

void GradientAnimationPopup::onIntensity(CCObject* sender) {
    auto thumb = typeinfo_cast<SliderThumb*>(sender);
    if (!thumb) return;
    GradientAnimationManager::get().setIntensity(thumb->getValue());
    refreshLabels();
}

void GradientAnimationPopup::onPreviousIcon(CCObject*) {
    m_previewIndex = m_previewIndex == 0 ? kPreviewIcons.size() - 1 : m_previewIndex - 1;
    rebuildPreview();
}

void GradientAnimationPopup::onNextIcon(CCObject*) {
    m_previewIndex = (m_previewIndex + 1) % kPreviewIcons.size();
    rebuildPreview();
}

void GradientAnimationPopup::onReset(CCObject*) {
    GradientAnimationManager::get().reset();
    refreshControls();
}

} // namespace paimon::icon_gradients
