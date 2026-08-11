#include "ColorSelectLayer.hpp"
#include "GradientAnimationPopup.hpp"
#include "GradientLayer.hpp"
#include "LoadLayer.hpp"
#include "PointsLayer.hpp"

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

#include <Geode/loader/Dispatch.hpp>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

static GradientLayer* layer = nullptr;

$on_mod(Loaded) {

    listenForSettingChanges<bool>(kSettingEnabled, [](bool value) {
        if (layer) {
            layer->updateGarage();
        }
    });

    listenForSettingChanges<float>(kSettingPointScale, [](float value) {
        if (layer) {
            layer->updatePointScale(value);
        }
    });

    listenForSettingChanges<bool>(kSettingDisable2P, [](bool value) {
        if (layer) {
            if (value) {
                layer->updatePlayer(false);
            }

            layer->updatePlayerToggle();
            layer->updateGarage();
        }
    });

    listenForSettingChanges<bool>(kSettingSeparate2P, [](bool value) {
        if (layer) {
            if (!value) {
                layer->updatePlayer(false);
            }

            layer->updatePlayerToggle();
            layer->updateGarage();
        }
    });

    listenForSettingChanges<bool>(kSettingIncreaseTolerance, [](bool value) {
        if (layer) {
            layer->updatePlayer(false);
            layer->updatePlayerToggle();
            layer->updateGarage();
        }
    });

    MouseMoveEvent().listen([](int32_t, int32_t) {
        if (layer) {
            layer->updateHover();
        }
    }).leak();
}

GradientLayer::~GradientLayer() {
    updateGarage();

    layer = nullptr;
}

void GradientLayer::updateHover() {
    m_pointsLayer->updateHover(getMousePos());
}

void GradientLayer::updatePointOpacity(int value) {
    m_pointsLayer->updatePointOpacity(value);
}

void GradientLayer::updatePointScale(float value) {
    m_pointsLayer->updatePointScale(value);
}

void GradientLayer::updateGarage() {
    if (m_garage) {
        m_garage->updateGradient();
    }
}

void GradientLayer::updatePlayer(bool secondPlayer) {
    m_isSecondPlayer = secondPlayer;

    m_pointsLayer->setPlayerFrame(m_selectedButton->getType());

    for (IconButton* button : m_buttons) {
        button->setLocked(
            Mod::get()->hasSavedValue(GradientUtils::getConfigKey(button->getType(), m_isSecondPlayer)),
            true
        );
        button->updateSprite(m_isSecondPlayer);
    }

    load(m_selectedButton->getType(), m_currentColor, true, true, true);

    updateGlowToggle();
    updateWhiteToggle();
}

void GradientLayer::updateWhiteToggle() {
    SimplePlayer* icon = m_pointsLayer->getIcon();
    GJRobotSprite* otherSprite = nullptr;

    IconType iconType = m_selectedButton->getType();

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

    m_whiteColorToggle->setForceDisabled(!hasWhite);
}

void GradientLayer::updateColorToggles() {
    bool isGlowActive = GameManager::get()->getPlayerGlow();
    if (m_isSecondPlayer) {
        if (sdiEnabled()) {
            isGlowActive = sdiSaved<bool>("glow", false);
        }
    }

    if (
        (m_glowColorToggle->isSelected() && !isGlowActive)
        || (m_whiteColorToggle->isSelected() && !m_whiteColorToggle->isEnabled())
    ) {
        onColorToggle(m_mainColorToggle);

        Loader::get()->queueInMainThread([self = Ref(this)] {
            self->m_pointsLayer->selectFirst();
        });
    }
}

void GradientLayer::updateGlowToggle() {
    bool isGlowActive = GameManager::get()->getPlayerGlow();
    if (m_isSecondPlayer) {
        if (sdiEnabled()) {
            isGlowActive = sdiSaved<bool>("glow", false);
        }
    }

    updateColorToggles();

    m_glowColorToggle->setForceDisabled(!isGlowActive);
}

void GradientLayer::updatePlayerToggle() {
    Loader::get()->queueInMainThread([self = Ref(this)] {
        self->m_playerToggle->setVisible(GradientCache::is2PSeparate());
        self->m_playerToggle->toggle(false);
    });
}

bool GradientLayer::isSecondPlayer() {
    return m_isSecondPlayer;
}

void GradientLayer::pointMoved() {
    save();
    updateGradient(false, false, false, true);
}

void GradientLayer::pointSelected(CCNode* point) {
    ccColor3B color = static_cast<ColorNode*>(point)->getColor();

    m_picker->setColor(color);
    m_colorSelector->setColor(color, 0.15f);

}

void GradientLayer::pointReleased() {
    updateGarage();
}

void GradientLayer::colorSelected(const ccColor3B& color) {
    m_rInput->setString(std::to_string(color.r).c_str());
    m_gInput->setString(std::to_string(color.g).c_str());
    m_bInput->setString(std::to_string(color.b).c_str());

    textChanged(nullptr);
}

GradientLayer* GradientLayer::create() {
    GradientLayer* ret = new GradientLayer();

    if (ret->init()) {
        ret->autorelease();
        layer = ret;
        return ret;
    }

    delete ret;
    return nullptr;
}

void GradientLayer::updateGradient(bool force, bool all, bool transition, bool light) {
    m_currentConfig = GradientUtils::getSavedConfig(m_selectedButton->getType(), m_currentColor, m_isSecondPlayer);

    // Light updates run on every drag/color tick; only refresh what the user
    // is actively editing (preview + the active channel's toggle), leaving the
    // garage and the per-icon buttons out of the hot path.
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
        m_pointsLayer->updateGradient(gradient.main, ColorType::Main, force);
        m_pointsLayer->updateGradient(gradient.secondary, ColorType::Secondary, force);
        m_pointsLayer->updateGradient(gradient.glow, ColorType::Glow, force);
        m_pointsLayer->updateGradient(gradient.white, ColorType::White, force);
        m_pointsLayer->updateGradient(gradient.line, ColorType::Line, force);
    } else {
        m_pointsLayer->updateGradient(m_currentConfig, m_currentColor, force);
    }

    for (IconButton* button : m_buttons) {
        button->applyGradient(force, m_currentColor, transition, all, m_isSecondPlayer);
        button->setColor(m_currentColor, false, m_isSecondPlayer);
    }

    m_mainColorToggle->applyGradient(gradient.main, force, transition);
    m_secondaryColorToggle->applyGradient(gradient.secondary, force, transition);
    m_glowColorToggle->applyGradient(gradient.glow, force, transition);
    m_whiteColorToggle->applyGradient(gradient.white, force, transition);
    m_lineColorToggle->applyGradient(gradient.line, force, transition);
}

void GradientLayer::updateCountLabel() {
    m_countLabel->setString(fmt::format("{} / 24", m_pointsLayer->getPointCount()).c_str());
    m_countLabel->setOpacity(170);
}

void GradientLayer::updateUI() {
    m_currentConfig = GradientUtils::getSavedConfig(m_selectedButton->getType(), m_currentColor, m_isSecondPlayer);

    bool hasPoints = m_currentConfig.points.size() > 0;
    bool canAddPoints = m_currentConfig.points.size() < 24;

    m_addButton->setEnabled(canAddPoints);
    m_addButton->setOpacity(canAddPoints ? 255 : 140);

    m_removeButton->setEnabled(hasPoints);
    m_removeButton->setOpacity(hasPoints ? 255 : 140);

    m_copyButton->setEnabled(hasPoints);
    m_copyButton->setOpacity(hasPoints ? 255 : 140);

    m_saveButton->setEnabled(hasPoints);
    m_saveButton->setOpacity(hasPoints ? 255 : 140);

    m_hideToggle->setEnabled(hasPoints);
    m_hideToggle->setOpacity(hasPoints ? 255 : 110);

    m_pasteButton->setEnabled(!GradientCache::getCopiedConfig().points.empty());
    m_pasteButton->setOpacity(!GradientCache::getCopiedConfig().points.empty() ? 255 : 140);

    m_colorSelector->setEnabled(hasPoints);
    m_picker->setEnabled(hasPoints);
    m_rInput->setEnabled(hasPoints);
    m_gInput->setEnabled(hasPoints);
    m_bInput->setEnabled(hasPoints);

    m_pointsLayer->setPointsHidden(m_pointsHidden, 0.f);

    updateCountLabel();
    updateWhiteToggle();
}

void GradientLayer::onAddPoint(CCObject*) {
    GameManager* gm = GameManager::get();

    m_pointsLayer->addPoint();
    m_pointsLayer->selectLast();
    m_pointsLayer->getSelectedPoint()->flash();

    if (m_pointsHidden) {
        onHideToggle(nullptr);
        m_hideToggle->toggle(false);
    }

    ccColor3B color = GradientUtils::getPlayerColor(m_currentColor, m_isSecondPlayer);

    m_pointsLayer->getSelectedPoint()->setColor(color);
    m_picker->setColor(color);
    m_colorSelector->setColor(color, 0.15f);

    save();
    updateUI();
    updateGradient();
    updateCountLabel();
}

void GradientLayer::onRemovePoint(CCObject*) {
    m_pointsLayer->removeSelected();
    m_pointsLayer->selectLast();

    if (m_pointsLayer->getPointCount() <= 0 && m_pointsHidden) {
        onHideToggle(nullptr);
        m_hideToggle->toggle(false);
    }

    save();
    updateUI();
    updateGradient();
    updateCountLabel();
}

void GradientLayer::onAnimations(CCObject*) {
    if (auto popup = GradientAnimationPopup::create(m_selectedButton->getType(), m_isSecondPlayer)) {
        popup->show();
    }
}

void GradientLayer::onCopy(CCObject*) {
    GradientCache::setCopiedConfig({
        m_pointsLayer->getPoints(),
        m_currentConfig.isLinear
    });

    updateUI();
}

void GradientLayer::onPaste(CCObject*) {
    load(GradientCache::getCopiedConfig());
}

void GradientLayer::load(GradientConfig config) {
    if (config.points.empty()) return;

    save(config, m_currentColor);
    load(m_selectedButton->getType(), m_currentColor, true, true, true);
}

void GradientLayer::onSave(CCObject*) {
    m_currentConfig.points = m_pointsLayer->getPoints();

    if (GradientUtils::isGradientSaved(m_currentConfig)) {
        return Notification::create("Gradient is already saved", NotificationIcon::Error, 0.1f)->show();
    }

    GradientUtils::saveConfig(m_currentConfig, kSavedGradientsKey, "");

    Notification::create("Gradient saved", NotificationIcon::Success, 0.1f)->show();
}

void GradientLayer::onLoad(CCObject*) {
    LoadLayer::create(this)->show();
}

void GradientLayer::load(IconType type, ColorType colorType, bool force, bool all, bool transition) {
    GradientConfig previousConfig = m_currentConfig;

    m_currentConfig = GradientUtils::getSavedConfig(type, colorType, m_isSecondPlayer);

    m_pointsLayer->loadPoints(m_currentConfig, previousConfig != m_currentConfig && transition);

    for (IconButton* button : m_buttons) {
        if (type == button->getType()) {
            button->setSelected(true);
            break;
        }
    }

    updateUI();
    updateGradient(force, all, transition);

    m_linearToggle->toggle(m_currentConfig.isLinear);
    m_radialToggle->toggle(!m_currentConfig.isLinear);
    m_dotToggle->toggle(m_selectedButton->isLocked());
}

void GradientLayer::save() {
    m_currentConfig.points = m_pointsLayer->getPoints();
    save(m_currentConfig, m_currentColor);
}

void GradientLayer::save(GradientConfig config, ColorType colorType) {
    if (!m_selectedButton) return;

    IconType type = m_selectedButton->isLocked()
        ? m_selectedButton->getType()
        : static_cast<IconType>(-1);
    std::string id = GradientUtils::getConfigKey(type, m_isSecondPlayer);

    std::string color = "color" + std::to_string(colorType);

    GradientUtils::saveConfig(config, id, color);
}

void GradientLayer::onIconButton(CCObject* sender) {
    IconButton* button = static_cast<IconButton*>(sender);

    if (button == m_selectedButton) return;

    m_pointsLayer->setPlayerFrame(button->getType());
    m_dotToggle->toggle(button->isLocked());
    button->setSelected(true);

    if (m_selectedButton && m_selectedButton != button) {
        m_selectedButton->setSelected(false);
    }

    m_selectedButton = button;

    load(button->getType(), m_currentColor, true, true, true);

    GradientCache::setLastSelected(button->getType());

    updateColorToggles();
}

void GradientLayer::onTypeToggle(CCObject* sender) {
    CCMenuItemToggler* toggler = static_cast<CCMenuItemToggler*>(sender);

    bool isLinear = toggler == m_linearToggle;

    if (isLinear == m_currentConfig.isLinear) {
        return toggler->toggle(!toggler->isToggled());
    }

    m_linearToggle->toggle(false);
    m_radialToggle->toggle(false);

    m_currentConfig.isLinear = isLinear;

    Loader::get()->queueInMainThread([self = Ref(this)] {
        self->m_linearToggle->toggle(self->m_currentConfig.isLinear);
        self->m_radialToggle->toggle(!self->m_currentConfig.isLinear);
    });

    save();
    updateGradient(true, false, true);
}

void GradientLayer::onLockToggle(CCObject* sender) {
    if (!m_selectedButton) return;

    if (!m_selectedButton->isLocked()) {
        m_selectedButton->setLocked(!m_selectedButton->isLocked());

        save(GradientUtils::getSavedConfig(static_cast<IconType>(-1), ColorType::Main, m_isSecondPlayer), ColorType::Main);
        save(GradientUtils::getSavedConfig(static_cast<IconType>(-1), ColorType::Secondary, m_isSecondPlayer), ColorType::Secondary);
        save(GradientUtils::getSavedConfig(static_cast<IconType>(-1), ColorType::Glow, m_isSecondPlayer), ColorType::Glow);
        save(GradientUtils::getSavedConfig(static_cast<IconType>(-1), ColorType::White, m_isSecondPlayer), ColorType::White);
        save(GradientUtils::getSavedConfig(static_cast<IconType>(-1), ColorType::Line, m_isSecondPlayer), ColorType::Line);
    } else {
        std::string id = GradientUtils::getConfigKey(m_selectedButton->getType(), m_isSecondPlayer);

        if (Mod::get()->hasSavedValue(id)) {
            Mod::get()->getSaveContainer().erase(id);
        }

        bool locked = !m_selectedButton->isLocked();

        m_selectedButton->setLocked(locked);

        load(static_cast<IconType>(-1), m_currentColor, true, true, true);

        Loader::get()->queueInMainThread([self = Ref(this), locked] {
            self->m_dotToggle->toggle(locked);
        });
    }
}

void GradientLayer::onColorToggle(CCObject* sender) {
    ColorToggle* toggle = static_cast<ColorToggle*>(sender);

    if (!toggle->isEnabled()) return;

    if (toggle == m_mainColorToggle && m_mainColorToggle->isSelected()) return;
    if (toggle == m_secondaryColorToggle && m_secondaryColorToggle->isSelected()) return;
    if (toggle == m_glowColorToggle && m_glowColorToggle->isSelected()) return;
    if (toggle == m_whiteColorToggle && m_whiteColorToggle->isSelected()) return;
    if (toggle == m_lineColorToggle && m_lineColorToggle->isSelected()) return;

    m_mainColorToggle->setSelected(false);
    m_secondaryColorToggle->setSelected(false);
    m_glowColorToggle->setSelected(false);
    m_whiteColorToggle->setSelected(false);
    m_lineColorToggle->setSelected(false);

    toggle->setSelected(true);

    m_currentColor = toggle->getColorType();

    load(m_selectedButton->getType(), m_currentColor, true, true, true);
}

void GradientLayer::onColorSelector(CCObject*) {
    ColorSelectLayer::create(this)->show();
}

void GradientLayer::onPlayerToggle(PlayerToggle* toggle) {
    updatePlayer(toggle->isToggled());
}

void GradientLayer::onHideToggle(CCObject* sender) {
    m_pointsHidden = !m_hideToggle->isToggled();
    m_pointsLayer->setPointsHidden(m_pointsHidden, 0.15f);
}

void GradientLayer::textChanged(CCTextInputNode* input) {
    int r = numFromString<int>(m_rInput->getString()).unwrapOr(0);
    int g = numFromString<int>(m_gInput->getString()).unwrapOr(0);
    int b = numFromString<int>(m_bInput->getString()).unwrapOr(0);

    if (r > 255 || r < 0) return m_rInput->setString(std::to_string(m_picker->getColor().r).c_str());
    if (g > 255 || g < 0) return m_gInput->setString(std::to_string(m_picker->getColor().g).c_str());
    if (b > 255 || b < 0) return m_bInput->setString(std::to_string(m_picker->getColor().b).c_str());

    m_ignoreColorChange = true;

    ccColor3B color = ccc3(r, g, b);

    m_picker->setColor(color);
    m_colorSelector->setColor(color, 0.15f);

    m_ignoreColorChange = false;
}

void GradientLayer::colorValueChanged(ccColor3B color) {
    if (!m_ignoreColorChange) {
        m_rInput->setString(std::to_string(color.r).c_str());
        m_gInput->setString(std::to_string(color.g).c_str());
        m_bInput->setString(std::to_string(color.b).c_str());

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
        Loader::get()->queueInMainThread([self = Ref(this)] {
            self->m_dotToggle->toggle(!self->m_dotToggle->isToggled());
        });

        return onLockToggle(nullptr);
    }

    CCPoint move = {0, 0};
    float amount = Mod::get()->getSettingValue<float>(kSettingMoveStep);

    switch (key) {
        case enumKeyCodes::KEY_Up: move = ccp(0, amount); break;
        case enumKeyCodes::KEY_Down: move = ccp(0, -amount); break;
        case enumKeyCodes::KEY_Right: move = ccp(amount, 0); break;
        case enumKeyCodes::KEY_Left: move = ccp(-amount, 0); break;
        default: return;
    }

    m_pointsLayer->moveSelected(move);

    return FLAlertLayer::keyDown(key, timestamp);
}

void GradientLayer::scrollWheel(float y, float) {
    if (m_buttons.empty() || Mod::get()->getSettingValue<bool>(kSettingDisableKeys)) return;

    m_scroll = m_smoothScroll ? m_scroll + y : y;

    if (m_scroll < 12 && m_scroll > -12 && m_smoothScroll) return;

    int index = 0;

    for (IconButton* button : m_buttons) {
        if (button == m_selectedButton) {
            break;
        }

        index++;
    }

    index += m_scroll > 0 ? 1 : -1;

    m_scroll = 0;

    if (index >= static_cast<int>(m_buttons.size())) index = 0;
    if (index < 0) index = static_cast<int>(m_buttons.size()) - 1;

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

    auto addActionButton = [this](
        char const* text, char const* background, CCPoint position,
        SEL_MenuHandler callback, char const* id
    ) {
        auto sprite = ButtonSprite::create(
            text, 66, true, "bigFont.fnt", background, 24.f, 0.45f
        );
        sprite->setScale(0.7f);
        sprite->setCascadeOpacityEnabled(true);

        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setPosition(position);
        button->setCascadeOpacityEnabled(true);
        button->setID(id);
        m_buttonMenu->addChild(button);
        return button;
    };

    m_addButton = addActionButton(
        "Add", "GJ_button_01.png", {70.f, 29.f},
        menu_selector(GradientLayer::onAddPoint), "add-point-button"
    );
    m_removeButton = addActionButton(
        "Delete", "GJ_button_06.png", {130.f, 29.f},
        menu_selector(GradientLayer::onRemovePoint), "remove-point-button"
    );
    m_copyButton = addActionButton(
        "Copy", "GJ_button_04.png", {190.f, 29.f},
        menu_selector(GradientLayer::onCopy), "copy-gradient-button"
    );
    m_pasteButton = addActionButton(
        "Paste", "GJ_button_04.png", {250.f, 29.f},
        menu_selector(GradientLayer::onPaste), "paste-gradient-button"
    );
    m_saveButton = addActionButton(
        "Save", "GJ_button_01.png", {310.f, 29.f},
        menu_selector(GradientLayer::onSave), "save-gradient-button"
    );
    m_loadButton = addActionButton(
        "Load", "GJ_button_02.png", {370.f, 29.f},
        menu_selector(GradientLayer::onLoad), "load-gradient-button"
    );

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

    m_dotToggle = CCMenuItemToggler::create(
        CCSprite::createWithSpriteFrameName("GJ_lock_open_001.png"),
        CCSprite::createWithSpriteFrameName("GJ_lock_001.png"),
        this,
        menu_selector(GradientLayer::onLockToggle)
    );

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

    m_mainColorToggle->applyGradient(GradientUtils::getDefaultConfig(ColorType::Main, m_isSecondPlayer), true, false);
    m_secondaryColorToggle->applyGradient(GradientUtils::getDefaultConfig(ColorType::Secondary, m_isSecondPlayer), true, false);
    m_glowColorToggle->applyGradient(GradientUtils::getDefaultConfig(ColorType::Glow, m_isSecondPlayer), true, false);
    m_whiteColorToggle->applyGradient(GradientUtils::getDefaultConfig(ColorType::White, m_isSecondPlayer), true, false);
    m_lineColorToggle->applyGradient(GradientUtils::getDefaultConfig(ColorType::Line, m_isSecondPlayer), true, false);

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

    if (GradientCache::getLastSelected() != IconType::Cube) {
        Loader::get()->queueInMainThread([self = Ref(this)] {
            for (IconButton* button : self->m_buttons) {
                if (button->getType() == GradientCache::getLastSelected()) {
                    self->onIconButton(button);
                }
            }
        });
    }

    Loader::get()->queueInMainThread([self = Ref(this)] {
        self->m_pointsLayer->selectLast();
    });

    updateGlowToggle();

    runAction(CCSequence::create(
        CCDelayTime::create(0.1f),
        CallFuncExt::create([this] {
            if (CCTouchHandler* handler = CCTouchDispatcher::get()->findHandler(m_pointsLayer)) {
                CCTouchDispatcher::get()->setPriority(-1001, handler->getDelegate());
            }
        }),
        nullptr
    ));

    return true;
}
