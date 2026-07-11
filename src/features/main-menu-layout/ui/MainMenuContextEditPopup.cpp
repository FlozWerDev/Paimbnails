#include "MainMenuContextEditPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../fonts/FontTag.hpp"
#include "../../fonts/ui/FontPickerPopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    constexpr float kW = 280.f;
    constexpr float kH = 175.f;

    std::string tr(char const* key) {
        return Localization::get().getString(key);
    }
}

MainMenuContextEditPopup* MainMenuContextEditPopup::create(std::string title, MenuButtonLayout const& layout, bool allowColor, bool allowFont, ApplyCallback onApply) {
    auto* ret = new MainMenuContextEditPopup();
    if (ret && ret->init(std::move(title), layout, allowColor, allowFont, std::move(onApply))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MainMenuContextEditPopup::init(std::string title, MenuButtonLayout const& layout, bool allowColor, bool allowFont, ApplyCallback onApply) {
    if (!Popup::init(kW, kH)) return false;

    m_layout = layout;
    m_allowColor = allowColor;
    m_allowFont = allowFont;
    m_onApply = std::move(onApply);

    this->setTitle(title);
    this->buildUI(title);
    this->refreshUI();
    paimon::markDynamicPopup(this);
    return true;
}

void MainMenuContextEditPopup::buildUI(std::string const&) {
    auto size = m_mainLayer->getContentSize();

    auto* sub = CCLabelBMFont::create(tr("menu_layout.context_popup_hint").c_str(), "chatFont.fnt");
    sub->setScale(0.52f);
    sub->setPosition({ size.width * 0.5f, size.height - 34.f });
    sub->setColor({ 180, 220, 235 });
    m_mainLayer->addChild(sub, 3);

    auto* menu = CCMenu::create();
    menu->setPosition({ 0.f, 0.f });
    m_mainLayer->addChild(menu, 4);
    m_menu = menu;

    auto* opacityLabel = CCLabelBMFont::create(tr("menu_layout.draw_shape_opacity").c_str(), "goldFont.fnt");
    opacityLabel->setScale(0.42f);
    opacityLabel->setAnchorPoint({ 0.f, 0.5f });
    opacityLabel->setPosition({ 20.f, size.height - 68.f });
    m_mainLayer->addChild(opacityLabel, 4);

    m_opacitySlider = Slider::create(this, menu_selector(MainMenuContextEditPopup::onOpacity), 0.64f);
    m_opacitySlider->setPosition({ 144.f, size.height - 68.f });
    m_mainLayer->addChild(m_opacitySlider, 4);

    m_opacityValue = CCLabelBMFont::create("", "chatFont.fnt");
    m_opacityValue->setScale(0.52f);
    m_opacityValue->setAnchorPoint({ 1.f, 0.5f });
    m_opacityValue->setPosition({ size.width - 18.f, size.height - 68.f });
    m_opacityValue->setColor({ 205, 240, 255 });
    m_mainLayer->addChild(m_opacityValue, 4);

    if (m_allowColor) {
        auto* colorLabel = CCLabelBMFont::create(tr("menu_layout.draw_shape_color").c_str(), "goldFont.fnt");
        colorLabel->setScale(0.42f);
        colorLabel->setAnchorPoint({ 0.f, 0.5f });
        colorLabel->setPosition({ 20.f, size.height - 100.f });
        m_mainLayer->addChild(colorLabel, 4);

        m_colorPreview = CCLayerColor::create({255, 255, 255, 255});
        m_colorPreview->ignoreAnchorPointForPosition(false);
        m_colorPreview->setAnchorPoint({0.5f, 0.5f});
        m_colorPreview->setContentSize({32.f, 16.f});
        m_colorPreview->setPosition({ size.width - 94.f, size.height - 100.f });
        m_mainLayer->addChild(m_colorPreview, 4);

        auto* spr = ButtonSprite::create(tr("menu_layout.draw_shape_pick_color").c_str(), 66, true, "goldFont.fnt", "GJ_button_02.png", 22.f, 0.4f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MainMenuContextEditPopup::onColor));
        btn->setPosition({ size.width - 42.f, size.height - 100.f });
        m_menu->addChild(btn);
    }

    if (m_allowFont) {
        auto* fontLabel = CCLabelBMFont::create(tr("menu_layout.context_font").c_str(), "goldFont.fnt");
        fontLabel->setScale(0.42f);
        fontLabel->setAnchorPoint({ 0.f, 0.5f });
        fontLabel->setPosition({ 20.f, size.height - 132.f });
        m_mainLayer->addChild(fontLabel, 4);

        m_fontValue = CCLabelBMFont::create("", "chatFont.fnt");
        m_fontValue->setScale(0.5f);
        m_fontValue->setAnchorPoint({ 1.f, 0.5f });
        m_fontValue->setPosition({ size.width - 92.f, size.height - 132.f });
        m_fontValue->setColor({ 205, 240, 255 });
        m_mainLayer->addChild(m_fontValue, 4);

        auto* spr = ButtonSprite::create(tr("menu_layout.context_pick_font").c_str(), 66, true, "goldFont.fnt", "GJ_button_01.png", 22.f, 0.4f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MainMenuContextEditPopup::onFont));
        btn->setPosition({ size.width - 42.f, size.height - 132.f });
        m_menu->addChild(btn);
    }
}

void MainMenuContextEditPopup::refreshUI() {
    if (m_opacitySlider) m_opacitySlider->setValue(std::clamp(m_layout.opacity, 0.f, 1.f));
    if (m_opacityValue) m_opacityValue->setString(fmt::format("{:.0f}%", m_layout.opacity * 100.f).c_str());
    if (m_colorPreview) {
        m_colorPreview->setColor(m_layout.color);
        m_colorPreview->setOpacity(static_cast<GLubyte>(std::clamp(m_layout.opacity, 0.f, 1.f) * 255.f));
    }
    if (m_fontValue) {
        m_fontValue->setString((m_layout.fontFile.empty() ? "chatFont.fnt" : m_layout.fontFile).c_str());
    }
}

void MainMenuContextEditPopup::emit() {
    if (m_onApply) m_onApply(m_layout);
    this->refreshUI();
}

void MainMenuContextEditPopup::onOpacity(CCObject*) {
    if (!m_opacitySlider) return;
    m_layout.opacity = std::clamp(m_opacitySlider->getValue(), 0.f, 1.f);
    this->emit();
}

void MainMenuContextEditPopup::onColor(CCObject*) {
    auto* popup = geode::ColorPickPopup::create(ccc4(m_layout.color.r, m_layout.color.g, m_layout.color.b, 255));
    if (!popup) return;
    WeakRef<MainMenuContextEditPopup> self = this;
    popup->setCallback([self](ccColor4B const& color) {
        auto ref = self.lock();
        auto* popup = static_cast<MainMenuContextEditPopup*>(ref.data());
        if (!popup || !popup->getParent()) return;
        popup->m_layout.color = { color.r, color.g, color.b };
        popup->m_layout.hasColor = true;
        popup->emit();
    });
    popup->show();
}

void MainMenuContextEditPopup::onFont(CCObject*) {
    WeakRef<MainMenuContextEditPopup> self = this;
    auto* picker = paimon::fonts::FontPickerPopup::create([self](std::string const& fontTag) {
        auto ref = self.lock();
        auto* popup = static_cast<MainMenuContextEditPopup*>(ref.data());
        if (!popup || !popup->getParent()) return;

        auto result = paimon::fonts::parseFontTag(fontTag);
        popup->m_layout.fontFile = result.hasTag ? result.fontFile : std::string("chatFont.fnt");
        popup->emit();
    });
    if (picker) picker->show();
}

} // namespace paimon::menu_layout
