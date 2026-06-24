#include "ColorPickerRow.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Build a simple square sprite as a colour swatch. Uses the white square
// shipped with GD; setColor() does the rest.
CCSprite* makeSwatch(ccColor3B color, float size) {
    auto* spr = CCSprite::create("square.png");
    if (!spr) {
        // Fall back to a known-good frame.
        spr = CCSprite::createWithSpriteFrameName("GJ_button_05.png");
    }
    if (spr) {
        // Scale to the desired pixel size.
        auto sz = spr->getContentSize();
        if (sz.width > 0 && sz.height > 0) {
            spr->setScale(size / std::max(sz.width, sz.height));
        }
        spr->setColor(color);
    }
    return spr;
}

}  // anonymous namespace

ColorPickerRow* ColorPickerRow::create(ColorPickerRowState const& initial,
                                       float width,
                                       ChangeCallback cb) {
    auto* ret = new ColorPickerRow();
    if (ret->init(initial, width, std::move(cb))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ColorPickerRow::init(ColorPickerRowState const& initial, float width, ChangeCallback cb) {
    if (!CCNode::init()) return false;
    m_state    = initial;
    m_callback = std::move(cb);
    this->setContentSize({width, 40.f});

    // Single CCMenu hosts the three swatch buttons + the slider thumb is
    // its own node added directly to `this`.
    auto* menu = CCMenu::create();
    if (!menu) return false;
    menu->setPosition({0, 0});
    menu->setContentSize({width, 40.f});

    constexpr float kSwatchSize = 28.f;
    constexpr float kSpacing    = 6.f;
    constexpr float kStartX     = 8.f;

    auto makeButton = [&](float x, ColorField field, ccColor3B color, CCSprite*& outSwatch) {
        auto* swatch = makeSwatch(color, kSwatchSize);
        if (!swatch) return;
        auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
            [this, field](CCMenuItemSpriteExtra*) {
                this->openPickerFor(field);
            });
        if (!btn) return;
        outSwatch = swatch;
        btn->setPosition({x + kSwatchSize / 2.f, 20.f});
        menu->addChild(btn);
    };

    makeButton(kStartX,                                    ColorField::Color1, m_state.color1,    m_swatch1);
    makeButton(kStartX + (kSwatchSize + kSpacing),         ColorField::Color2, m_state.color2,    m_swatch2);
    makeButton(kStartX + (kSwatchSize + kSpacing) * 2.f,   ColorField::Glow,   m_state.colorGlow, m_swatchGlow);

    this->addChild(menu);

    // Brightness slider on the right. We use the standard GD slider sized
    // to the remaining width minus a value label.
    float sliderStartX = kStartX + (kSwatchSize + kSpacing) * 3.f + 12.f;
    float valueLblWidth = 50.f;
    float sliderWidth = std::max(80.f, width - sliderStartX - valueLblWidth - kStartX);

    auto* slider = Slider::create(this, menu_selector(ColorPickerRow::onBrightnessSlider), 0.6f);
    if (slider) {
        slider->setPosition({sliderStartX + sliderWidth / 2.f, 20.f});
        // Map brightness 100..300 → slider value 0..1.
        float v = (m_state.brightness - 100) / 200.f;
        v = std::clamp(v, 0.f, 1.f);
        slider->setValue(v);
        this->addChild(slider);
        m_brightnessSlider = slider;
    }

    if (auto* lbl = CCLabelBMFont::create(
            std::to_string(m_state.brightness).c_str(), "bigFont.fnt")) {
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({sliderStartX + sliderWidth + 6.f, 20.f});
        this->addChild(lbl);
        m_brightnessValueLbl = lbl;
    }

    return true;
}

// Forward-declared inside the class; we attach behaviour via a regular
// member here. Note: menu_selector requires non-virtual member functions
// returning void with one CCObject* argument.
void ColorPickerRow::openPickerFor(ColorField which) {
    ccColor3B current = m_state.color1;
    switch (which) {
        case ColorField::Color1: current = m_state.color1;    break;
        case ColorField::Color2: current = m_state.color2;    break;
        case ColorField::Glow:   current = m_state.colorGlow; break;
        case ColorField::Brightness: return;  // brightness uses the slider
    }
    auto* popup = ColorPickPopup::create(ccColor4B{current.r, current.g, current.b, 255});
    if (!popup) return;

    popup->setCallback([this, which](ccColor4B const& picked) {
        ccColor3B c{picked.r, picked.g, picked.b};
        switch (which) {
            case ColorField::Color1:
                m_state.color1 = c;
                if (m_swatch1) m_swatch1->setColor(c);
                break;
            case ColorField::Color2:
                m_state.color2 = c;
                if (m_swatch2) m_swatch2->setColor(c);
                break;
            case ColorField::Glow:
                m_state.colorGlow = c;
                if (m_swatchGlow) m_swatchGlow->setColor(c);
                break;
            default: break;
        }
        if (m_callback) m_callback(which, m_state);
    });
    popup->show();
}

void ColorPickerRow::onBrightnessSlider(CCObject* sender) {
    if (!m_brightnessSlider) return;
    auto* thumb = m_brightnessSlider->getThumb();
    if (!thumb) return;
    float v = std::clamp(thumb->getValue(), 0.f, 1.f);
    int brightness = 100 + static_cast<int>(std::lround(v * 200.f));
    if (brightness == m_state.brightness) return;
    m_state.brightness = brightness;
    if (m_brightnessValueLbl) {
        m_brightnessValueLbl->setString(std::to_string(brightness).c_str());
    }
    if (m_callback) m_callback(ColorField::Brightness, m_state);
}

void ColorPickerRow::setState(ColorPickerRowState const& state) {
    m_state = state;
    if (m_swatch1)    m_swatch1->setColor(m_state.color1);
    if (m_swatch2)    m_swatch2->setColor(m_state.color2);
    if (m_swatchGlow) m_swatchGlow->setColor(m_state.colorGlow);
    if (m_brightnessValueLbl) {
        m_brightnessValueLbl->setString(std::to_string(m_state.brightness).c_str());
    }
    if (m_brightnessSlider) {
        float v = std::clamp((m_state.brightness - 100) / 200.f, 0.f, 1.f);
        m_brightnessSlider->setValue(v);
    }
}

}  // namespace paimon::texture_studio
