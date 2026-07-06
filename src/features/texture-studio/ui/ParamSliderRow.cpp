#include "ParamSliderRow.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {
constexpr float kRowHeight = 24.f;
}  // namespace

ParamSliderRow* ParamSliderRow::create(std::string const& label,
                                       float minValue, float maxValue, float step,
                                       float initial, float width,
                                       ChangeCallback onChange,
                                       Formatter formatter) {
    auto* ret = new ParamSliderRow();
    if (ret->init(label, minValue, maxValue, step, initial, width,
                  std::move(onChange), std::move(formatter))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ParamSliderRow::init(std::string const& label,
                          float minValue, float maxValue, float step,
                          float initial, float width,
                          ChangeCallback onChange, Formatter formatter) {
    if (!CCNode::init()) return false;
    m_min       = minValue;
    m_max       = std::max(maxValue, minValue + 1e-6f);
    m_step      = step;
    m_value     = std::clamp(initial, m_min, m_max);
    m_onChange  = std::move(onChange);
    m_formatter = std::move(formatter);

    this->setContentSize({width, kRowHeight});
    this->setAnchorPoint({0.f, 0.5f});

    // Proportional layout so the row works from ~180 px side panels up to
    // full-width popups.
    float labelW = std::clamp(width * 0.34f, 56.f, 96.f);
    float valueW = std::clamp(width * 0.18f, 30.f, 46.f);

    if (auto* lbl = CCLabelBMFont::create(label.c_str(), "bigFont.fnt")) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->limitLabelWidth(labelW - 4.f, 0.32f, 0.12f);
        lbl->setColor({210, 210, 220});
        this->addChildAtPosition(lbl, Anchor::Left, {0.f, 0.f});
    }

    float sliderW = std::max(50.f, width - labelW - valueW - 8.f);
    // The stock groove is ~210 px at scale 1; pick the scale that fills the
    // available space without dwarfing the 24 px row.
    float scale = std::clamp(sliderW / 210.f, 0.3f, 0.62f);
    auto* slider = Slider::create(this, menu_selector(ParamSliderRow::onSlider), scale);
    if (slider) {
        slider->setPosition({labelW + sliderW * 0.5f, kRowHeight * 0.5f});
        slider->setValue((m_value - m_min) / (m_max - m_min));
        this->addChild(slider);
        m_slider = slider;
    }

    if (auto* val = CCLabelBMFont::create("", "bigFont.fnt")) {
        val->setAnchorPoint({1.f, 0.5f});
        val->setScale(0.3f);
        val->setColor({255, 220, 120});
        this->addChildAtPosition(val, Anchor::Right, {0.f, 0.f});
        m_valueLbl = val;
    }
    m_valueWidth = valueW;
    refreshLabel();

    return true;
}

float ParamSliderRow::snap(float v) const {
    if (m_step > 0.f) {
        v = m_min + std::round((v - m_min) / m_step) * m_step;
    }
    return std::clamp(v, m_min, m_max);
}

void ParamSliderRow::onSlider(CCObject*) {
    if (!m_slider) return;
    auto* thumb = m_slider->getThumb();
    if (!thumb) return;
    float t = std::clamp(thumb->getValue(), 0.f, 1.f);
    float v = snap(m_min + t * (m_max - m_min));
    if (std::fabs(v - m_value) < 1e-6f) return;
    m_value = v;
    refreshLabel();
    if (m_onChange) m_onChange(m_value);
}

void ParamSliderRow::setValue(float v) {
    m_value = snap(v);
    if (m_slider) {
        m_slider->setValue((m_value - m_min) / (m_max - m_min));
    }
    refreshLabel();
}

void ParamSliderRow::refreshLabel() {
    if (!m_valueLbl) return;
    std::string text;
    if (m_formatter) {
        text = m_formatter(m_value);
    } else if (m_step >= 1.f) {
        text = std::to_string(static_cast<int>(std::lround(m_value)));
    } else {
        text = fmt::format("{:.2f}", m_value);
    }
    m_valueLbl->setString(text.c_str());
    m_valueLbl->limitLabelWidth(m_valueWidth, 0.3f, 0.1f);
}

}  // namespace paimon::texture_studio
