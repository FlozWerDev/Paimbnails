#include "GradientAnimationPopup.hpp"

#include "CustomAnimationPopup.hpp"

#include "../GradientUtils.hpp"

#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/SliderThumb.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

namespace paimon::icon_gradients {

namespace {

constexpr float kSpeedMin = 0.1f;
constexpr float kSpeedMax = 4.f;

constexpr float kPopupW = 480.f;
constexpr float kPopupH = 320.f;

// one shader cache slot for the live preview, kept away from the editor's slot.
constexpr int kPreviewShaderTag = 807;

constexpr float kGridX = 236.f;
constexpr float kGridTop = 236.f;
constexpr float kGridW = 232.f;
constexpr float kGridH = 150.f;
constexpr int kGridCols = 2;
constexpr float kCardW = 110.f;
constexpr float kCardH = 42.f;
constexpr float kCardGapX = 8.f;
constexpr float kCardGapY = 8.f;

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

// recurse the preview doll and force the animation off on its gradient shaders.
void freezePreview(CCNode* node, bool freeze) {
    if (!node) return;
    if (auto sprite = typeinfo_cast<CCSprite*>(node)) {
        if (auto program = sprite->getShaderProgram()) {
            auto loc = glGetUniformLocation(program->getProgram(), "u_animType");
            if (loc >= 0) {
                program->use();
                if (freeze) glUniform1i(loc, 0);
            }
        }
    }
    if (auto children = node->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            freezePreview(static_cast<CCNode*>(children->objectAtIndex(i)), freeze);
        }
    }
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
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    m_secondPlayer = secondPlayer;

    setTitle("Gradient Animations", "goldFont.fnt", 0.72f, 18.f);
    setID("gradient-animation-popup"_spr);

    addInfoButton("Gradient Animations",
        "Pick a <cy>preset</c> from the grid to make your icon's gradient move. The "
        "<cy>stage</c> on the left shows it live - swap the icon type or player there. "
        "<cy>Speed</c>, <cy>Intensity</c>, <cy>Phase</c> and <cy>Reverse</c> shape it; "
        "<cg>Edit</c> opens the layer builder, <cg>Random</c> rolls a surprise, and "
        "<cy>Reset</c> restores the default. Everything saves on its own.");

    for (size_t i = 0; i < kPreviewIcons.size(); ++i) {
        if (kPreviewIcons[i].type == initialType) {
            m_previewIndex = i;
            break;
        }
    }

    buildPreviewStage();
    buildPresetGrid();
    buildParameters();

    rebuildPreview();
    refreshControls();
    return true;
}

void GradientAnimationPopup::buildPreviewStage() {
    auto stage = paimon::ui::makeInset({206.f, 150.f});
    stage->setPosition({12.f, 86.f});
    stage->setID("animation-preview-panel"_spr);
    m_mainLayer->addChild(stage);

    addLabel(m_mainLayer, "LIVE PREVIEW", {115.f, 224.f}, 0.3f, "goldFont.fnt");

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    // a soft disc behind the icon reads as a lit stage; it also tints with the
    // current preset's accent so the grid and preview feel connected.
    m_glow = CCSprite::createWithSpriteFrameName("d_circle_01_001.png");
    if (m_glow) {
        m_glow->setColor(paimon::ui::palette::gold);
        m_glow->setOpacity(70);
        m_glow->setScale(1.35f);
        m_glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        m_glow->setPosition({115.f, 168.f});
        m_mainLayer->addChild(m_glow, 1);
    }

    auto shadow = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
    shadow->setColor({0, 0, 0});
    shadow->setOpacity(80);
    shadow->setScaleX(1.5f);
    shadow->setScaleY(0.45f);
    shadow->setPosition({115.f, 138.f});
    m_mainLayer->addChild(shadow, 1);

    m_previewHost = CCNode::create();
    m_previewHost->setContentSize({120.f, 70.f});
    m_previewHost->setAnchorPoint({0.5f, 0.5f});
    m_previewHost->ignoreAnchorPointForPosition(false);
    m_previewHost->setPosition({115.f, 170.f});
    m_previewHost->setID("animation-preview-icon"_spr);
    m_mainLayer->addChild(m_previewHost, 3);

    auto arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    arrow->setScale(0.5f);
    auto previous = CCMenuItemSpriteExtra::create(
        arrow, this, menu_selector(GradientAnimationPopup::onPreviousIcon)
    );
    previous->setPosition({28.f, 170.f});
    previous->setID("previous-preview-icon"_spr);
    menu->addChild(previous);

    auto nextArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    nextArrow->setScale(0.5f);
    nextArrow->setFlipX(true);
    auto next = CCMenuItemSpriteExtra::create(
        nextArrow, this, menu_selector(GradientAnimationPopup::onNextIcon)
    );
    next->setPosition({202.f, 170.f});
    next->setID("next-preview-icon"_spr);
    menu->addChild(next);

    m_iconLabel = addLabel(m_mainLayer, "Cube - P1", {104.f, 132.f}, 0.3f);
    m_iconLabel->setAnchorPoint({0.f, 0.5f});

    auto swapSprite = CCSprite::createWithSpriteFrameName("GJ_replayBtn_001.png");
    swapSprite->setScale(0.42f);
    auto swap = CCMenuItemSpriteExtra::create(
        swapSprite, this, menu_selector(GradientAnimationPopup::onSwapPlayer)
    );
    swap->setPosition({188.f, 132.f});
    swap->setID("swap-player-button"_spr);
    menu->addChild(swap);

    auto playSprite = ButtonSprite::create(
        "Pause", 70, true, "bigFont.fnt", "GJ_button_02.png", 20.f, 0.4f
    );
    auto play = CCMenuItemSpriteExtra::create(
        playSprite, this, menu_selector(GradientAnimationPopup::onPlayPause)
    );
    play->setPosition({68.f, 104.f});
    play->setID("play-pause-button"_spr);
    menu->addChild(play);
    m_playSprite = playSprite;

    auto randomSprite = ButtonSprite::create(
        "Random", 70, true, "bigFont.fnt", "GJ_button_03.png", 20.f, 0.4f
    );
    auto random = CCMenuItemSpriteExtra::create(
        randomSprite, this, menu_selector(GradientAnimationPopup::onRandomize)
    );
    random->setPosition({158.f, 104.f});
    random->setID("randomize-button"_spr);
    menu->addChild(random);

    m_typeLabel = addLabel(m_mainLayer, "Flow", {115.f, 72.f}, 0.4f, "goldFont.fnt");

    m_descriptionLabel = addLabel(m_mainLayer, "", {115.f, 56.f}, 0.42f, "chatFont.fnt");
    m_descriptionLabel->setOpacity(195);
    m_descriptionLabel->limitLabelWidth(196.f, 0.42f, 0.26f);
}

void GradientAnimationPopup::buildPresetGrid() {
    auto panel = paimon::ui::makeInset({kGridW + 8.f, kGridH + 24.f});
    panel->setPosition({kGridX - 4.f, kGridTop - kGridH - 2.f});
    panel->setID("animation-presets-panel"_spr);
    m_mainLayer->addChild(panel);

    addLabel(m_mainLayer, "PRESETS", {kGridX + kGridW / 2.f, kGridTop + 10.f},
             0.3f, "goldFont.fnt");

    auto scroll = ScrollLayer::create({kGridW, kGridH});
    scroll->setPosition({kGridX, kGridTop - kGridH});
    scroll->setID("animation-presets-scroll"_spr);
    m_mainLayer->addChild(scroll);

    auto const& types = GradientAnimationManager::builtInTypes();
    int rows = (static_cast<int>(types.size()) + kGridCols - 1) / kGridCols;
    float contentH = std::max(kGridH, rows * (kCardH + kCardGapY) + kCardGapY);

    scroll->m_contentLayer->setContentSize({kGridW, contentH});

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kGridW, contentH});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    scroll->m_contentLayer->addChild(menu);

    for (size_t i = 0; i < types.size(); ++i) {
        auto type = types[i];
        int col = static_cast<int>(i) % kGridCols;
        int row = static_cast<int>(i) / kGridCols;

        auto card = CCNode::create();
        card->setContentSize({kCardW, kCardH});
        card->setAnchorPoint({0.5f, 0.5f});

        auto bg = paimon::ui::makeInset({kCardW, kCardH}, 90, paimon::ui::palette::ink);
        bg->setPosition({0.f, 0.f});
        card->addChild(bg, -1);

        auto name = CCLabelBMFont::create(
            GradientAnimationManager::nameFor(type), "bigFont.fnt"
        );
        name->setScale(0.4f);
        name->limitLabelWidth(kCardW - 14.f, 0.4f, 0.26f);
        name->setPosition({kCardW / 2.f, kCardH / 2.f});
        card->addChild(name);

        auto button = CCMenuItemExt::createSpriteExtra(
            card, [this, type](CCMenuItemSpriteExtra*) { pickType(type); }
        );
        button->setContentSize({kCardW, kCardH});
        button->setPosition({
            kCardGapX + kCardW / 2.f + col * (kCardW + kCardGapX),
            contentH - kCardGapY - kCardH / 2.f - row * (kCardH + kCardGapY),
        });
        menu->addChild(button);

        m_presetCards.emplace_back(type, card);
    }

    scroll->moveToTop();
}

void GradientAnimationPopup::buildParameters() {
    auto panel = paimon::ui::makeInset({206.f, 72.f});
    panel->setPosition({12.f, 10.f});
    panel->setID("animation-controls-panel"_spr);
    m_mainLayer->addChild(panel);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    auto speedTitle = addLabel(m_mainLayer, "SPEED", {24.f, 68.f}, 0.26f, "goldFont.fnt");
    speedTitle->setAnchorPoint({0.f, 0.5f});
    m_speedSlider = Slider::create(this, menu_selector(GradientAnimationPopup::onSpeed), 0.6f);
    m_speedSlider->setPosition({118.f, 68.f});
    m_mainLayer->addChild(m_speedSlider, 3);
    m_speedLabel = addLabel(m_mainLayer, "1.0x", {206.f, 68.f}, 0.3f);
    m_speedLabel->setAnchorPoint({1.f, 0.5f});

    auto intensityTitle = addLabel(m_mainLayer, "INTENSITY", {24.f, 44.f}, 0.26f, "goldFont.fnt");
    intensityTitle->setAnchorPoint({0.f, 0.5f});
    m_intensitySlider = Slider::create(
        this, menu_selector(GradientAnimationPopup::onIntensity), 0.6f
    );
    m_intensitySlider->setPosition({118.f, 44.f});
    m_mainLayer->addChild(m_intensitySlider, 3);
    m_intensityLabel = addLabel(m_mainLayer, "60%", {206.f, 44.f}, 0.3f);
    m_intensityLabel->setAnchorPoint({1.f, 0.5f});

    auto enabledLabel = addLabel(m_mainLayer, "On", {18.f, 22.f}, 0.3f);
    enabledLabel->setAnchorPoint({0.f, 0.5f});
    m_enabledToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(GradientAnimationPopup::onEnabled), 0.5f
    );
    m_enabledToggle->setPosition({48.f, 22.f});
    m_enabledToggle->setID("animation-enabled-toggle"_spr);
    menu->addChild(m_enabledToggle);

    auto reverseLabel = addLabel(m_mainLayer, "Reverse", {74.f, 22.f}, 0.3f);
    reverseLabel->setAnchorPoint({0.f, 0.5f});
    m_reverseToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(GradientAnimationPopup::onReverse), 0.5f
    );
    m_reverseToggle->setPosition({150.f, 22.f});
    m_reverseToggle->setID("animation-reverse-toggle"_spr);
    menu->addChild(m_reverseToggle);

    auto resetSprite = ButtonSprite::create(
        "Reset", 70, true, "bigFont.fnt", "GJ_button_04.png", 20.f, 0.42f
    );
    auto reset = CCMenuItemSpriteExtra::create(
        resetSprite, this, menu_selector(GradientAnimationPopup::onReset)
    );
    reset->setPosition({kGridX + 44.f, 24.f});
    reset->setID("reset-animation-button"_spr);
    menu->addChild(reset);

    auto editSprite = ButtonSprite::create(
        "Edit custom", 110, true, "bigFont.fnt", "GJ_button_01.png", 20.f, 0.42f
    );
    auto edit = CCMenuItemSpriteExtra::create(
        editSprite, this, menu_selector(GradientAnimationPopup::onCustomize)
    );
    edit->setPosition({kGridX + 150.f, 24.f});
    edit->setID("edit-custom-animation-button"_spr);
    menu->addChild(edit);

    auto note = addLabel(
        m_mainLayer, "Changes are saved automatically", {kGridX + kGridW / 2.f, 48.f},
        0.34f, "chatFont.fnt"
    );
    note->setOpacity(150);
}

void GradientAnimationPopup::rebuildPreview() {
    m_previewHost->removeAllChildrenWithCleanup(true);

    auto size = m_previewHost->getContentSize();
    auto const& preview = kPreviewIcons[m_previewIndex];

    auto previewIcon = GradientUtils::createIcon(preview.type, m_secondPlayer);
    GradientUtils::fitIcon(
        previewIcon, {74.f, 60.f}, {size.width / 2.f, size.height / 2.f}
    );
    m_previewHost->addChild(previewIcon, 2);

    GradientUtils::applyGradient(
        previewIcon,
        GradientUtils::getGradient(preview.type, m_secondPlayer),
        false,
        m_secondPlayer,
        kPreviewShaderTag
    );

    m_iconLabel->setString(
        fmt::format("{} - P{}", preview.name, m_secondPlayer ? 2 : 1).c_str()
    );

    applyPreviewAnimation();
}

void GradientAnimationPopup::applyPreviewAnimation() {
    if (m_paused) freezePreview(m_previewHost, true);
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

    refreshPresetSelection();
    refreshLabels();
}

void GradientAnimationPopup::refreshPresetSelection() {
    auto selected = GradientAnimationManager::get().config().type;
    for (auto const& [type, card] : m_presetCards) {
        bool active = type == selected;
        card->setScale(active ? 1.06f : 1.f);
        if (auto bg = typeinfo_cast<CCScale9Sprite*>(card->getChildren()->objectAtIndex(0))) {
            bg->setColor(active ? paimon::ui::palette::gold : paimon::ui::palette::ink);
            bg->setOpacity(active ? 120 : 90);
        }
    }

    m_typeLabel->setString(GradientAnimationManager::nameFor(selected));
    m_descriptionLabel->setString(GradientAnimationManager::descriptionFor(selected));
    m_descriptionLabel->limitLabelWidth(196.f, 0.42f, 0.26f);
}

void GradientAnimationPopup::refreshLabels() {
    auto const& config = GradientAnimationManager::get().config();
    m_speedLabel->setString(fmt::format("{:.1f}x", config.speed).c_str());
    m_intensityLabel->setString(
        fmt::format("{}%", static_cast<int>(std::lround(config.intensity * 100.f))).c_str()
    );
}

void GradientAnimationPopup::pickType(GradientAnimationType type) {
    GradientAnimationManager::get().setType(type);
    refreshPresetSelection();

    // custom shows nothing until built, so picking it jumps to the editor.
    if (type == GradientAnimationType::Custom) openCustomEditor();
}

void GradientAnimationPopup::onCustomize(CCObject*) {
    openCustomEditor();
}

void GradientAnimationPopup::openCustomEditor() {
    auto& manager = GradientAnimationManager::get();

    if (manager.config().type != GradientAnimationType::Custom) {
        manager.setType(GradientAnimationType::Custom);
        refreshPresetSelection();
    }

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

void GradientAnimationPopup::onSwapPlayer(CCObject*) {
    m_secondPlayer = !m_secondPlayer;
    rebuildPreview();
}

void GradientAnimationPopup::onPlayPause(CCObject*) {
    m_paused = !m_paused;
    if (m_playSprite) m_playSprite->setString(m_paused ? "Play" : "Pause");

    if (m_paused) {
        freezePreview(m_previewHost, true);
    } else {
        GradientAnimationManager::get().refreshPrograms();
    }
}

void GradientAnimationPopup::onRandomize(CCObject*) {
    GradientAnimationManager::get().randomize();
    refreshControls();
}

void GradientAnimationPopup::onReset(CCObject*) {
    GradientAnimationManager::get().reset();
    m_paused = false;
    if (m_playSprite) m_playSprite->setString("Pause");
    refreshControls();
}

} // namespace paimon::icon_gradients
