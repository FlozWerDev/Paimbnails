#include "GlassConfigPopup.hpp"

#include "../services/GlassConfig.hpp"
#include "../services/GlassDecorator.hpp"
#include "../services/GlassNode.hpp"
#include "../services/GlassRuntime.hpp"
#include "../../dynamic-ambient/services/AmbientRuntime.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/cocos/sprite_nodes/CCSprite.h>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/binding/Slider.hpp>
#include <cmath>

using namespace geode::prelude;

namespace paimon::glass {
namespace {

constexpr float kWidth = 440.f;
constexpr float kHeight = 300.f;

void setBtnLabel(CCMenuItemSpriteExtra* btn, char const* text) {
    if (!btn) return;
    if (auto* bs = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
        bs->setString(text);
    }
}

} // namespace

GlassConfigPopup* GlassConfigPopup::create() {
    auto* ret = new GlassConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GlassConfigPopup::init() {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    this->setTitle("Liquid Glass UI", "goldFont.fnt", 0.7f);
    this->setID("glass-config-popup"_spr);
    this->addInfoButton("Liquid Glass UI",
        "Popups, menu panels, lists, inputs, sliders, toasts and UI buttons "
        "turn into Apple-style glass (UI only, never gameplay): what is behind "
        "them shows through, frosted, bent by a curved rim, with a specular "
        "edge that follows the light.\n\n"
        "<cy>Balanced</c> refreshes the glass every frame and suits most PCs. "
        "<cy>High</c> blurs deeper. <cy>Lite</c> refreshes 12 times per second "
        "for weak phones. <cy>Simulated</c> copies nothing: tint and rim only.\n\n"
        "<cy>GD colors</c> keeps each button's colour (green, red...) as its tint.");

    m_work = loadSettings();
    GlassRuntime::get().reload();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 6);

    auto enableLabel = CCLabelBMFont::create("Enable Liquid Glass", "bigFont.fnt");
    enableLabel->setAnchorPoint({0.f, 0.5f});
    enableLabel->setScale(0.42f);
    enableLabel->limitLabelWidth(280.f, 0.42f, 0.2f);
    enableLabel->setPosition({30.f, 254.f});
    m_mainLayer->addChild(enableLabel);

    m_enableToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(GlassConfigPopup::onToggleEnabled), 0.72f);
    m_enableToggle->setPosition({kWidth - 40.f, 254.f});
    m_enableToggle->toggle(m_work.enabled);
    menu->addChild(m_enableToggle);

    auto tierLabel = CCLabelBMFont::create("Quality", "bigFont.fnt");
    tierLabel->setAnchorPoint({0.f, 0.5f});
    tierLabel->setScale(0.42f);
    tierLabel->setPosition({30.f, 222.f});
    m_mainLayer->addChild(tierLabel);

    m_tierButton = CCMenuItemSpriteExtra::create(
        ButtonSprite::create(glassTierName(m_work.tier), "bigFont.fnt", "GJ_button_04.png", 0.7f),
        this, menu_selector(GlassConfigPopup::onCycleTier));
    m_tierButton->setPosition({kWidth - 92.f, 222.f});
    menu->addChild(m_tierButton);

    // scope row: label + toggle, three times across the popup.
    auto addScope = [&](char const* title, float x, float toggleX, CCMenuItemToggler*& toggle,
                        bool value, SEL_MenuHandler cb) {
        auto label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(toggleX - x - 18.f, 0.36f, 0.16f);
        label->setPosition({x, 190.f});
        m_mainLayer->addChild(label);
        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.55f);
        toggle->setPosition({toggleX, 190.f});
        toggle->toggle(value);
        menu->addChild(toggle);
    };
    addScope("Popups", 30.f, 128.f, m_popupsToggle, m_work.popups,
        menu_selector(GlassConfigPopup::onTogglePopups));
    addScope("Buttons", 162.f, 262.f, m_buttonsToggle, m_work.buttons,
        menu_selector(GlassConfigPopup::onToggleButtons));
    addScope("GD colors", 296.f, kWidth - 40.f, m_colorsToggle, m_work.coloredButtons,
        menu_selector(GlassConfigPopup::onToggleColors));

    auto tintLabel = CCLabelBMFont::create("Tint Color", "bigFont.fnt");
    tintLabel->setAnchorPoint({0.f, 0.5f});
    tintLabel->setScale(0.42f);
    tintLabel->setPosition({30.f, 158.f});
    m_mainLayer->addChild(tintLabel);

    m_tintButton = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Pick", "bigFont.fnt", "GJ_button_01.png", 0.7f),
        this, menu_selector(GlassConfigPopup::onPickTint));
    m_tintButton->setPosition({kWidth - 92.f, 158.f});
    menu->addChild(m_tintButton);

    // one line per slider: label | centred slider | value. A GD Slider's
    // groove (~210 * barScale wide) is centred on the node position, so the
    // node must sit at the centre of its zone, never at its left edge.
    float y = 128.f;
    auto addSlider = [&](char const* title, Slider*& slider, CCLabelBMFont*& value,
                         float current, SEL_MenuHandler cb) {
        auto label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(102.f, 0.36f, 0.16f);
        label->setPosition({30.f, y});
        m_mainLayer->addChild(label);

        slider = Slider::create(this, cb, 0.75f);
        slider->setPosition({247.f, y});
        slider->setValue(current);
        m_mainLayer->addChild(slider, 4);

        value = CCLabelBMFont::create("", "bigFont.fnt");
        value->setAnchorPoint({1.f, 0.5f});
        value->setScale(0.34f);
        value->setPosition({kWidth - 30.f, y});
        m_mainLayer->addChild(value);
        y -= 25.f;
    };
    addSlider("Blur", m_blurSlider, m_blurLabel, m_work.blur,
        menu_selector(GlassConfigPopup::onBlurChanged));
    addSlider("Refraction", m_refractionSlider, m_refractionLabel, m_work.refraction,
        menu_selector(GlassConfigPopup::onRefractionChanged));
    addSlider("Tint Opacity", m_tintOpacitySlider, m_tintOpacityLabel, m_work.tintOpacity / 0.8f,
        menu_selector(GlassConfigPopup::onTintOpacityChanged));
    addSlider("Rim Light", m_rimSlider, m_rimLabel, m_work.rim,
        menu_selector(GlassConfigPopup::onRimChanged));

    // live preview capsule over an inset, clear of the lowest slider row
    // (y = 53) and of the Reset button on the left.
    auto previewBg = paimon::ui::makeInset({150.f, 28.f});
    if (previewBg) {
        previewBg->setPosition({260.f, 8.f});
        m_mainLayer->addChild(previewBg);
    }
    if (auto* plate = GlassNode::create({140.f, 22.f}, GlassKind::Button)) {
        plate->setPosition({335.f, 22.f});
        m_mainLayer->addChild(plate, 3);
    }
    auto previewLabel = CCLabelBMFont::create("Preview", "bigFont.fnt");
    previewLabel->setScale(0.4f);
    previewLabel->setPosition({335.f, 22.f});
    m_mainLayer->addChild(previewLabel, 4);

    auto resetBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.7f),
        this, menu_selector(GlassConfigPopup::onReset));
    resetBtn->setPosition({78.f, 22.f});
    menu->addChild(resetBtn);

    refreshLabels();
    return true;
}

void GlassConfigPopup::refreshLabels() {
    if (m_blurLabel) m_blurLabel->setString(fmt::format("{}%", std::lround(m_work.blur * 100.f)).c_str());
    if (m_refractionLabel) m_refractionLabel->setString(fmt::format("{}%", std::lround(m_work.refraction * 100.f)).c_str());
    if (m_tintOpacityLabel) m_tintOpacityLabel->setString(fmt::format("{}%", std::lround(m_work.tintOpacity * 100.f)).c_str());
    if (m_rimLabel) m_rimLabel->setString(fmt::format("{}%", std::lround(m_work.rim * 100.f)).c_str());
    setBtnLabel(m_tierButton, glassTierName(m_work.tier));
}

void GlassConfigPopup::applyAndSave(bool redecorate) {
    m_work = sanitizeGlassSettings(m_work);
    saveSettings(m_work);
    auto& runtime = GlassRuntime::get();
    runtime.reload();
    // plates created while the material was off do not exist yet.
    if (redecorate && runtime.active()) decorateRunningScene();
    refreshLabels();
}

// cCMenuItemToggler reports the state from before the click.
void GlassConfigPopup::onToggleEnabled(CCObject*) {
    bool const wasAmbientOn = paimon::ambient::AmbientRuntime::get().active();
    m_work.enabled = !m_enableToggle->isToggled();
    applyAndSave(m_work.enabled);
    if (m_work.enabled && wasAmbientOn) {
        PaimonNotify::show("Dynamic Ambient disabled (incompatible with Liquid Glass).", NotificationIcon::Info);
    }
}

void GlassConfigPopup::onTogglePopups(CCObject*) {
    m_work.popups = !m_popupsToggle->isToggled();
    applyAndSave(m_work.popups);
}

void GlassConfigPopup::onToggleButtons(CCObject*) {
    m_work.buttons = !m_buttonsToggle->isToggled();
    applyAndSave(m_work.buttons);
}

void GlassConfigPopup::onToggleColors(CCObject*) {
    m_work.coloredButtons = !m_colorsToggle->isToggled();
    applyAndSave();
}

void GlassConfigPopup::onCycleTier(CCObject*) {
    m_work.tier = nextGlassTier(m_work.tier);
    applyAndSave();
}

void GlassConfigPopup::onPickTint(CCObject*) {
    WeakRef<GlassConfigPopup> self = this;
    auto* picker = ColorPickPopup::create(
        ccc3(static_cast<GLubyte>(m_work.tintR), static_cast<GLubyte>(m_work.tintG),
             static_cast<GLubyte>(m_work.tintB)));
    picker->setCallback([self](ccColor4B const& color) {
        auto popup = self.lock();
        if (!popup) return;
        popup->m_work.tintR = color.r;
        popup->m_work.tintG = color.g;
        popup->m_work.tintB = color.b;
        popup->applyAndSave();
    });
    picker->show();
}

void GlassConfigPopup::onBlurChanged(CCObject*) {
    if (m_blurSlider) {
        m_work.blur = m_blurSlider->getThumb()->getValue();
        applyAndSave();
    }
}

void GlassConfigPopup::onRefractionChanged(CCObject*) {
    if (m_refractionSlider) {
        m_work.refraction = m_refractionSlider->getThumb()->getValue();
        applyAndSave();
    }
}

void GlassConfigPopup::onTintOpacityChanged(CCObject*) {
    if (m_tintOpacitySlider) {
        m_work.tintOpacity = m_tintOpacitySlider->getThumb()->getValue() * 0.8f;
        applyAndSave();
    }
}

void GlassConfigPopup::onRimChanged(CCObject*) {
    if (m_rimSlider) {
        m_work.rim = m_rimSlider->getThumb()->getValue();
        applyAndSave();
    }
}

void GlassConfigPopup::onReset(CCObject*) {
    m_work = GlassSettings{};
    m_work.enabled = true;
    if (m_enableToggle) m_enableToggle->toggle(true);
    if (m_popupsToggle) m_popupsToggle->toggle(m_work.popups);
    if (m_buttonsToggle) m_buttonsToggle->toggle(m_work.buttons);
    if (m_colorsToggle) m_colorsToggle->toggle(m_work.coloredButtons);
    if (m_blurSlider) { m_blurSlider->setValue(m_work.blur); m_blurSlider->updateBar(); }
    if (m_refractionSlider) { m_refractionSlider->setValue(m_work.refraction); m_refractionSlider->updateBar(); }
    if (m_tintOpacitySlider) { m_tintOpacitySlider->setValue(m_work.tintOpacity / 0.8f); m_tintOpacitySlider->updateBar(); }
    if (m_rimSlider) { m_rimSlider->setValue(m_work.rim); m_rimSlider->updateBar(); }
    applyAndSave(true);
    PaimonNotify::show("Liquid Glass reset.", NotificationIcon::Success);
}

void GlassConfigPopup::onClose(CCObject* sender) {
    applyAndSave();
    PaimonPopup::onClose(sender);
}

} // namespace paimon::glass
