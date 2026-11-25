#include "AmbientConfigPopup.hpp"

#include "../services/AmbientAnalyzer.hpp"
#include "../services/AmbientConfig.hpp"
#include "../services/AmbientDecorator.hpp"
#include "../services/AmbientRuntime.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::ambient {
namespace {

constexpr float kWidth = 440.f;
constexpr float kHeight = 324.f;

char const* quadrantLabel(float x, float y) {
    if (y >= 0.62f) {
        if (x <= 0.38f) return "Top-Left";
        if (x >= 0.62f) return "Top-Right";
        return "Top";
    }
    if (y <= 0.38f) {
        if (x <= 0.38f) return "Bottom-Left";
        if (x >= 0.62f) return "Bottom-Right";
        return "Bottom";
    }
    if (x <= 0.38f) return "Left";
    if (x >= 0.62f) return "Right";
    return "Center";
}

GLubyte toByte(float v) {
    return static_cast<GLubyte>(std::clamp(std::lround(v * 255.f), 0l, 255l));
}

} // namespace

AmbientConfigPopup* AmbientConfigPopup::create() {
    auto* ret = new AmbientConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool AmbientConfigPopup::init() {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    this->setTitle("Dynamic Ambient", "goldFont.fnt", 0.7f);
    this->setID("dynamic-ambient-config-popup"_spr);
    this->addInfoButton("Dynamic Ambient",
        "Analyzes the active background in every menu to detect the dominant "
        "light source and extract an adaptive real-time UI color palette.\n\n"
        "Buttons, panels and UI assets adapt their colors like a live texture "
        "pack while receiving directional sunlight, rim highlights, ambient grading "
        "and directional drop shadows.\n\n"
        "<cr>Mutually exclusive</c> with <cy>Liquid Glass</c>. Buttons with an "
        "active <cg>Button Skin</c> are kept untouched.");

    m_work = loadSettings();
    AmbientRuntime::get().reload();
    if (AmbientRuntime::get().isBlockedByGlass()) {
        m_work.enabled = false;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 6);

    auto* enableLabel = CCLabelBMFont::create("Enable Dynamic Ambient", "bigFont.fnt");
    enableLabel->setAnchorPoint({0.f, 0.5f});
    enableLabel->setScale(0.42f);
    enableLabel->limitLabelWidth(280.f, 0.42f, 0.2f);
    enableLabel->setPosition({30.f, 276.f});
    m_mainLayer->addChild(enableLabel);

    m_enableToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(AmbientConfigPopup::onToggleEnabled), 0.72f);
    m_enableToggle->setPosition({kWidth - 40.f, 276.f});
    m_enableToggle->toggle(m_work.enabled);
    menu->addChild(m_enableToggle);

    m_glassNoticeLabel = CCLabelBMFont::create(
        "Liquid Glass is active: disable it first to enable Dynamic Ambient.",
        "chatFont.fnt"
    );
    m_glassNoticeLabel->setAnchorPoint({0.f, 0.5f});
    m_glassNoticeLabel->setScale(0.54f);
    m_glassNoticeLabel->setColor({255, 160, 120});
    m_glassNoticeLabel->setPosition({30.f, 254.f});
    m_glassNoticeLabel->setVisible(AmbientRuntime::get().isBlockedByGlass());
    m_mainLayer->addChild(m_glassNoticeLabel);

    auto addScope = [&](char const* title, float x, float toggleX, CCMenuItemToggler*& toggle,
                        bool value, SEL_MenuHandler cb) {
        auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(toggleX - x - 18.f, 0.36f, 0.16f);
        label->setPosition({x, 226.f});
        m_mainLayer->addChild(label);
        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.55f);
        toggle->setPosition({toggleX, 226.f});
        toggle->toggle(value);
        menu->addChild(toggle);
    };
    addScope("Buttons", 30.f, 128.f, m_buttonsToggle, m_work.affectButtons,
        menu_selector(AmbientConfigPopup::onToggleButtons));
    addScope("Panels", 162.f, 262.f, m_panelsToggle, m_work.affectPanels,
        menu_selector(AmbientConfigPopup::onTogglePanels));
    addScope("Assets", 296.f, kWidth - 40.f, m_assetsToggle, m_work.affectAssets,
        menu_selector(AmbientConfigPopup::onToggleAssets));

    float y = 190.f;
    auto addSlider = [&](char const* title, Slider*& slider, CCLabelBMFont*& value,
                         float current, SEL_MenuHandler cb) {
        auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(104.f, 0.36f, 0.16f);
        label->setPosition({30.f, y});
        m_mainLayer->addChild(label);

        slider = Slider::create(this, cb, 0.75f);
        slider->setPosition({247.f, y});
        slider->setValue(std::clamp(current, 0.f, 1.f));
        m_mainLayer->addChild(slider, 4);

        value = CCLabelBMFont::create("", "bigFont.fnt");
        value->setAnchorPoint({1.f, 0.5f});
        value->setScale(0.34f);
        value->setPosition({kWidth - 30.f, y});
        m_mainLayer->addChild(value);
        y -= 28.f;
    };

    addSlider("Intensity", m_intensitySlider, m_intensityLabel, m_work.intensity / 1.5f,
        menu_selector(AmbientConfigPopup::onIntensityChanged));
    addSlider("Rim Light", m_rimSlider, m_rimLabel, m_work.rimStrength / 1.5f,
        menu_selector(AmbientConfigPopup::onRimChanged));
    addSlider("Drop Shadow", m_shadowSlider, m_shadowLabel, m_work.shadowStrength,
        menu_selector(AmbientConfigPopup::onShadowChanged));
    addSlider("Color Match", m_tintSlider, m_tintLabel, m_work.tintStrength,
        menu_selector(AmbientConfigPopup::onTintChanged));
    addSlider("Palette Sync", m_paletteSlider, m_paletteLabel, m_work.paletteStrength,
        menu_selector(AmbientConfigPopup::onPaletteChanged));

    // Bottom status pill: detected light direction + live key/shadow/primary/secondary swatches.
    if (auto* statusBg = paimon::ui::makeInset({254.f, 28.f})) {
        statusBg->setPosition({156.f, 10.f});
        m_mainLayer->addChild(statusBg);
    }
    m_keySwatch = CCLayerColor::create({255, 240, 190, 255}, 12.f, 12.f);
    m_keySwatch->setPosition({166.f, 18.f});
    m_mainLayer->addChild(m_keySwatch, 3);

    m_shadowSwatch = CCLayerColor::create({70, 60, 110, 255}, 12.f, 12.f);
    m_shadowSwatch->setPosition({182.f, 18.f});
    m_mainLayer->addChild(m_shadowSwatch, 3);

    m_primarySwatch = CCLayerColor::create({120, 224, 75, 255}, 12.f, 12.f);
    m_primarySwatch->setPosition({198.f, 18.f});
    m_mainLayer->addChild(m_primarySwatch, 3);

    m_secondarySwatch = CCLayerColor::create({66, 209, 245, 255}, 12.f, 12.f);
    m_secondarySwatch->setPosition({214.f, 18.f});
    m_mainLayer->addChild(m_secondarySwatch, 3);

    m_liveLightLabel = CCLabelBMFont::create("Light: Top-Left", "chatFont.fnt");
    m_liveLightLabel->setAnchorPoint({0.f, 0.5f});
    m_liveLightLabel->setScale(0.52f);
    m_liveLightLabel->limitLabelWidth(174.f, 0.52f, 0.25f);
    m_liveLightLabel->setPosition({232.f, 24.f});
    m_mainLayer->addChild(m_liveLightLabel, 3);

    auto* resetBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.7f),
        this, menu_selector(AmbientConfigPopup::onReset));
    resetBtn->setPosition({78.f, 24.f});
    menu->addChild(resetBtn);

    refreshLabels();
    refreshLiveStatus();
    scheduleUpdate();
    return true;
}

void AmbientConfigPopup::update(float /*dt*/) {
    refreshLiveStatus();
}

void AmbientConfigPopup::refreshLabels() {
    if (m_intensityLabel) m_intensityLabel->setString(fmt::format("{}%", std::lround(m_work.intensity * 100.f)).c_str());
    if (m_rimLabel) m_rimLabel->setString(fmt::format("{}%", std::lround(m_work.rimStrength * 100.f)).c_str());
    if (m_shadowLabel) m_shadowLabel->setString(fmt::format("{}%", std::lround(m_work.shadowStrength * 100.f)).c_str());
    if (m_tintLabel) m_tintLabel->setString(fmt::format("{}%", std::lround(m_work.tintStrength * 100.f)).c_str());
    if (m_paletteLabel) m_paletteLabel->setString(fmt::format("{}%", std::lround(m_work.paletteStrength * 100.f)).c_str());
    if (m_glassNoticeLabel) m_glassNoticeLabel->setVisible(AmbientRuntime::get().isBlockedByGlass());
}

void AmbientConfigPopup::refreshLiveStatus() {
    auto const& st = AmbientAnalyzer::get().currentState();
    if (m_keySwatch) {
        m_keySwatch->setColor({toByte(st.keyColor.r), toByte(st.keyColor.g), toByte(st.keyColor.b)});
    }
    if (m_shadowSwatch) {
        m_shadowSwatch->setColor({toByte(st.shadowColor.r), toByte(st.shadowColor.g), toByte(st.shadowColor.b)});
    }
    if (m_primarySwatch) {
        m_primarySwatch->setColor({toByte(st.palettePrimary.r), toByte(st.palettePrimary.g), toByte(st.palettePrimary.b)});
    }
    if (m_secondarySwatch) {
        m_secondarySwatch->setColor({toByte(st.paletteSecondary.r), toByte(st.paletteSecondary.g), toByte(st.paletteSecondary.b)});
    }
    if (m_liveLightLabel) {
        auto text = fmt::format(
            "Light: {} ({}%)",
            quadrantLabel(st.lightX, st.lightY),
            std::lround(st.directionality * 100.f)
        );
        if (text != m_lastLightText) {
            m_lastLightText = std::move(text);
            m_liveLightLabel->setString(m_lastLightText.c_str());
        }
    }
}

void AmbientConfigPopup::applyAndSave(bool redecorate) {
    m_work = sanitizeAmbientSettings(m_work);
    if (m_work.enabled && AmbientRuntime::get().isBlockedByGlass()) {
        m_work.enabled = false;
    }
    saveSettings(m_work);
    AmbientRuntime::get().reload();
    if (redecorate && AmbientRuntime::get().active()) {
        decorateRunningScene();
    }
    refreshLabels();
}

void AmbientConfigPopup::onToggleEnabled(CCObject*) {
    bool const wantEnable = !m_enableToggle->isToggled();
    if (wantEnable && AmbientRuntime::get().isBlockedByGlass()) {
        m_work.enabled = false;
        // CCMenuItemToggler toggles itself after callback if we don't restore it next tick;
        // setToggled(true) before its own toggle flips it back to false.
        if (m_enableToggle) m_enableToggle->toggle(true);
        PaimonNotify::show("Disable Liquid Glass first to enable Dynamic Ambient.", NotificationIcon::Warning);
        refreshLabels();
        return;
    }
    m_work.enabled = wantEnable;
    applyAndSave(m_work.enabled);
}

void AmbientConfigPopup::onToggleButtons(CCObject*) {
    m_work.affectButtons = !m_buttonsToggle->isToggled();
    applyAndSave(m_work.affectButtons);
}

void AmbientConfigPopup::onTogglePanels(CCObject*) {
    m_work.affectPanels = !m_panelsToggle->isToggled();
    applyAndSave(m_work.affectPanels);
}

void AmbientConfigPopup::onToggleAssets(CCObject*) {
    m_work.affectAssets = !m_assetsToggle->isToggled();
    applyAndSave(m_work.affectAssets);
}

void AmbientConfigPopup::onIntensityChanged(CCObject*) {
    if (m_intensitySlider) {
        m_work.intensity = m_intensitySlider->getThumb()->getValue() * 1.5f;
        applyAndSave();
    }
}

void AmbientConfigPopup::onRimChanged(CCObject*) {
    if (m_rimSlider) {
        m_work.rimStrength = m_rimSlider->getThumb()->getValue() * 1.5f;
        applyAndSave();
    }
}

void AmbientConfigPopup::onShadowChanged(CCObject*) {
    if (m_shadowSlider) {
        m_work.shadowStrength = m_shadowSlider->getThumb()->getValue();
        applyAndSave();
    }
}

void AmbientConfigPopup::onTintChanged(CCObject*) {
    if (m_tintSlider) {
        m_work.tintStrength = m_tintSlider->getThumb()->getValue();
        applyAndSave();
    }
}

void AmbientConfigPopup::onPaletteChanged(CCObject*) {
    if (m_paletteSlider) {
        m_work.paletteStrength = m_paletteSlider->getThumb()->getValue();
        applyAndSave();
    }
}

void AmbientConfigPopup::onReset(CCObject*) {
    bool const blocked = AmbientRuntime::get().isBlockedByGlass();
    m_work = AmbientSettings{};
    m_work.enabled = !blocked;
    if (m_enableToggle) m_enableToggle->toggle(m_work.enabled);
    if (m_buttonsToggle) m_buttonsToggle->toggle(m_work.affectButtons);
    if (m_panelsToggle) m_panelsToggle->toggle(m_work.affectPanels);
    if (m_assetsToggle) m_assetsToggle->toggle(m_work.affectAssets);
    if (m_intensitySlider) { m_intensitySlider->setValue(m_work.intensity / 1.5f); m_intensitySlider->updateBar(); }
    if (m_rimSlider) { m_rimSlider->setValue(m_work.rimStrength / 1.5f); m_rimSlider->updateBar(); }
    if (m_shadowSlider) { m_shadowSlider->setValue(m_work.shadowStrength); m_shadowSlider->updateBar(); }
    if (m_tintSlider) { m_tintSlider->setValue(m_work.tintStrength); m_tintSlider->updateBar(); }
    if (m_paletteSlider) { m_paletteSlider->setValue(m_work.paletteStrength); m_paletteSlider->updateBar(); }
    applyAndSave(!blocked);
    if (blocked) {
        PaimonNotify::show("Settings reset. Disable Liquid Glass to enable Dynamic Ambient.", NotificationIcon::Warning);
    } else {
        PaimonNotify::show("Dynamic Ambient reset.", NotificationIcon::Success);
    }
}

void AmbientConfigPopup::onClose(CCObject* sender) {
    applyAndSave();
    PaimonPopup::onClose(sender);
}

} // namespace paimon::ambient
