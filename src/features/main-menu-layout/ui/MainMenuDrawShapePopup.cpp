#include "MainMenuDrawShapePopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/ColorPickPopup.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    constexpr float kPopupWidth = 300.f;
    constexpr float kPopupHeight = 230.f;

    std::string tr(char const* key) {
        return Localization::get().getString(key);
    }

    float norm(float value, float min, float max) {
        if (max <= min) return 0.f;
        return std::clamp((value - min) / (max - min), 0.f, 1.f);
    }

    float denorm(float value, float min, float max) {
        return min + std::clamp(value, 0.f, 1.f) * (max - min);
    }

    char const* kindKey(DrawShapeKind kind) {
        switch (kind) {
            case DrawShapeKind::Rectangle: return "menu_layout.draw_shape_kind_rect";
            case DrawShapeKind::RoundedRect: return "menu_layout.draw_shape_kind_round";
            case DrawShapeKind::Circle: return "menu_layout.draw_shape_kind_circle";
        }
        return "menu_layout.draw_shape_kind_round";
    }
}

MainMenuDrawShapePopup* MainMenuDrawShapePopup::create(DrawShapeLayout const& layout, ApplyCallback onApply) {
    auto* ret = new MainMenuDrawShapePopup();
    if (ret && ret->init(layout, std::move(onApply))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MainMenuDrawShapePopup::init(DrawShapeLayout const& layout, ApplyCallback onApply) {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;

    m_layout = layout;
    m_onApply = std::move(onApply);

    this->setTitle(tr("menu_layout.draw_shape_popup_title"));
    this->buildUI();
    this->refreshUI();
    paimon::markDynamicPopup(this);
    return true;
}

void MainMenuDrawShapePopup::buildUI() {
    auto size = m_mainLayer->getContentSize();

    auto* subtitle = CCLabelBMFont::create(tr("menu_layout.draw_shape_popup_hint").c_str(), "chatFont.fnt");
    subtitle->setScale(0.55f);
    subtitle->setPosition({ size.width * 0.5f, size.height - 34.f });
    subtitle->setColor({ 175, 210, 225 });
    m_mainLayer->addChild(subtitle, 3);

    auto* panel = CCLayerColor::create({ 8, 12, 18, 170 });
    panel->setContentSize({ size.width - 22.f, size.height - 62.f });
    panel->setPosition({ 11.f, 18.f });
    m_mainLayer->addChild(panel, 1);

    m_menu = CCMenu::create();
    m_menu->setPosition({ 0.f, 0.f });
    m_mainLayer->addChild(m_menu, 4);

    auto makeFieldLabel = [this](char const* key, float x, float y) {
        auto* label = CCLabelBMFont::create(tr(key).c_str(), "goldFont.fnt");
        label->setScale(0.42f);
        label->setAnchorPoint({ 0.f, 0.5f });
        label->setPosition({ x, y });
        m_mainLayer->addChild(label, 4);
    };

    auto makeValueLabel = [this](float x, float y) {
        auto* label = CCLabelBMFont::create("", "chatFont.fnt");
        label->setScale(0.56f);
        label->setAnchorPoint({ 1.f, 0.5f });
        label->setPosition({ x, y });
        label->setColor({ 205, 238, 255 });
        m_mainLayer->addChild(label, 4);
        return label;
    };

    float leftX = 24.f;
    float sliderX = 150.f;
    float valueX = size.width - 24.f;
    float rowY = size.height - 66.f;
    float rowGap = 30.f;

    makeFieldLabel("menu_layout.draw_shape_kind", leftX, rowY);
    m_kindValue = makeValueLabel(valueX - 56.f, rowY);
    auto* kindSpr = ButtonSprite::create(tr("menu_layout.draw_shape_cycle").c_str(), 58, true, "goldFont.fnt", "GJ_button_01.png", 22.f, 0.42f);
    auto* kindBtn = CCMenuItemSpriteExtra::create(kindSpr, this, menu_selector(MainMenuDrawShapePopup::onKind));
    kindBtn->setPosition({ valueX - 18.f, rowY });
    m_menu->addChild(kindBtn);

    makeFieldLabel("menu_layout.draw_shape_color", leftX, rowY - rowGap);
    m_colorPreview = CCLayerColor::create({ 255, 255, 255, 255 });
    m_colorPreview->ignoreAnchorPointForPosition(false);
    m_colorPreview->setAnchorPoint({ 0.5f, 0.5f });
    m_colorPreview->setContentSize({ 28.f, 16.f });
    m_colorPreview->setPosition({ valueX - 52.f, rowY - rowGap });
    m_mainLayer->addChild(m_colorPreview, 4);
    auto* colorSpr = ButtonSprite::create(tr("menu_layout.draw_shape_pick_color").c_str(), 62, true, "goldFont.fnt", "GJ_button_02.png", 22.f, 0.4f);
    auto* colorBtn = CCMenuItemSpriteExtra::create(colorSpr, this, menu_selector(MainMenuDrawShapePopup::onColor));
    colorBtn->setPosition({ valueX - 14.f, rowY - rowGap });
    m_menu->addChild(colorBtn);

    auto addSlider = [this, leftX, sliderX, valueX](char const* key, float y, Slider*& slider, CCLabelBMFont*& valueLabel, SEL_MenuHandler cb) {
        auto* label = CCLabelBMFont::create(tr(key).c_str(), "goldFont.fnt");
        label->setScale(0.42f);
        label->setAnchorPoint({ 0.f, 0.5f });
        label->setPosition({ leftX, y });
        m_mainLayer->addChild(label, 4);

        slider = Slider::create(this, cb, 0.65f);
        slider->setPosition({ sliderX, y });
        m_mainLayer->addChild(slider, 4);

        valueLabel = CCLabelBMFont::create("", "chatFont.fnt");
        valueLabel->setScale(0.56f);
        valueLabel->setAnchorPoint({ 1.f, 0.5f });
        valueLabel->setPosition({ valueX, y });
        valueLabel->setColor({ 205, 238, 255 });
        m_mainLayer->addChild(valueLabel, 4);
    };

    addSlider("menu_layout.draw_shape_width", rowY - rowGap * 2.f, m_widthSlider, m_widthValue, menu_selector(MainMenuDrawShapePopup::onWidth));
    addSlider("menu_layout.draw_shape_height", rowY - rowGap * 3.f, m_heightSlider, m_heightValue, menu_selector(MainMenuDrawShapePopup::onHeight));
    addSlider("menu_layout.draw_shape_radius", rowY - rowGap * 4.f, m_radiusSlider, m_radiusValue, menu_selector(MainMenuDrawShapePopup::onRadius));
    addSlider("menu_layout.draw_shape_opacity", rowY - rowGap * 5.f, m_opacitySlider, m_opacityValue, menu_selector(MainMenuDrawShapePopup::onOpacity));
}

void MainMenuDrawShapePopup::refreshUI() {
    if (m_kindValue) {
        m_kindValue->setString(tr(kindKey(m_layout.kind)).c_str());
    }
    if (m_colorPreview) {
        m_colorPreview->setColor(m_layout.color);
        m_colorPreview->setOpacity(static_cast<GLubyte>(std::clamp(m_layout.opacity, 0.f, 1.f) * 255.f));
    }
    if (m_widthSlider) m_widthSlider->setValue(norm(m_layout.width, 26.f, 280.f));
    if (m_heightSlider) m_heightSlider->setValue(norm(m_layout.height, 26.f, 240.f));
    if (m_radiusSlider) m_radiusSlider->setValue(norm(m_layout.cornerRadius, 0.f, 80.f));
    if (m_opacitySlider) m_opacitySlider->setValue(std::clamp(m_layout.opacity, 0.f, 1.f));

    if (m_widthValue) m_widthValue->setString(fmt::format("{:.0f}px", m_layout.width).c_str());
    if (m_heightValue) m_heightValue->setString(fmt::format("{:.0f}px", m_layout.height).c_str());
    if (m_radiusValue) {
        bool enabled = m_layout.kind == DrawShapeKind::RoundedRect;
        m_radiusValue->setString(enabled ? fmt::format("{:.0f}px", m_layout.cornerRadius).c_str() : tr("menu_layout.draw_shape_radius_disabled").c_str());
        m_radiusValue->setOpacity(enabled ? 255 : 120);
    }
    if (m_opacityValue) m_opacityValue->setString(fmt::format("{:.0f}%", m_layout.opacity * 100.f).c_str());
    if (m_radiusSlider) {
        bool enabled = m_layout.kind == DrawShapeKind::RoundedRect;
        m_radiusSlider->setVisible(enabled);
    }
}

void MainMenuDrawShapePopup::applyPreview() {
    if (m_onApply) {
        m_onApply(m_layout);
    }
    this->refreshUI();
}

void MainMenuDrawShapePopup::onKind(CCObject*) {
    switch (m_layout.kind) {
        case DrawShapeKind::Rectangle: m_layout.kind = DrawShapeKind::RoundedRect; break;
        case DrawShapeKind::RoundedRect: m_layout.kind = DrawShapeKind::Circle; break;
        case DrawShapeKind::Circle: m_layout.kind = DrawShapeKind::Rectangle; break;
    }
    if (m_layout.kind == DrawShapeKind::Circle) {
        auto size = std::max(m_layout.width, m_layout.height);
        m_layout.width = size;
        m_layout.height = size;
    }
    this->applyPreview();
}

void MainMenuDrawShapePopup::onColor(CCObject*) {
    auto current = ccc4(m_layout.color.r, m_layout.color.g, m_layout.color.b, 255);
    auto* popup = geode::ColorPickPopup::create(current);
    if (!popup) return;

    WeakRef<MainMenuDrawShapePopup> self = this;
    popup->setCallback([self](ccColor4B const& color) {
        auto ref = self.lock();
        auto* popup = static_cast<MainMenuDrawShapePopup*>(ref.data());
        if (!popup || !popup->getParent()) return;
        popup->m_layout.color = { color.r, color.g, color.b };
        popup->applyPreview();
    });
    popup->show();
}

void MainMenuDrawShapePopup::onWidth(CCObject*) {
    if (!m_widthSlider) return;
    m_layout.width = denorm(m_widthSlider->getValue(), 26.f, 280.f);
    if (m_layout.kind == DrawShapeKind::Circle) {
        m_layout.height = m_layout.width;
    }
    this->applyPreview();
}

void MainMenuDrawShapePopup::onHeight(CCObject*) {
    if (!m_heightSlider) return;
    m_layout.height = denorm(m_heightSlider->getValue(), 26.f, 240.f);
    if (m_layout.kind == DrawShapeKind::Circle) {
        m_layout.width = m_layout.height;
    }
    this->applyPreview();
}

void MainMenuDrawShapePopup::onRadius(CCObject*) {
    if (!m_radiusSlider) return;
    m_layout.cornerRadius = denorm(m_radiusSlider->getValue(), 0.f, 80.f);
    this->applyPreview();
}

void MainMenuDrawShapePopup::onOpacity(CCObject*) {
    if (!m_opacitySlider) return;
    m_layout.opacity = std::clamp(m_opacitySlider->getValue(), 0.f, 1.f);
    this->applyPreview();
}

} // namespace paimon::menu_layout
