#pragma once
//
// ColorPickerRow.hpp - Reusable UI component that presents three color
// swatches (Color 1, Color 2, Glow) plus a brightness slider, fitted into
// a single horizontal row. Used in ProjectEditorLayer and (potentially)
// in NewProjectPopup.
//
// Each swatch opens a Geode ColorPickPopup when clicked. Changes are
// reported back to the owner through a single callback. The owner is
// expected to update the underlying TextureProject and trigger any
// downstream effects (live preview, save, etc.).
//
// We keep the component completely stateless w.r.t. the project — it
// mirrors what the owner gives it and reports user edits, nothing else.
//

#include <Geode/Geode.hpp>

#include <functional>

class Slider;

namespace paimon::texture_studio {

// Snapshot of all colour-related state. Identifies which field changed
// in callbacks so the owner can update only what's needed.
struct ColorPickerRowState {
    cocos2d::ccColor3B color1{149, 226, 3};
    cocos2d::ccColor3B color2{28, 233, 255};
    cocos2d::ccColor3B colorGlow{255, 255, 255};
    int brightness = 160;          // 100..300; PackGen range
};

// Identifies which field changed.
enum class ColorField {
    Color1,
    Color2,
    Glow,
    Brightness,
};

class ColorPickerRow : public cocos2d::CCNode {
public:
    using ChangeCallback = std::function<void(ColorField, ColorPickerRowState const&)>;

    // Create the row with a starting state and a callback. The callback
    // receives the field that changed plus the new full state, so the
    // owner can decide whether to debounce / persist.
    //
    // Width controls the horizontal extent of the row; height is fixed
    // to ~40 pixels to fit GD's typical UI spacing.
    static ColorPickerRow* create(ColorPickerRowState const& initial,
                                  float width,
                                  ChangeCallback cb);

    // Update internal state from outside (e.g. when loading a slot).
    // Does NOT fire the callback.
    void setState(ColorPickerRowState const& state);

    ColorPickerRowState const& getState() const { return m_state; }

protected:
    bool init(ColorPickerRowState const& initial, float width, ChangeCallback cb);

private:
    ColorPickerRowState m_state;
    ChangeCallback m_callback;

    cocos2d::CCSprite*    m_swatch1    = nullptr;
    cocos2d::CCSprite*    m_swatch2    = nullptr;
    cocos2d::CCSprite*    m_swatchGlow = nullptr;
    cocos2d::CCLabelBMFont* m_brightnessValueLbl = nullptr;
    Slider*               m_brightnessSlider = nullptr;

    // Open the standard Geode color picker for one swatch.
    void openPickerFor(ColorField which);

    // Apply the brightness slider value to m_state.brightness.
    void onBrightnessSlider(cocos2d::CCObject* sender);
};

}  // namespace paimon::texture_studio
