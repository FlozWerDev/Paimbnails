#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCControlColourPicker.h>

#include "../GradientTypes.hpp"
#include "../hooks/GradientGarageLayer.hpp"

#include "ColorNode.hpp"
#include "ColorPicker.hpp"
// Channel switch and icon buttons.
#include "ColorToggle.hpp"
#include "IconButton.hpp"
// Player switch and points canvas.
#include "PlayerToggle.hpp"
#include "PointsLayer.hpp"

namespace paimon::icon_gradients {

class GradientLayer : public Popup, public ColorPickerDelegate, public TextInputDelegate {

private:

    // Numeric channel fields.
    TextInput* m_rInput = nullptr;
    TextInput* m_gInput = nullptr;
    TextInput* m_bInput = nullptr;

    // Point add/remove bar.
    CCMenuItemSpriteExtra* m_addButton = nullptr;
    CCMenuItemSpriteExtra* m_removeButton = nullptr;
    // Point clipboard bar.
    CCMenuItemSpriteExtra* m_copyButton = nullptr;
    CCMenuItemSpriteExtra* m_pasteButton = nullptr;
    // Gradient library bar.
    CCMenuItemSpriteExtra* m_saveButton = nullptr;
    CCMenuItemSpriteExtra* m_loadButton = nullptr;

    // Shape and lock switches.
    CCMenuItemToggler* m_linearToggle = nullptr;
    CCMenuItemToggler* m_radialToggle = nullptr;
    CCMenuItemToggler* m_dotToggle = nullptr;
    // Points visibility switch.
    CCMenuItemToggler* m_hideToggle = nullptr;

    CCLabelBMFont* m_countLabel = nullptr;

    GradientGarageLayer* m_garage = nullptr;

    ColorPicker* m_picker = nullptr;

    // Channel toggles.
    ColorToggle* m_mainColorToggle = nullptr;
    ColorToggle* m_secondaryColorToggle = nullptr;
    ColorToggle* m_glowColorToggle = nullptr;
    // Detail toggles.
    ColorToggle* m_whiteColorToggle = nullptr;
    ColorToggle* m_lineColorToggle = nullptr;
    ColorToggle* m_colorSelector = nullptr;

    // Player switch and points canvas.
    PlayerToggle* m_playerToggle = nullptr;
    PointsLayer* m_pointsLayer = nullptr;

    // Focused button and icon roster.
    IconButton* m_selectedButton = nullptr;
    std::vector<IconButton*> m_buttons;

    // Saved overlays and working copy.
    std::unordered_map<IconType, std::vector<CCPoint>> m_iconPoints;
    GradientConfig m_currentConfig;
    ColorType m_currentColor = ColorType::Main;

    // Editor state.
    bool m_isSecondPlayer = false;
    bool m_ignoreColorChange = false;
    bool m_pointsHidden = false;
    // Scroll smoothing state.
    bool m_smoothScroll = false;
    float m_scroll = 0.f;

    // Lifecycle.
    ~GradientLayer();
    bool init() override;

    // Shape, lock and color switches.
    void onTypeToggle(CCObject*);
    void onImage(CCObject*);
    void onPointColor(CCObject*);
    void onLockToggle(CCObject*);
    // Channel and visibility switches.
    void onColorToggle(CCObject*);
    void onHideToggle(CCObject*);
    void onColorSelector(CCObject*);

    // Icon switching.
    void onIconButton(CCObject*);
    // Point editing.
    void onAddPoint(CCObject*);
    void onRemovePoint(CCObject*);
    void onAnimations(CCObject*);
    // Clipboard actions.
    void onCopy(CCObject*);
    void onPaste(CCObject*);
    // Saved gradient slots.
    void onSave(CCObject*);
    void onLoad(CCObject*);

    // Working-copy persistence.
    void load(IconType, ColorType, bool = false, bool = false, bool = false);
    void save(GradientConfig, ColorType);
    void save();

    void updateGradient(bool = false, bool = false, bool = false, bool = false);
    void updateCountLabel();
    void updateUI();

    // One icon button repainted for the active channel.
    void paintButton(IconButton*, bool, bool, bool);
    // The RGB fields share one writer.
    void setRGBInputs(ccColor3B);
    // Re-show hidden points before edits that need them visible.
    void unhidePoints();
    // Persist and repaint everything after a point edit.
    void refresh();

    // Picker and field input.
    void colorValueChanged(ccColor3B) override;
    void textChanged(CCTextInputNode*) override;
    // Navigation input.
    void keyDown(enumKeyCodes, double) override;
    void scrollWheel(float, float) override;

public:

    static GradientLayer* create();

    GradientGarageLayer* getGarage();

    void updateHover();
    void updatePointOpacity(int);
    void updatePointScale(float);

    // Garage sync and player switching.
    void updateGarage();
    void updatePlayer(bool);
    void updatePlayerToggle();
    // Toggle availability.
    void updateGlowToggle();
    void updateWhiteToggle();
    void updateColorToggles();

    // Active player query.
    bool isSecondPlayer();

    // Points-layer callbacks.
    void pointMoved();
    void pointSelected(CCNode*);
    void pointReleased();
    // Channel picking.
    void colorSelected(const ccColor3B&);

    void load(GradientConfig);

    void onPlayerToggle(PlayerToggle*);

};

} // namespace paimon::icon_gradients
