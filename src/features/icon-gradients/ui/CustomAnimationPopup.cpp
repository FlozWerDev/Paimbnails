#include "CustomAnimationPopup.hpp"

#include "../GradientUtils.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

namespace paimon::icon_gradients {

namespace {

namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;

// Fixed band on top: the icon on the left, the layer stack on the right.
constexpr float kBandX = 12.f;
constexpr float kBandY = 190.f;
constexpr float kBandW = 416.f;
constexpr float kBandH = 72.f;

constexpr float kStackX = 112.f;
constexpr float kStackW = 312.f;
constexpr float kStackTop = 238.f;
constexpr float kStackStep = 14.f;
constexpr float kStackLineH = 13.f;

constexpr float kScrollX = 12.f;
constexpr float kScrollY = 14.f;
constexpr float kScrollW = 416.f;
constexpr float kScrollH = 168.f;

// The preview icon needs its own shader cache slot so it never shares uniforms
// with the one in GradientAnimationPopup.
constexpr int kPreviewShaderTag = 808;

constexpr ccColor3B kLayerAccent = {255, 165, 210};
constexpr ccColor3B kMasterAccent = {120, 210, 255};
constexpr ccColor3B kPresetAccent = {255, 210, 100};
constexpr ccColor3B kManageAccent = {170, 180, 200};

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

std::vector<std::string> motionNames() {
    std::vector<std::string> names;
    for (int i = 0; i < kGradientMotionCount; ++i) {
        names.emplace_back(GradientAnimationManager::nameFor(static_cast<GradientMotion>(i)));
    }
    return names;
}

std::vector<std::string> waveNames() {
    std::vector<std::string> names;
    for (int i = 0; i < kGradientWaveCount; ++i) {
        names.emplace_back(GradientAnimationManager::nameFor(static_cast<GradientWave>(i)));
    }
    return names;
}

std::string formatPercent(double value) {
    return fmt::format("{}%", static_cast<int>(std::lround(value * 100.0)));
}

std::string formatLayerSpeed(double value) {
    return fmt::format("x{:.2f}", value);
}

std::string formatMasterSpeed(double value) {
    return fmt::format("x{:.1f}", value);
}

} // namespace

CustomAnimationPopup* CustomAnimationPopup::create(
    IconType previewType, bool secondPlayer, std::function<void()> onChanged
) {
    auto ret = new CustomAnimationPopup();
    if (ret->init(previewType, secondPlayer, std::move(onChanged))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CustomAnimationPopup::init(
    IconType previewType, bool secondPlayer, std::function<void()> onChanged
) {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_onChanged = std::move(onChanged);
    m_secondPlayer = secondPlayer;

    setTitle("Custom Animation", "goldFont.fnt", 0.66f, 16.f);
    setID("custom-animation-popup");

    for (size_t i = 0; i < kPreviewIcons.size(); ++i) {
        if (kPreviewIcons[i].type == previewType) {
            m_previewIndex = i;
            break;
        }
    }

    // Starting from a blank stack shows nothing at all, which reads as broken.
    // One layer gives the user something to move around immediately.
    if (GradientAnimationManager::get().customLayers().empty()) {
        GradientAnimationManager::get().addCustomLayer();
    }

    buildPreview();
    rebuild();
    return true;
}

void CustomAnimationPopup::onClose(CCObject* sender) {
    if (m_onChanged) m_onChanged();
    Popup::onClose(sender);
}


void CustomAnimationPopup::buildPreview() {
    auto panel = paimon::SpriteHelper::createColorPanel(
        kBandW, kBandH, kit::kCardColor, kit::kCardAlpha, 7.f
    );
    if (panel) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({kBandX, kBandY});
        panel->setID("custom-animation-band");
        m_mainLayer->addChild(panel);
    }

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    m_previewHost = CCNode::create();
    m_previewHost->setContentSize({48.f, 42.f});
    m_previewHost->setAnchorPoint({0.5f, 0.5f});
    m_previewHost->ignoreAnchorPointForPosition(false);
    m_previewHost->setPosition({54.f, 230.f});
    m_previewHost->setID("custom-animation-preview");
    m_mainLayer->addChild(m_previewHost, 3);

    auto previousArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    previousArrow->setScale(0.34f);
    auto previous = CCMenuItemExt::createSpriteExtra(
        previousArrow, [this](CCMenuItemSpriteExtra*) {
            m_previewIndex = m_previewIndex == 0
                ? kPreviewIcons.size() - 1
                : m_previewIndex - 1;
            rebuildPreviewIcon();
        }
    );
    previous->setPosition({22.f, 230.f});
    menu->addChild(previous);

    auto nextArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    nextArrow->setScale(0.34f);
    nextArrow->setFlipX(true);
    auto next = CCMenuItemExt::createSpriteExtra(
        nextArrow, [this](CCMenuItemSpriteExtra*) {
            m_previewIndex = (m_previewIndex + 1) % kPreviewIcons.size();
            rebuildPreviewIcon();
        }
    );
    next->setPosition({86.f, 230.f});
    menu->addChild(next);

    m_iconLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_iconLabel->setScale(0.42f);
    m_iconLabel->setColor(kit::kDescColor);
    m_iconLabel->setPosition({54.f, 199.f});
    m_mainLayer->addChild(m_iconLabel, 3);

    auto divider = paimon::SpriteHelper::createColorPanel(1.f, kBandH - 16.f, {255, 255, 255}, 40, 0.5f);
    if (divider) {
        divider->setAnchorPoint({0.f, 0.f});
        divider->setPosition({102.f, kBandY + 8.f});
        m_mainLayer->addChild(divider, 2);
    }

    auto stackTitle = CCLabelBMFont::create("LAYER STACK", "goldFont.fnt");
    stackTitle->setScale(0.26f);
    stackTitle->setAnchorPoint({0.f, 0.5f});
    stackTitle->setPosition({kStackX + 2.f, 254.f});
    m_mainLayer->addChild(stackTitle, 3);

    m_stackHost = CCNode::create();
    m_stackHost->setPosition({0.f, 0.f});
    m_stackHost->setID("custom-animation-stack");
    m_mainLayer->addChild(m_stackHost, 4);

    rebuildPreviewIcon();
}

void CustomAnimationPopup::rebuildPreviewIcon() {
    m_previewHost->removeAllChildrenWithCleanup(true);

    auto const& preview = kPreviewIcons[m_previewIndex];
    auto size = m_previewHost->getContentSize();

    auto icon = GradientUtils::createIcon(preview.type, m_secondPlayer);
    GradientUtils::fitIcon(icon, {44.f, 38.f}, {size.width / 2.f, size.height / 2.f});
    m_previewHost->addChild(icon);

    GradientUtils::applyGradient(
        icon,
        GradientUtils::getGradient(preview.type, m_secondPlayer),
        false,
        m_secondPlayer,
        kPreviewShaderTag
    );

    m_iconLabel->setString(
        fmt::format("{} - P{}", preview.name, m_secondPlayer ? 2 : 1).c_str()
    );
}

void CustomAnimationPopup::rebuildStack() {
    if (!m_stackHost) return;

    m_stackHost->removeAllChildrenWithCleanup(true);

    auto const& layers = GradientAnimationManager::get().customLayers();

    if (layers.empty()) {
        auto empty = CCLabelBMFont::create(
            "No movements yet. Press Add below to make one.", "chatFont.fnt"
        );
        empty->setAnchorPoint({0.f, 0.5f});
        empty->limitLabelWidth(kStackW - 12.f, 0.44f, 0.2f);
        empty->setColor(kit::kDescColor);
        empty->setPosition({kStackX + 6.f, kStackTop});
        m_stackHost->addChild(empty);
        return;
    }

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    m_stackHost->addChild(menu, 5);

    for (size_t i = 0; i < layers.size(); ++i) {
        auto const& layer = layers[i];
        bool selected = i == m_selected;

        auto holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({kStackW, kStackLineH});

        auto background = paimon::SpriteHelper::createColorPanel(
            kStackW, kStackLineH,
            selected ? kLayerAccent : ccColor3B{0, 0, 0},
            selected ? 70 : 60,
            4.f
        );
        if (background) {
            background->setAnchorPoint({0.f, 0.f});
            background->setPosition({0.f, 0.f});
            holder->addChild(background, -1);
        }

        auto text = CCLabelBMFont::create(
            fmt::format(
                "{}. {} - {}   {}%   x{:.2f}",
                i + 1,
                GradientAnimationManager::nameFor(layer.motion),
                GradientAnimationManager::nameFor(layer.wave),
                static_cast<int>(std::lround(layer.amount * 100.f)),
                layer.speed
            ).c_str(),
            "chatFont.fnt"
        );
        text->setAnchorPoint({0.f, 0.5f});
        text->limitLabelWidth(kStackW - 16.f, 0.42f, 0.2f);
        text->setColor(selected ? ccColor3B{255, 255, 255} : kit::kDescColor);
        text->setPosition({8.f, kStackLineH / 2.f});
        holder->addChild(text);

        auto button = CCMenuItemExt::createSpriteExtra(
            holder, [this, i](CCMenuItemSpriteExtra*) { selectLayer(i); }
        );
        button->setPosition({
            kStackX + kStackW / 2.f,
            kStackTop - kStackStep * static_cast<float>(i),
        });
        menu->addChild(button);
    }
}


void CustomAnimationPopup::selectLayer(size_t index) {
    m_selected = index;
    scheduleRebuild();
}

GradientAnimationLayer CustomAnimationPopup::currentLayer() const {
    auto const& layers = GradientAnimationManager::get().customLayers();
    if (m_selected >= layers.size()) return {};
    return layers[m_selected];
}

void CustomAnimationPopup::writeLayer(GradientAnimationLayer const& layer) {
    GradientAnimationManager::get().updateCustomLayer(m_selected, layer);
}


CCNode* CustomAnimationPopup::makeDescriptionRow(
    float width, char const* text, CCLabelBMFont** out
) {
    constexpr float kRowH = 38.f;
    constexpr float kScale = 0.42f;

    auto row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto label = CCLabelBMFont::create(
        text, "chatFont.fnt", (width - 24.f) / kScale, kCCTextAlignmentLeft
    );
    label->setScale(kScale);
    label->setAnchorPoint({0.f, 1.f});
    label->setColor(kit::kDescColor);
    label->setPosition({12.f, kRowH - 4.f});
    row->addChild(label);

    if (out) *out = label;
    return row;
}

CCNode* CustomAnimationPopup::makeToolbar(float width) {
    constexpr float kRowH = 34.f;

    auto& manager = GradientAnimationManager::get();
    auto count = manager.customLayers().size();
    bool hasRoom = count < kMaxCustomLayers;
    bool hasLayer = count > 0;

    auto row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 5);

    struct Action {
        char const* label;
        bool available;
        std::function<void()> run;
    };

    std::array<Action, 5> actions = {
        Action{"Add", hasRoom, [this] {
            auto& mgr = GradientAnimationManager::get();
            if (mgr.addCustomLayer()) m_selected = mgr.customLayers().size() - 1;
            scheduleRebuild();
        }},
        Action{"Copy", hasRoom && hasLayer, [this] {
            auto& mgr = GradientAnimationManager::get();
            if (mgr.duplicateCustomLayer(m_selected)) m_selected += 1;
            scheduleRebuild();
        }},
        Action{"Up", hasLayer && m_selected > 0, [this] {
            m_selected = GradientAnimationManager::get().moveCustomLayer(m_selected, -1);
            scheduleRebuild();
        }},
        Action{"Down", hasLayer && m_selected + 1 < count, [this] {
            m_selected = GradientAnimationManager::get().moveCustomLayer(m_selected, 1);
            scheduleRebuild();
        }},
        Action{"Delete", hasLayer, [this] {
            auto& mgr = GradientAnimationManager::get();
            mgr.removeCustomLayer(m_selected);
            if (m_selected > 0) m_selected -= 1;
            scheduleRebuild();
        }},
    };

    float slot = width / static_cast<float>(actions.size());

    for (size_t i = 0; i < actions.size(); ++i) {
        auto const& action = actions[i];

        auto sprite = ButtonSprite::create(
            action.label, "bigFont.fnt",
            action.available ? "GJ_button_04.png" : "GJ_button_05.png", 0.7f
        );
        if (sprite) sprite->setScale(0.52f);

        auto button = CCMenuItemExt::createSpriteExtra(
            sprite, [run = action.run](CCMenuItemSpriteExtra*) { run(); }
        );
        button->setEnabled(action.available);
        button->setPosition({slot * (static_cast<float>(i) + 0.5f), kRowH / 2.f});
        menu->addChild(button);
    }

    return row;
}


void CustomAnimationPopup::scheduleRebuild() {
    Ref<CustomAnimationPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (self && self->getParent()) self->rebuild();
    });
}

void CustomAnimationPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }
    m_motionDesc = nullptr;
    m_waveDesc = nullptr;

    auto& manager = GradientAnimationManager::get();
    auto const& layers = manager.customLayers();

    if (layers.empty()) {
        m_selected = 0;
    } else if (m_selected >= layers.size()) {
        m_selected = layers.size() - 1;
    }

    float const innerW = kit::cardInnerWidth(kScrollW);

    std::vector<CCNode*> items;

    {
        std::vector<CCNode*> rows = {makeToolbar(innerW)};

        if (layers.empty()) {
            rows.push_back(kit::makeHint(innerW,
                "A custom animation is a stack of movements. Add the first one "
                "to start building."));
        } else {
            auto layer = layers[m_selected];

            rows.push_back(kit::makeSelectRow(innerW,
                "Movement", nullptr,
                motionNames(), static_cast<int>(layer.motion),
                [this](int index) {
                    auto edited = currentLayer();
                    edited.motion = static_cast<GradientMotion>(
                        std::clamp(index, 0, kGradientMotionCount - 1));
                    writeLayer(edited);
                    if (m_motionDesc) {
                        m_motionDesc->setString(
                            GradientAnimationManager::descriptionFor(edited.motion));
                    }
                    rebuildStack();
                }));
            rows.push_back(makeDescriptionRow(innerW,
                GradientAnimationManager::descriptionFor(layer.motion), &m_motionDesc));

            rows.push_back(kit::makeSelectRow(innerW,
                "Rhythm", nullptr,
                waveNames(), static_cast<int>(layer.wave),
                [this](int index) {
                    auto edited = currentLayer();
                    edited.wave = static_cast<GradientWave>(
                        std::clamp(index, 0, kGradientWaveCount - 1));
                    writeLayer(edited);
                    if (m_waveDesc) {
                        m_waveDesc->setString(
                            GradientAnimationManager::descriptionFor(edited.wave));
                    }
                    rebuildStack();
                }));
            rows.push_back(makeDescriptionRow(innerW,
                GradientAnimationManager::descriptionFor(layer.wave), &m_waveDesc));

            rows.push_back(kit::makeSliderRow(innerW,
                "Amount", "How far this movement pushes the colors.",
                layer.amount, 0.0, 1.0, formatPercent,
                [this](double value) {
                    auto edited = currentLayer();
                    edited.amount = static_cast<float>(value);
                    writeLayer(edited);
                    rebuildStack();
                }));

            rows.push_back(kit::makeSliderRow(innerW,
                "Speed", "How fast this layer runs, on top of the master speed.",
                layer.speed, kLayerSpeedMin, kLayerSpeedMax, formatLayerSpeed,
                [this](double value) {
                    auto edited = currentLayer();
                    edited.speed = static_cast<float>(value);
                    writeLayer(edited);
                    rebuildStack();
                }));

            rows.push_back(kit::makeSliderRow(innerW,
                "Start at", "Where in its cycle the layer begins. Offset two "
                            "layers so they take turns instead of moving together.",
                layer.phase, 0.0, 1.0, formatPercent,
                [this](double value) {
                    auto edited = currentLayer();
                    edited.phase = static_cast<float>(value);
                    writeLayer(edited);
                }));
        }

        std::string title = layers.empty()
            ? std::string("Layers")
            : fmt::format("Layer {} of {}", m_selected + 1, layers.size());

        items.push_back(kit::makeCard(kScrollW, title.c_str(), kLayerAccent, rows));
    }

    {
        auto const& config = manager.config();

        items.push_back(kit::makeCard(kScrollW, "Master", kMasterAccent, {
            kit::makeSliderRow(innerW,
                "Speed", "Multiplies the speed of every layer.",
                config.speed, 0.1, 4.0, formatMasterSpeed,
                [](double value) {
                    GradientAnimationManager::get().setSpeed(static_cast<float>(value));
                }),
            kit::makeSliderRow(innerW,
                "Intensity", "Multiplies the amount of every layer. At 0% nothing moves.",
                config.intensity, 0.0, 1.0, formatPercent,
                [](double value) {
                    GradientAnimationManager::get().setIntensity(static_cast<float>(value));
                }),
            kit::makeToggleRow(innerW,
                "Reverse", "Runs the whole animation backwards.",
                config.reverse,
                [](bool value) { GradientAnimationManager::get().setReverse(value); }),
        }));
    }

    {
        std::vector<CCNode*> rows;
        for (auto const& preset : GradientAnimationManager::customPresets()) {
            rows.push_back(kit::makeButtonRow(innerW,
                preset.name, preset.description, "Use",
                [this, recipe = preset.layers] {
                    GradientAnimationManager::get().setCustomLayers(recipe);
                    m_selected = 0;
                    scheduleRebuild();
                }));
        }

        items.push_back(kit::makeCard(kScrollW, "Recipes", kPresetAccent, rows));
        items.push_back(kit::makeHint(kScrollW,
            "A recipe replaces your stack. Load one, then edit it above to make "
            "it yours."));
    }

    items.push_back(kit::makeCard(kScrollW, "Manage", kManageAccent, {
        kit::makeButtonRow(innerW,
            "Clear stack", "Removes every layer so you can start from zero.",
            "Clear",
            [this] {
                GradientAnimationManager::get().clearCustomLayers();
                m_selected = 0;
                scheduleRebuild();
            }),
    }));

    items.push_back(kit::makeCard(kScrollW, "How it works", kManageAccent, {
        kit::makeHint(innerW,
            "Each layer takes the gradient and pushes it a little. Movement "
            "decides how it pushes, Rhythm decides when. Layers run from top to "
            "bottom, so layer 2 moves the result of layer 1 - that is how two "
            "simple movements turn into one complex animation."),
        kit::makeHint(innerW,
            "Tap a line in the stack above to pick the layer you are editing. "
            "Everything saves by itself and the icon on the left shows the real "
            "result while you drag."),
    }));

    m_scroll = kit::makeScrollStack({kScrollW, kScrollH}, items);
    m_scroll->setPosition({kScrollX, kScrollY});
    m_mainLayer->addChild(m_scroll);

    rebuildStack();
}

} // namespace paimon::icon_gradients
