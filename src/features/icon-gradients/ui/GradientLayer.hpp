#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCControlColourPicker.h>

#include "../GradientTypes.hpp"
#include "../hooks/GradientGarageLayer.hpp"

#include "ColorNode.hpp"
#include "ColorPicker.hpp"
// channel switch and icon buttons.
#include "ColorToggle.hpp"
#include "IconButton.hpp"
// player switch and points canvas.
#include "PlayerToggle.hpp"
#include "PointsLayer.hpp"

namespace paimon::icon_gradients {

class GradientLayer : public PaimonPopup, public ColorPickerDelegate, public TextInputDelegate {

private:

    // numeric channel fields.
    TextInput* m_rInput = nullptr;
    TextInput* m_gInput = nullptr;
    TextInput* m_bInput = nullptr;

    // point add/remove bar.
    CCMenuItemSpriteExtra* m_addButton = nullptr;
    CCMenuItemSpriteExtra* m_removeButton = nullptr;
    // point clipboard bar.
    CCMenuItemSpriteExtra* m_copyButton = nullptr;
    CCMenuItemSpriteExtra* m_pasteButton = nullptr;
    // gradient library bar.
    CCMenuItemSpriteExtra* m_saveButton = nullptr;
    CCMenuItemSpriteExtra* m_loadButton = nullptr;

    // shape and lock switches.
    CCMenuItemToggler* m_linearToggle = nullptr;
    CCMenuItemToggler* m_radialToggle = nullptr;
    CCMenuItemToggler* m_dotToggle = nullptr;
    // points visibility switch.
    CCMenuItemToggler* m_hideToggle = nullptr;

    CCLabelBMFont* m_countLabel = nullptr;

    GradientGarageLayer* m_garage = nullptr;

    ColorPicker* m_picker = nullptr;

    // channel toggles.
    ColorToggle* m_mainColorToggle = nullptr;
    ColorToggle* m_secondaryColorToggle = nullptr;
    ColorToggle* m_glowColorToggle = nullptr;
    // detail toggles.
    ColorToggle* m_whiteColorToggle = nullptr;
    ColorToggle* m_lineColorToggle = nullptr;
    ColorToggle* m_colorSelector = nullptr;

    // player switch and points canvas.
    PlayerToggle* m_playerToggle = nullptr;
    PointsLayer* m_pointsLayer = nullptr;

    // focused button and icon roster.
    IconButton* m_selectedButton = nullptr;
    std::vector<IconButton*> m_buttons;

    // saved overlays and working copy.
    GradientConfig m_currentConfig;
    ColorType m_currentColor = ColorType::Main;

    // editor state.
    bool m_isSecondPlayer = false;
    bool m_ignoreColorChange = false;
    bool m_pointsHidden = false;
    // scroll smoothing state.
    bool m_smoothScroll = false;
    float m_scroll = 0.f;

    // lifecycle.
    ~GradientLayer();
    bool init() override;

    // shape, lock and color switches.
    void onTypeToggle(CCObject*);
    void onImage(CCObject*);
    void onPointColor(CCObject*);
    void onLockToggle(CCObject*);
    // channel and visibility switches.
    void onColorToggle(CCObject*);
    void onHideToggle(CCObject*);
    void onColorSelector(CCObject*);

    // icon switching.
    void onIconButton(CCObject*);
    // point editing.
    void onAddPoint(CCObject*);
    void onRemovePoint(CCObject*);
    void onAnimations(CCObject*);
    // clipboard actions.
    void onCopy(CCObject*);
    void onPaste(CCObject*);
    // saved gradient slots.
    void onSave(CCObject*);
    void onLoad(CCObject*);

    // working-copy persistence.
    void load(IconType, ColorType, bool = false, bool = false, bool = false);
    void save(GradientConfig, ColorType);
    void save();

    void updateGradient(bool = false, bool = false, bool = false, bool = false);
    void updateCountLabel();
    void updateUI();

    // one icon button repainted for the active channel.
    void paintButton(IconButton*, bool, bool, bool);
    // the rgb fields share one writer.
    void setRGBInputs(ccColor3B);
    // re-show hidden points before edits that need them visible.
    void unhidePoints();
    // persist and repaint everything after a point edit.
    void refresh();

    // picker and field input.
    void colorValueChanged(ccColor3B) override;
    void textChanged(CCTextInputNode*) override;
    // navigation input.
    void keyDown(enumKeyCodes, double) override;
    void scrollWheel(float, float) override;

public:

    static GradientLayer* create();

    GradientGarageLayer* getGarage();

    void updateHover();
    void updatePointOpacity(int);
    void updatePointScale(float);

    // garage sync and player switching.
    void updateGarage();
    void updatePlayer(bool);
    void updatePlayerToggle();
    // toggle availability.
    void updateGlowToggle();
    void updateWhiteToggle();
    void updateColorToggles();

    // active player query.
    bool isSecondPlayer();

    // points-layer callbacks.
    void pointMoved();
    void pointSelected(CCNode*);
    void pointReleased();
    // channel picking.
    void colorSelected(const ccColor3B&);

    void load(GradientConfig);

    void onPlayerToggle(PlayerToggle*);

};

} // namespace paimon::icon_gradients
