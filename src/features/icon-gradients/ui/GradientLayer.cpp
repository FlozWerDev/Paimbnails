#include "ColorSelectLayer.hpp"
#include "GradientAnimationPopup.hpp"
#include "GradientLayer.hpp"
#include "LoadLayer.hpp"
#include "PointsLayer.hpp"

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"
#include "../services/GradientImage.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../smooth-scroll/services/SmoothScrollController.hpp"

#include <Geode/loader/Dispatch.hpp>
#include <Geode/ui/Layout.hpp>
#include <cmath>
#include <utility>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

static GradientLayer* layer = nullptr;

$on_mod(Loaded) {

    listenForSettingChanges<bool>(kSettingEnabled, [](bool) {
        if (layer) layer->updateGarage();
    });

    listenForSettingChanges<float>(kSettingPointScale, [](float value) {
        if (layer) layer->updatePointScale(value);
    });

    listenForSettingChanges<bool>(kSettingDisable2P, [](bool value) {
        if (!layer) return;

        if (value) layer->updatePlayer(false);

        layer->updatePlayerToggle();
        layer->updateGarage();
    });

    listenForSettingChanges<bool>(kSettingSeparate2P, [](bool value) {
        if (!layer) return;

        if (!value) layer->updatePlayer(false);

        layer->updatePlayerToggle();
        layer->updateGarage();
    });

    listenForSettingChanges<bool>(kSettingIncreaseTolerance, [](bool) {
        if (!layer) return;

        layer->updatePlayer(false); layer->updatePlayerToggle();
        layer->updateGarage();
    });

    MouseMoveEvent().listen([](int32_t, int32_t) {
        if (layer) layer->updateHover();
    }).leak();
}

GradientLayer::~GradientLayer() {
    updateGarage(); layer = nullptr;
}

void GradientLayer::updateHover() { m_pointsLayer->updateHover(getMousePos()); }

void GradientLayer::updatePointOpacity(int value) { m_pointsLayer->updatePointOpacity(value); }

void GradientLayer::updatePointScale(float value) { m_pointsLayer->updatePointScale(value); }

void GradientLayer::updateGarage() {
    if (m_garage) m_garage->updateGradient();
}

void GradientLayer::updatePlayer(bool secondPlayer) {
    m_isSecondPlayer = secondPlayer;

    m_pointsLayer->setPlayerFrame(m_selectedButton->getType());

    // Refresh every icon for the active player.
    for (IconButton* button : m_buttons) {
        // Locked state follows whatever each icon has saved.
        button->setLocked(Mod::get()->hasSavedValue(GradientUtils::getConfigKey(button->getType(), m_isSecondPlayer)), true);
        button->updateSprite(m_isSecondPlayer);
    }

    load(m_selectedButton->getType(), m_currentColor, true, true, true);

    updateGlowToggle(); updateWhiteToggle();
}

void GradientLayer::updateWhiteToggle() {
    SimplePlayer* icon = m_pointsLayer->getIcon(); GJRobotSprite* otherSprite = nullptr;

    IconType iconType = m_selectedButton->getType();

    // Only robots, spiders and detailed icons carry a white layer.
    bool hasWhite = false;

    if (iconType == IconType::Robot || iconType == IconType::Spider) {
        if (icon->m_robotSprite) {
            if (icon->m_robotSprite->isVisible()) otherSprite = icon->m_robotSprite;
        }
        if (icon->m_spiderSprite) {
            if (icon->m_spiderSprite->isVisible()) otherSprite = icon->m_spiderSprite;
        }

        if (otherSprite && otherSprite->m_extraSprite) {
            hasWhite = otherSprite->m_extraSprite->isVisible();
        }
    } else {
        if (icon->m_detailSprite) {
            hasWhite = icon->m_detailSprite->isVisible();
        }
    }

    // No white layer, no toggle.
    m_whiteColorToggle->setForceDisabled(!hasWhite);
}

void GradientLayer::updateColorToggles() {
    bool isGlowActive = GameManager::get()->getPlayerGlow();
    if (m_isSecondPlayer) {
        if (sdiEnabled()) {
            isGlowActive = sdiSaved<bool>("glow", false);
        }
    }

    bool glowStuck = m_glowColorToggle->isSelected() && !isGlowActive;
    bool whiteStuck = m_whiteColorToggle->isSelected() && !m_whiteColorToggle->isEnabled();

    if (glowStuck || whiteStuck) {
        onColorToggle(m_mainColorToggle);

        Loader::get()->queueInMainThread([self = Ref(this)] { self->m_pointsLayer->selectFirst(); });
    }
}

void GradientLayer::updateGlowToggle() {
    bool isGlowActive = GameManager::get()->getPlayerGlow();
    if (m_isSecondPlayer) {
        if (sdiEnabled()) {
            isGlowActive = sdiSaved<bool>("glow", false);
        }
    }

    updateColorToggles(); m_glowColorToggle->setForceDisabled(!isGlowActive);
}

void GradientLayer::updatePlayerToggle() {
    Loader::get()->queueInMainThread([self = Ref(this)] {
        self->m_playerToggle->setVisible(GradientCache::is2PSeparate()); self->m_playerToggle->toggle(false);
    });
}

bool GradientLayer::isSecondPlayer() { return m_isSecondPlayer; }

void GradientLayer::pointMoved() { save(); updateGradient(false, false, false, true); }

void GradientLayer::pointSelected(CCNode* point) {
    // Mirror the focused point across the picker and the selector.
    ccColor3B color = static_cast<ColorNode*>(point)->getColor();
    m_picker->setColor(color);
    m_colorSelector->setColor(color, 0.15f);
}

void GradientLayer::pointReleased() { updateGarage(); }

void GradientLayer::setRGBInputs(ccColor3B color) {
    // One writer for the three numeric fields.
    m_rInput->setString(std::to_string(color.r).c_str()); m_gInput->setString(std::to_string(color.g).c_str());
    m_bInput->setString(std::to_string(color.b).c_str());
}

void GradientLayer::colorSelected(const ccColor3B& color) {
    setRGBInputs(color);

    textChanged(nullptr);
}

GradientLayer* GradientLayer::create() {
    auto ret = new GradientLayer();

    if (!ret->init()) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    layer = ret;
    return ret;
}

// One icon button repainted for the active channel.
void GradientLayer::paintButton(IconButton* button, bool force, bool transition, bool all) {
    button->applyGradient(force, m_currentColor, transition, all, m_isSecondPlayer);
    button->setColor(m_currentColor, false, m_isSecondPlayer);
}

void GradientLayer::updateGradient(bool force, bool all, bool transition, bool light) {
    m_currentConfig = GradientUtils::getSavedConfig(m_selectedButton->getType(), m_currentColor, m_isSecondPlayer);

    // Drag ticks only refresh the preview and active toggle to keep the editor responsive.
    if (light) {
        m_pointsLayer->updateGradient(m_currentConfig, m_currentColor, force);

        switch (m_currentColor) {
            case ColorType::Main: m_mainColorToggle->applyGradient(m_currentConfig, force, transition); break;
            case ColorType::Secondary: m_secondaryColorToggle->applyGradient(m_currentConfig, force, transition); break;
            case ColorType::Glow: m_glowColorToggle->applyGradient(m_currentConfig, force, transition); break;
            case ColorType::White: m_whiteColorToggle->applyGradient(m_currentConfig, force, transition); break;
            case ColorType::Line: m_lineColorToggle->applyGradient(m_currentConfig, force, transition); break;
        }

        return;
    }

    updateGarage();

    Gradient gradient = GradientUtils::getGradient(m_selectedButton->getType(), m_isSecondPlayer);

    if (all) {
        // Every channel preview from its config slot.
        std::pair<ColorToggle*, GradientConfig const*> previews[] = {
            {m_mainColorToggle, &gradient.main},
            {m_secondaryColorToggle, &gradient.secondary},
            {m_glowColorToggle, &gradient.glow},
            {m_whiteColorToggle, &gradient.white},
            {m_lineColorToggle, &gradient.line},
        };

        for (auto const& [toggle, cfg] : previews) {
            m_pointsLayer->updateGradient(*cfg, toggle->getColorType(), force);
            toggle->applyGradient(*cfg, force, transition);
        }
    } else {
        m_pointsLayer->updateGradient(m_currentConfig, m_currentColor, force);
    }

    for (IconButton* button : m_buttons) paintButton(button, force, transition, all);
}

void GradientLayer::updateCountLabel() {
    m_countLabel->setString(fmt::format("{} / 24", m_pointsLayer->getPointCount()).c_str());
    m_countLabel->setOpacity(170);
}

void GradientLayer::updateUI() {
    m_currentConfig = GradientUtils::getSavedConfig(m_selectedButton->getType(), m_currentColor, m_isSecondPlayer);

    bool hasPoints = m_currentConfig.points.size() > 0;
    bool canAddPoints = m_currentConfig.points.size() < 24;
    bool canPaste = !GradientCache::getCopiedConfig().points.empty();

    // Each action mirrors its availability in its opacity.
    m_addButton->setEnabled(canAddPoints); m_addButton->setOpacity(canAddPoints ? 255 : 140);
    m_removeButton->setEnabled(hasPoints); m_removeButton->setOpacity(hasPoints ? 255 : 140);
    m_copyButton->setEnabled(hasPoints); m_copyButton->setOpacity(hasPoints ? 255 : 140);
    m_saveButton->setEnabled(hasPoints); m_saveButton->setOpacity(hasPoints ? 255 : 140);
    m_hideToggle->setEnabled(hasPoints); m_hideToggle->setOpacity(hasPoints ? 255 : 110);
    m_pasteButton->setEnabled(canPaste); m_pasteButton->setOpacity(canPaste ? 255 : 140);

    m_colorSelector->setEnabled(hasPoints);
    m_picker->setEnabled(hasPoints);
    // The RGB fields share one state.
    m_rInput->setEnabled(hasPoints); m_gInput->setEnabled(hasPoints); m_bInput->setEnabled(hasPoints);

    m_pointsLayer->setPointsHidden(m_pointsHidden, 0.f);

    updateCountLabel();
    updateWhiteToggle();
}

// Re-show hidden points before edits that need them visible.
void GradientLayer::unhidePoints() {
    if (!m_pointsHidden) return;

    onHideToggle(nullptr);
    m_hideToggle->toggle(false);
}

// Persist and repaint everything after a point edit.
void GradientLayer::refresh() {
    save(); updateUI();
    updateGradient(); updateCountLabel();
}

void GradientLayer::onAddPoint(CCObject*) {
    if (m_pointsLayer->getPointCount() >= 24) return;

    m_pointsLayer->addPoint();
    m_pointsLayer->selectLast();
    m_pointsLayer->getSelectedPoint()->flash();

    unhidePoints();

    ccColor3B color = GradientUtils::getPlayerColor(m_currentColor, m_isSecondPlayer);

    m_pointsLayer->getSelectedPoint()->setColor(color);
    m_picker->setColor(color);
    m_colorSelector->setColor(color, 0.15f);

    refresh();
}

void GradientLayer::onRemovePoint(CCObject*) {
    m_pointsLayer->removeSelected();
    m_pointsLayer->selectLast();

    if (m_pointsLayer->getPointCount() <= 0) unhidePoints();

    refresh();
}

void GradientLayer::onAnimations(CCObject*) {
    if (auto popup = GradientAnimationPopup::create(m_selectedButton->getType(), m_isSecondPlayer)) {
        popup->show();
    }
}

void GradientLayer::onCopy(CCObject*) {
    // Snapshot the live points plus the current shape.
    auto points = m_pointsLayer->getPoints();
    GradientCache::setCopiedConfig({std::move(points), m_currentConfig.isLinear});

    updateUI();
}

void GradientLayer::onPaste(CCObject*) {
    load(GradientCache::getCopiedConfig());
}

void GradientLayer::load(GradientConfig config) {
    // Empty drops carry nothing to apply.
    if (config.points.empty()) return;

    save(config, m_currentColor); load(m_selectedButton->getType(), m_currentColor, true, true, true);
}

void GradientLayer::onSave(CCObject*) {
    m_currentConfig.points = m_pointsLayer->getPoints();

    if (GradientUtils::isGradientSaved(m_currentConfig)) {
        return Notification::create("Gradient is already saved", NotificationIcon::Error, 0.1f)->show();
    }

    GradientUtils::saveConfig(m_currentConfig, kSavedGradientsKey, "");

    // Confirm the new library entry.
    auto toast = Notification::create("Gradient saved", NotificationIcon::Success, 0.1f);
    toast->show();
}

void GradientLayer::onLoad(CCObject*) { LoadLayer::create(this)->show(); }

void GradientLayer::load(IconType type, ColorType colorType, bool force, bool all, bool transition) {
    GradientConfig previousConfig = m_currentConfig;

    m_currentConfig = GradientUtils::getSavedConfig(type, colorType, m_isSecondPlayer);

    m_pointsLayer->loadPoints(m_currentConfig, previousConfig != m_currentConfig && transition);

    for (IconButton* button : m_buttons)
        if (type == button->getType()) { button->setSelected(true); break; }

    updateUI();
    updateGradient(force, all, transition);

    bool linear = m_currentConfig.isLinear;
    m_linearToggle->toggle(linear);
    m_radialToggle->toggle(!linear);
    m_dotToggle->toggle(m_selectedButton->isLocked());
}

void GradientLayer::save() {
    auto points = m_pointsLayer->getPoints();
    m_currentConfig.points = std::move(points);
    save(m_currentConfig, m_currentColor);
}

void GradientLayer::save(GradientConfig config, ColorType colorType) {
    if (!m_selectedButton) return;

    std::string id = GradientUtils::getConfigKey(m_selectedButton->getType(), m_isSecondPlayer);

    std::string color = "color" + std::to_string(colorType);

    GradientUtils::saveConfig(config, id, color);
}

void GradientLayer::onIconButton(CCObject* sender) {
    IconButton* button = static_cast<IconButton*>(sender);

    if (button == m_selectedButton) return;

    IconType type = button->getType();
    m_pointsLayer->setPlayerFrame(type);
    m_dotToggle->toggle(button->isLocked());
    button->setSelected(true);

    if (m_selectedButton && m_selectedButton != button) m_selectedButton->setSelected(false);

    m_selectedButton = button;

    load(type, m_currentColor, true, true, true);

    GradientCache::setLastSelected(type);

    updateColorToggles();
}

void GradientLayer::onTypeToggle(CCObject* sender) {
    // Linear/radial switch.
    CCMenuItemToggler* toggler = static_cast<CCMenuItemToggler*>(sender);

    bool isLinear = toggler == m_linearToggle;

    if (isLinear == m_currentConfig.isLinear) return toggler->toggle(!toggler->isToggled());

    m_linearToggle->toggle(false); m_radialToggle->toggle(false);

    m_currentConfig.isLinear = isLinear;

    Loader::get()->queueInMainThread([self = Ref(this)] {
        bool linear = self->m_currentConfig.isLinear;
        self->m_linearToggle->toggle(linear);
        self->m_radialToggle->toggle(!linear);
    });

    save();
    updateGradient(true, false, true);
}

void GradientLayer::onImage(CCObject*) {
    if (!m_pointsLayer->getSelectedPoint()) m_pointsLayer->selectLast();
    if (!m_pointsLayer->getSelectedPoint()) onAddPoint(nullptr);
    auto selected = m_pointsLayer->getSelectedPoint();
    if (!selected) return;
    WeakRef<GradientLayer> self = this;
    WeakRef<ColorNode> point = selected;
    auto type = m_selectedButton->getType();
    auto color = m_currentColor;
    auto secondPlayer = m_isSecondPlayer;
    pt::pickImage([self, point, type, color, secondPlayer](Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        auto node = point.lock();
        if (!popup || !node || node->getParent() != popup->m_pointsLayer) return;
        if (popup->m_selectedButton->getType() != type || popup->m_currentColor != color ||
            popup->m_isSecondPlayer != secondPlayer) return;
        if (result.isErr()) {
            Notification::create("Could not open image", NotificationIcon::Error)->show();
            return;
        }
        auto path = std::move(result).unwrapOr(std::nullopt);
        if (!path) return;
        auto imported = paimon::assets::importToBucket(*path, "icon-gradients", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            Notification::create("Could not import image", NotificationIcon::Error)->show();
            return;
        }
        auto imagePath = paimon::assets::normalizePathString(imported.path);
        // Validate the same decoder and atlas used by the renderer before saving.
        auto atlas = getGradientImageAtlas({SimplePoint{{0, 0}, ccWHITE, imagePath}});
        if (!atlas || !atlas->slots.contains(imagePath)) {
            Notification::create("Unsupported image. Try PNG or JPG", NotificationIcon::Error)->show();
            return;
        }
        node->setImagePath(imagePath);
        popup->save();
        popup->updateUI();
        popup->updateGradient(true, false, false);
        Notification::create("Image set for this point", NotificationIcon::Success)->show();
    });
}

void GradientLayer::onPointColor(CCObject*) {
    auto point = m_pointsLayer->getSelectedPoint();
    if (!point) return;
    point->setImagePath("");
    save();
    updateGradient(true, false, false);
}

void GradientLayer::onLockToggle(CCObject*) {
    if (!m_selectedButton) return;

    if (!m_selectedButton->isLocked()) {
        m_selectedButton->setLocked(true);
    } else {
        std::string id = GradientUtils::getConfigKey(m_selectedButton->getType(), m_isSecondPlayer);

        if (Mod::get()->hasSavedValue(id)) Mod::get()->getSaveContainer().erase(id);

        bool locked = !m_selectedButton->isLocked();

        m_selectedButton->setLocked(locked);

        load(m_selectedButton->getType(), m_currentColor, true, true, true);

        Loader::get()->queueInMainThread([self = Ref(this), locked] { self->m_dotToggle->toggle(locked); });
    }
}

void GradientLayer::onColorToggle(CCObject* sender) {
    ColorToggle* toggle = static_cast<ColorToggle*>(sender);

    if (!toggle->isEnabled()) return;

    ColorToggle* channels[] = {m_mainColorToggle, m_secondaryColorToggle, m_glowColorToggle, m_whiteColorToggle, m_lineColorToggle};

    for (ColorToggle* channel : channels)
        if (toggle == channel && channel->isSelected()) return;

    for (ColorToggle* channel : channels) channel->setSelected(false);

    // Focus the new channel.
    toggle->setSelected(true); m_currentColor = toggle->getColorType();

    load(m_selectedButton->getType(), m_currentColor, true, true, true);
}

void GradientLayer::onColorSelector(CCObject*) {
    // Standalone picker for the active channel.
    ColorSelectLayer::create(this)->show();
}

void GradientLayer::onPlayerToggle(PlayerToggle* toggle) { updatePlayer(toggle->isToggled()); }

void GradientLayer::onHideToggle(CCObject*) {
    bool hidden = !m_hideToggle->isToggled();
    m_pointsHidden = hidden;
    m_pointsLayer->setPointsHidden(hidden, 0.15f);
}

void GradientLayer::textChanged(CCTextInputNode*) {
    TextInput* inputs[] = {m_rInput, m_gInput, m_bInput};
    int rgb[3];
    for (int i = 0; i < 3; i++) rgb[i] = numFromString<int>(inputs[i]->getString()).unwrapOr(0);

    if (rgb[0] > 255 || rgb[0] < 0) return m_rInput->setString(std::to_string(m_picker->getColor().r).c_str());
    if (rgb[1] > 255 || rgb[1] < 0) return m_gInput->setString(std::to_string(m_picker->getColor().g).c_str());
    if (rgb[2] > 255 || rgb[2] < 0) return m_bInput->setString(std::to_string(m_picker->getColor().b).c_str());

    m_ignoreColorChange = true;

    ccColor3B color = ccc3(rgb[0], rgb[1], rgb[2]);

    m_picker->setColor(color); m_colorSelector->setColor(color, 0.15f);

    m_ignoreColorChange = false;
}

void GradientLayer::colorValueChanged(ccColor3B color) {
    if (!m_ignoreColorChange) {
        setRGBInputs(color);

        m_colorSelector->getMainSprite()->setColor(color);
    }

    if (ColorNode* point = m_pointsLayer->getSelectedPoint()) {
        point->setColor(color);
    }

    save();
    updateGradient(false, false, false, true);
}

void GradientLayer::keyDown(enumKeyCodes key, double timestamp) {
    if (key == enumKeyCodes::KEY_Escape) {
        return onClose(nullptr);
    }

    if (Mod::get()->getSettingValue<bool>(kSettingDisableKeys)) return;

    if (key == enumKeyCodes::KEY_Backspace) {
        return onRemovePoint(nullptr);
    }

    if (key == enumKeyCodes::KEY_Enter) {
        return onAddPoint(nullptr);
    }

    if (key == enumKeyCodes::KEY_One) {
        return onColorToggle(m_mainColorToggle);
    }

    if (key == enumKeyCodes::KEY_Two) {
        return onColorToggle(m_secondaryColorToggle);
    }

    if (key == enumKeyCodes::KEY_Three) {
        return onColorToggle(m_glowColorToggle);
    }

    if (key == enumKeyCodes::KEY_Four) {
        return onColorToggle(m_lineColorToggle);
    }

    if (key == cocos2d::enumKeyCodes::KEY_Five) {
        return onColorToggle(m_whiteColorToggle);
    }

    if (key == enumKeyCodes::KEY_C) {
        return onCopy(nullptr);
    }

    if (key == enumKeyCodes::KEY_V) {
        return onPaste(nullptr);
    }

    if (key == enumKeyCodes::KEY_S) {
        return onSave(nullptr);
    }

    if (key == enumKeyCodes::KEY_O) {
        return onLoad(nullptr);
    }

    if (key == enumKeyCodes::KEY_L) {
        Loader::get()->queueInMainThread([self = Ref(this)] { self->m_dotToggle->toggle(!self->m_dotToggle->isToggled()); });

        return onLockToggle(nullptr);
    }

    CCPoint move = {0, 0};
    float amount = Mod::get()->getSettingValue<float>(kSettingMoveStep);

    switch (key) {
        case enumKeyCodes::KEY_Up: move.setPoint(0, amount); break;
        case enumKeyCodes::KEY_Down: move.setPoint(0, -amount); break;
        case enumKeyCodes::KEY_Right: move.setPoint(amount, 0); break;
        case enumKeyCodes::KEY_Left: move.setPoint(-amount, 0); break;
        default: return;
    }

    m_pointsLayer->moveSelected(move);

    return FLAlertLayer::keyDown(key, timestamp);
}

void GradientLayer::scrollWheel(float y, float x) {
    if (m_buttons.empty() || Mod::get()->getSettingValue<bool>(kSettingDisableKeys)) return;

    auto& smooth = paimon::smoothscroll::SmoothScrollController::get();
    if (smooth.isReplaying()) {
        m_scroll += smooth.filteredWheelSteps(y, x);
        if (std::abs(m_scroll) < 0.999f) return;
    } else {
        m_scroll = m_smoothScroll ? m_scroll + y : y;
        if (m_scroll < 12.f && m_scroll > -12.f && m_smoothScroll) return;
    }

    int index = 0;

    for (IconButton* button : m_buttons) {
        if (button == m_selectedButton) {
            break;
        }

        index++;
    }

    float const direction = m_scroll > 0.f ? 1.f : -1.f;
    index += direction > 0.f ? 1 : -1;

    if (smooth.isReplaying()) {
        m_scroll -= direction;
        if (std::abs(m_scroll) < 0.001f) m_scroll = 0.f;
    } else {
        m_scroll = 0.f;
    }

    int count = static_cast<int>(m_buttons.size());
    if (index >= count) index = 0;
    if (index < 0) index = count - 1;

    onIconButton(m_buttons[index]);
}

bool GradientLayer::init() {
    if (!Popup::init(440, 300)) return false;

    Dispatch<CCNode*, CCRect>("timestepyt.gdneko/create-neko-rect").send(
        m_mainLayer, {264.f, 75.f, 314.f, 96.f}
    );

    setMouseEnabled(true);

    m_smoothScroll = Loader::get()->isModLoaded("prevter.smooth-scroll");

    if (sdiEnabled()) {
        m_isSecondPlayer = sdiSaved<bool>("2pselected", false) && GradientCache::is2PSeparate();
    }

    CCScene* scene = CCDirector::get()->getRunningScene();

    if (GJGarageLayer* garage = scene->getChildByType<GJGarageLayer>(0)) {
        m_garage = static_cast<GradientGarageLayer*>(garage);
    }

    setTitle("Icon Gradients", "goldFont.fnt", 0.72f, 18.f);

    auto addPanel = [this](CCPoint position, CCSize size, char const* id) {
        auto panel = CCScale9Sprite::create("square02b_001.png");
        panel->setContentSize(size);
        panel->setColor({0, 0, 0});
        panel->setOpacity(72);
        panel->setPosition(position);
        panel->setID(id);
        m_mainLayer->addChild(panel);
        return panel;
    };

    auto addCaption = [this](char const* text, CCPoint position, float scale = 0.34f) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(scale);
        label->setPosition(position);
        m_mainLayer->addChild(label, 2);
        return label;
    };

    auto const previewSize = CCSize{180.f, 134.f};
    auto const previewCenter = CCPoint{190.f, 193.f};
    auto const previewOrigin = CCPoint{100.f, 126.f};

    addPanel({52.f, 193.f}, {84.f, 134.f}, "icon-sidebar");
    addPanel(previewCenter, previewSize, "gradient-preview-panel");
    addPanel({358.f, 193.f}, {144.f, 134.f}, "color-editor-panel");
    addPanel({220.f, 84.f}, {420.f, 68.f}, "gradient-controls-panel");
    addPanel({220.f, 29.f}, {420.f, 34.f}, "gradient-actions-panel");

    addCaption("ICONS", {52.f, 250.f});
    addCaption("PREVIEW", {190.f, 250.f});
    addCaption("PLAYER", {46.f, 108.f}, 0.27f);
    addCaption("MODE", {158.f, 108.f}, 0.3f);
    addCaption("CHANNEL", {326.f, 108.f}, 0.3f);

    auto settingsSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    settingsSprite->setScale(0.58f);

    auto settingsButton = CCMenuItemSpriteExtra::create(
        settingsSprite, this, menu_selector(GradientLayer::onAnimations)
    );
    settingsButton->setPosition({418.f, 281.f});
    settingsButton->setID("animation-button");
    m_buttonMenu->addChild(settingsButton);

    auto* tabMenu = CCMenu::create();
    tabMenu->setContentSize({124.f, 30.f});
    tabMenu->setPosition({10.f, 266.f});
    tabMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center)->setDefaultScaleLimits(0.5f, 1.f));
    tabMenu->setID("point-mode-menu");
    m_mainLayer->addChild(tabMenu);

    auto addPointMode = [tabMenu, this](char const* title, SEL_MenuHandler callback, char const* id) {
        auto sprite = ButtonSprite::create(title, 54, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.42f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setID(id);
        tabMenu->addChild(button);
    };
    addPointMode("Image", menu_selector(GradientLayer::onImage), "point-image-button");
    addPointMode("Color", menu_selector(GradientLayer::onPointColor), "point-color-button");
    tabMenu->updateLayout();

    for (size_t i = 0; i < 9; ++i) {
        IconType type = static_cast<IconType>(i);

        IconButton* btn = IconButton::create(
            this, menu_selector(GradientLayer::onIconButton), type, m_isSecondPlayer
        );

        btn->setScale(0.78f);

        m_buttons.push_back(btn);

        float column = static_cast<float>(i % 3);
        float row = static_cast<float>(i / 3);
        btn->setPosition({27.f + 25.f * column, 222.f - 30.f * row});

        m_buttonMenu->addChild(btn);
    }

    m_selectedButton = m_buttons.front();

    m_pointsLayer = PointsLayer::create(previewSize, this, previewSize / 2.f);
    m_pointsLayer->setPosition(previewOrigin);
    m_pointsLayer->setID("gradient-points-layer");
    m_mainLayer->addChild(m_pointsLayer, 100);

    Loader::get()->queueInMainThread([self = Ref(this)] {
        if (CCTouchHandler* handler = CCTouchDispatcher::get()->findHandler(self->m_pointsLayer)) {
            CCTouchDispatcher::get()->setPriority(-1000, handler->getDelegate());
        }
    });

    m_picker = ColorPicker::create();
    m_picker->setScale(0.5f);
    m_picker->setPosition({358.f, 213.f});
    m_picker->setDelegate(this);
    m_picker->setID("color-picker");

    m_mainLayer->addChild(m_picker);

    auto addRGBInput = [this](char const* name, float x, TextInput*& input) {
        auto label = CCLabelBMFont::create(name, "bigFont.fnt");
        label->setOpacity(150);
        label->setScale(0.32f);
        label->setPosition({x, 166.f});
        m_mainLayer->addChild(label);

        input = TextInput::create(47.f, name);
        input->setScale(0.52f);
        input->setPosition({x, 145.f});
        input->setString("255");
        input->getInputNode()->setDelegate(this);
        input->getInputNode()->setAllowedChars("0123456789");
        m_mainLayer->addChild(input);
    };

    addRGBInput("R", 312.f, m_rInput);
    addRGBInput("G", 344.f, m_gInput);
    addRGBInput("B", 376.f, m_bInput);

    auto* actionsMenu = CCMenu::create();
    actionsMenu->setContentSize({420.f, 34.f});
    actionsMenu->setPosition({10.f, 12.f});
    actionsMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center)->setDefaultScaleLimits(0.5f, 1.f));
    actionsMenu->setID("gradient-actions-menu");
    m_mainLayer->addChild(actionsMenu);

    auto addActionButton = [actionsMenu, this](
        char const* text, int width, char const* background,
        SEL_MenuHandler callback, char const* id
    ) {
        auto sprite = ButtonSprite::create(
            text, width, true, "bigFont.fnt", background, 18.f, 0.40f
        );
        sprite->setCascadeOpacityEnabled(true);

        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setCascadeOpacityEnabled(true);
        button->setID(id);
        actionsMenu->addChild(button);
        return button;
    };

    m_addButton = addActionButton(
        "Add", 56, "GJ_button_01.png",
        menu_selector(GradientLayer::onAddPoint), "add-point-button"
    );
    m_removeButton = addActionButton(
        "Delete", 56, "GJ_button_06.png",
        menu_selector(GradientLayer::onRemovePoint), "remove-point-button"
    );
    m_copyButton = addActionButton(
        "Copy", 56, "GJ_button_04.png",
        menu_selector(GradientLayer::onCopy), "copy-gradient-button"
    );
    m_pasteButton = addActionButton(
        "Paste", 56, "GJ_button_04.png",
        menu_selector(GradientLayer::onPaste), "paste-gradient-button"
    );
    m_saveButton = addActionButton(
        "Save", 56, "GJ_button_01.png",
        menu_selector(GradientLayer::onSave), "save-gradient-button"
    );
    m_loadButton = addActionButton(
        "Load", 56, "GJ_button_02.png",
        menu_selector(GradientLayer::onLoad), "load-gradient-button"
    );
    actionsMenu->updateLayout();

    m_hideToggle = CCMenuItemToggler::create(
        CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
        CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
        this,
        menu_selector(GradientLayer::onHideToggle)
    );
    m_hideToggle->setPosition({211.f, 84.f});
    m_hideToggle->setScale(0.44f);
    m_hideToggle->setCascadeOpacityEnabled(true);
    m_hideToggle->setID("hide-points-toggle");

    m_buttonMenu->addChild(m_hideToggle);

    m_playerToggle = PlayerToggle::create(this);
    m_playerToggle->setPosition({46.f, 84.f});
    m_playerToggle->setVisible(GradientCache::is2PSeparate());
    m_playerToggle->toggle(m_isSecondPlayer);
    m_playerToggle->setID("player-toggle");

    m_buttonMenu->addChild(m_playerToggle);
    m_linearToggle = GradientUtils::createTypeToggle(
        false, {106.f, 84.f}, this, menu_selector(GradientLayer::onTypeToggle)
    );
    m_linearToggle->setID("linear-gradient-toggle");
    m_buttonMenu->addChild(m_linearToggle);

    m_radialToggle = GradientUtils::createTypeToggle(
        true, {149.f, 84.f}, this, menu_selector(GradientLayer::onTypeToggle)
    );
    m_radialToggle->setID("radial-gradient-toggle");
    m_buttonMenu->addChild(m_radialToggle);

    addCaption("LINEAR", {106.f, 64.f}, 0.22f);
    addCaption("RADIAL", {149.f, 64.f}, 0.22f);
    addCaption("LOCK", {187.f, 64.f}, 0.22f);
    addCaption("HIDE", {211.f, 64.f}, 0.22f);

    m_countLabel = CCLabelBMFont::create("0 / 24", "chatFont.fnt");
    m_countLabel->setOpacity(170);
    m_countLabel->setScale(0.38f);
    m_countLabel->setAnchorPoint({1.f, 0.5f});
    m_countLabel->setPosition({276.f, 250.f});

    m_mainLayer->addChild(m_countLabel);

    auto lockOpen = CCSprite::createWithSpriteFrameName("GJ_lock_open_001.png");
    auto lockShut = CCSprite::createWithSpriteFrameName("GJ_lock_001.png");
    m_dotToggle = CCMenuItemToggler::create(lockOpen, lockShut, this, menu_selector(GradientLayer::onLockToggle));

    m_dotToggle->setScale(0.48f);
    m_dotToggle->setPosition({187.f, 84.f});
    m_dotToggle->setID("per-icon-toggle");

    m_buttonMenu->addChild(m_dotToggle);

    m_mainColorToggle = ColorToggle::create(this, menu_selector(GradientLayer::onColorToggle), ColorType::Main, this);
    m_mainColorToggle->setPosition({248.f, 84.f});

    m_buttonMenu->addChild(m_mainColorToggle);

    m_secondaryColorToggle = ColorToggle::create(this, menu_selector(GradientLayer::onColorToggle), ColorType::Secondary, this);
    m_secondaryColorToggle->setPosition({287.f, 84.f});

    m_buttonMenu->addChild(m_secondaryColorToggle);

    m_glowColorToggle = ColorToggle::create(this, menu_selector(GradientLayer::onColorToggle), ColorType::Glow, this);
    m_glowColorToggle->setPosition({326.f, 84.f});

    m_buttonMenu->addChild(m_glowColorToggle);

    m_whiteColorToggle = ColorToggle::create(this, menu_selector(GradientLayer::onColorToggle), ColorType::White, this);
    m_whiteColorToggle->setPosition({404.f, 84.f});

    m_buttonMenu->addChild(m_whiteColorToggle);

    m_lineColorToggle = ColorToggle::create(this, menu_selector(GradientLayer::onColorToggle), ColorType::Line, this);
    m_lineColorToggle->setPosition({365.f, 84.f});

    m_buttonMenu->addChild(m_lineColorToggle);

    // First paint from the per-channel defaults.
    std::pair<ColorToggle*, GradientConfig> defaults[] = {
        {m_mainColorToggle, GradientUtils::getDefaultConfig(ColorType::Main, m_isSecondPlayer)},
        {m_secondaryColorToggle, GradientUtils::getDefaultConfig(ColorType::Secondary, m_isSecondPlayer)},
        {m_glowColorToggle, GradientUtils::getDefaultConfig(ColorType::Glow, m_isSecondPlayer)},
        {m_whiteColorToggle, GradientUtils::getDefaultConfig(ColorType::White, m_isSecondPlayer)},
        {m_lineColorToggle, GradientUtils::getDefaultConfig(ColorType::Line, m_isSecondPlayer)},
    };

    for (auto& [toggle, cfg] : defaults) toggle->applyGradient(std::move(cfg), true, false);

    m_colorSelector = ColorToggle::create(
        this, menu_selector(GradientLayer::onColorSelector), ColorType::Main, this, false
    );
    m_colorSelector->setPosition({408.f, 145.f});
    m_colorSelector->setID("selected-color-button");

    m_buttonMenu->addChild(m_colorSelector);
    addCaption("PICK", {408.f, 166.f}, 0.25f);

    for (IconButton* button : m_buttons) {
        button->setLocked(
            Mod::get()->hasSavedValue(GradientUtils::getConfigKey(button->getType(), m_isSecondPlayer)),
            true
        );
    }

    m_mainColorToggle->setSelected(true);

    load(IconType::Cube, ColorType::Main, true, true);

    IconType last = GradientCache::getLastSelected();

    if (last != IconType::Cube) {
        Loader::get()->queueInMainThread([self = Ref(this), last] {
            for (IconButton* button : self->m_buttons)
                if (button->getType() == last) { self->onIconButton(button); }
        });
    }

    Loader::get()->queueInMainThread([self = Ref(this)] { self->m_pointsLayer->selectLast(); });

    updateGlowToggle();

    // The points layer must win touch priority over the garage below.
    auto dispatcher = CCTouchDispatcher::get();
    runAction(CCSequence::create(
        CCDelayTime::create(0.1f),
        CallFuncExt::create([this, dispatcher] {
            if (CCTouchHandler* handler = dispatcher->findHandler(m_pointsLayer))
                dispatcher->setPriority(-1001, handler->getDelegate());
        }),
        nullptr
    ));

    return true;
}
