#pragma once

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

class Slider;

namespace paimon::texture_studio {

// Compact "label — slider — value" row used by the Texture Studio editors.
// Values are plain floats in [minValue, maxValue]; `step` snaps the slider
// (e.g. 1.0 for integer parameters, 0 for continuous).
class ParamSliderRow : public cocos2d::CCNode {
public:
    using ChangeCallback = std::function<void(float)>;
    // Turns the raw value into the text shown at the right (e.g. "35%").
    using Formatter = std::function<std::string(float)>;

    static ParamSliderRow* create(std::string const& label,
                                  float minValue, float maxValue, float step,
                                  float initial, float width,
                                  ChangeCallback onChange,
                                  Formatter formatter = nullptr);

    // Does NOT fire the callback.
    void setValue(float v);
    float value() const { return m_value; }

protected:
    bool init(std::string const& label,
              float minValue, float maxValue, float step,
              float initial, float width,
              ChangeCallback onChange, Formatter formatter);

private:
    void onSlider(cocos2d::CCObject*);
    void refreshLabel();
    float snap(float v) const;

    float m_min = 0.f;
    float m_max = 1.f;
    float m_step = 0.f;
    float m_value = 0.f;
    float m_valueWidth = 42.f;

    ChangeCallback m_onChange;
    Formatter      m_formatter;

    Slider*                 m_slider   = nullptr;
    cocos2d::CCLabelBMFont* m_valueLbl = nullptr;
};

}  // namespace paimon::texture_studio
