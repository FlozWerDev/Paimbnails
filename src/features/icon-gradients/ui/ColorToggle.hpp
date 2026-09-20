#pragma once
#include <Geode/Geode.hpp>
#include "../GradientTypes.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class ColorToggle : public CCMenuItemSpriteExtra {

private:

    // Owning editor and channel identity.
    GradientLayer* m_layer = nullptr;
    ColorType m_colorType = ColorType::Main;
    GradientConfig m_currentConfig;

    // Button artwork: swatch, crossfade sprite, selection check, dimmer.
    CCSprite* m_sprite = nullptr;
    CCSprite* m_secondSprite = nullptr;
    CCSprite* m_select = nullptr;
    CCSprite* m_dimSprite = nullptr;

    CCLabelBMFont* m_numberLabel = nullptr;

    // Sticky state.
    bool m_forceDisabled = false;
    bool m_isSelected = false;
    bool m_didForce = false;

    // Cache policy for painted sprites.
    bool m_shouldCache = true;

    bool init(CCObject*, SEL_MenuHandler, bool, float);

    // Size this button to its swatch, anchored bottom-left.
    void fitToSprite();
    // Selection check sprite shared by both init paths.
    void addSelectOverlay(float);
    // Empty slots preview the raw player color, filled ones stay white.
    ccColor3B slotColor(GradientConfig const&, bool) const;
    // Shade one of the button sprites with the toggle's channel.
    void paintSlot(CCSprite*, GradientConfig const&, bool) const;

    void onAnimationEnded();

public:

    static ColorToggle* create(CCObject*, SEL_MenuHandler, ColorType, GradientLayer*, bool = true, float = 1.f, bool = true);

    // State queries.
    bool isSelected() override;
    ColorType getColorType();
    CCSprite* getMainSprite();

    // Selection and enabled state.
    void setSelected(bool);
    void setEnabled(bool) override;
    void setForceDisabled(bool);

    // Appearance.
    void setColor(const ccColor3B&, float = 0.f);
    void applyGradient(GradientConfig, bool, bool);

};

} // namespace paimon::icon_gradients
