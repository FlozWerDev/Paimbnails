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
#include "ColorToggle.hpp"
#include "IconButton.hpp"
#include "PlayerToggle.hpp"
#include "PointsLayer.hpp"

namespace paimon::icon_gradients {

class GradientLayer : public PaimonPopup, public ColorPickerDelegate {

private:

    TextInput* m_rInput = nullptr;
    TextInput* m_gInput = nullptr;
    TextInput* m_bInput = nullptr;

    CCMenuItemSpriteExtra* m_addButton = nullptr;
    CCMenuItemSpriteExtra* m_removeButton = nullptr;
    CCMenuItemSpriteExtra* m_copyButton = nullptr;
    CCMenuItemSpriteExtra* m_pasteButton = nullptr;
    CCMenuItemSpriteExtra* m_saveButton = nullptr;
    CCMenuItemSpriteExtra* m_loadButton = nullptr;

    CCMenuItemToggler* m_linearToggle = nullptr;
    CCMenuItemToggler* m_radialToggle = nullptr;
    CCMenuItemToggler* m_dotToggle = nullptr;
    CCMenuItemToggler* m_hideToggle = nullptr;

    CCLabelBMFont* m_countLabel = nullptr;

    GradientGarageLayer* m_garage = nullptr;

    ColorPicker* m_picker = nullptr;

    ColorToggle* m_mainColorToggle = nullptr;
    ColorToggle* m_secondaryColorToggle = nullptr;
    ColorToggle* m_glowColorToggle = nullptr;
    ColorToggle* m_whiteColorToggle = nullptr;
    ColorToggle* m_lineColorToggle = nullptr;
    ColorToggle* m_colorSelector = nullptr;

    PlayerToggle* m_playerToggle = nullptr;
    CCLabelBMFont* m_playerCaption = nullptr;
    PointsLayer* m_pointsLayer = nullptr;

    IconButton* m_selectedButton = nullptr;
    std::vector<IconButton*> m_buttons;

    GradientConfig m_currentConfig;
    ColorType m_currentColor = ColorType::Main;

    bool m_isSecondPlayer = false;
    bool m_ignoreColorChange = false;
    bool m_pointsHidden = false;
    bool m_smoothScroll = false;
    float m_scroll = 0.f;

    ~GradientLayer();
    bool init() override;

    void onTypeToggle(CCObject*);
    void onImage(CCObject*);
    void onPointColor(CCObject*);
    void onLockToggle(CCObject*);
    void onColorToggle(CCObject*);
    void onHideToggle(CCObject*);
    void onColorSelector(CCObject*);

    void onIconButton(CCObject*);
    void onAddPoint(CCObject*);
    void onRemovePoint(CCObject*);
    void onAnimations(CCObject*);
    void onCopy(CCObject*);
    void onPaste(CCObject*);
    void onSave(CCObject*);
    void onLoad(CCObject*);

    void load(IconType type, ColorType colorType, bool force = false, bool all = false, bool transition = false);
    void save(GradientConfig config, ColorType colorType);
    void save();

    void updateGradient(bool force = false, bool all = false, bool transition = false, bool light = false);
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

    void colorValueChanged(ccColor3B) override;
    void onRGBInputChanged();
    void keyDown(enumKeyCodes, double) override;
    bool scrollWheel(float, float) override;

public:

    static GradientLayer* create();

    GradientGarageLayer* getGarage();

    void updateHover();
    void updatePointOpacity(int);
    void updatePointScale(float);

    void updateGarage();
    void updatePlayer(bool);
    void updatePlayerToggle();
    void updateGlowToggle();
    void updateWhiteToggle();
    void updateColorToggles();

    bool isSecondPlayer();

    void pointMoved();
    void pointSelected(CCNode*);
    void pointReleased();
    void colorSelected(const ccColor3B&);

    void load(GradientConfig);

    void onPlayerToggle(PlayerToggle*);

};

} // namespace paimon::icon_gradients
