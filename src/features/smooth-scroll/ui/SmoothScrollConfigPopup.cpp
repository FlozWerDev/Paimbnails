#include "SmoothScrollConfigPopup.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <fmt/format.h>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    namespace ss = paimon::settings::smoothscroll;

    float normFromValue(double val, double minV, double maxV) {
        if (maxV <= minV) return 0.f;
        return static_cast<float>(std::clamp((val - minV) / (maxV - minV), 0.0, 1.0));
    }
    double valueFromSlider(Slider* s, double minV, double maxV) {
        if (!s || !s->getThumb()) return minV;
        double v = static_cast<double>(s->getThumb()->getValue());
        return minV + v * (maxV - minV);
    }
}

namespace paimon::smoothscroll {

SmoothScrollConfigPopup* SmoothScrollConfigPopup::create() {
    auto* ret = new SmoothScrollConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SmoothScrollConfigPopup::init() {
    if (!Popup::init(340.f, 240.f)) return false;
    this->setTitle("Smooth Scroll");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    float y = content.height - 56.f;

    {
        auto lbl = CCLabelBMFont::create("Enable Smooth Scroll", "bigFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 150.f, y});
        m_mainLayer->addChild(lbl);

        m_enableToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(SmoothScrollConfigPopup::onEnableToggled), 0.7f);
        m_enableToggle->setPosition({cx + 140.f, y});
        m_enableToggle->toggle(ss::enabled());
        menu->addChild(m_enableToggle);
        y -= 46.f;
    }

    auto addSlider = [&](char const* text, Slider*& slider, CCLabelBMFont*& valLabel,
                         double val, double minV, double maxV, SEL_MenuHandler cb,
                         char const* fmtStr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 150.f, y});
        m_mainLayer->addChild(lbl);

        slider = Slider::create(this, cb, 0.8f);
        slider->setPosition({cx + 20.f, y});
        slider->setValue(normFromValue(val, minV, maxV));
        m_mainLayer->addChild(slider);

        valLabel = CCLabelBMFont::create(fmt::format(fmt::runtime(fmtStr), val).c_str(), "bigFont.fnt");
        valLabel->setScale(0.42f);
        valLabel->setAnchorPoint({1.f, 0.5f});
        valLabel->setPosition({cx + 152.f, y});
        m_mainLayer->addChild(valLabel);
        y -= 40.f;
    };

    addSlider("Sensitivity", m_sensitivitySlider, m_sensitivityLabel,
        ss::sensitivity(), ss::kSensitivityMin, ss::kSensitivityMax,
        menu_selector(SmoothScrollConfigPopup::onSensitivityChanged), "{:.2f}x");
    addSlider("Smoothness", m_smoothnessSlider, m_smoothnessLabel,
        ss::smoothness(), ss::kSmoothnessMin, ss::kSmoothnessMax,
        menu_selector(SmoothScrollConfigPopup::onSmoothnessChanged), "{:.2f}");


    auto info = CCLabelBMFont::create(
        "Sensitivity = scroll speed.  Smoothness = glide.", "chatFont.fnt");
    info->setScale(0.5f);
    info->setAlignment(kCCTextAlignmentCenter);
    info->setColor({170, 180, 200});
    info->setPosition({cx, 52.f});
    m_mainLayer->addChild(info);


    auto resetSpr = ButtonSprite::create("Reset Defaults", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    resetSpr->setScale(0.6f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this,
        menu_selector(SmoothScrollConfigPopup::onReset));
    resetBtn->setPosition({cx, 28.f});
    menu->addChild(resetBtn);

    paimon::markDynamicPopup(this);
    return true;
}

void SmoothScrollConfigPopup::onEnableToggled(CCObject*) {
    // CCMenuItemToggler::isToggled() returns the state BEFORE this click.
    bool newState = m_enableToggle ? !m_enableToggle->isToggled() : true;
    Mod::get()->setSettingValue<bool>("smooth-scroll", newState);
}

void SmoothScrollConfigPopup::onSensitivityChanged(CCObject*) {
    double v = valueFromSlider(m_sensitivitySlider, ss::kSensitivityMin, ss::kSensitivityMax);
    (void)Mod::get()->setSavedValue<double>("smooth-scroll-sensitivity", v);
    refreshLabels();
}

void SmoothScrollConfigPopup::onSmoothnessChanged(CCObject*) {
    double v = valueFromSlider(m_smoothnessSlider, ss::kSmoothnessMin, ss::kSmoothnessMax);
    (void)Mod::get()->setSavedValue<double>("smooth-scroll-smoothness", v);
    refreshLabels();
}

void SmoothScrollConfigPopup::onReset(CCObject*) {
    (void)Mod::get()->setSavedValue<double>("smooth-scroll-sensitivity", ss::kSensitivityDefault);
    (void)Mod::get()->setSavedValue<double>("smooth-scroll-smoothness", ss::kSmoothnessDefault);
    if (m_sensitivitySlider) m_sensitivitySlider->setValue(
        normFromValue(ss::kSensitivityDefault, ss::kSensitivityMin, ss::kSensitivityMax));
    if (m_smoothnessSlider) m_smoothnessSlider->setValue(
        normFromValue(ss::kSmoothnessDefault, ss::kSmoothnessMin, ss::kSmoothnessMax));
    refreshLabels();
}

void SmoothScrollConfigPopup::refreshLabels() {
    if (m_sensitivityLabel) {
        m_sensitivityLabel->setString(fmt::format("{:.2f}x", ss::sensitivity()).c_str());
    }
    if (m_smoothnessLabel) {
        m_smoothnessLabel->setString(fmt::format("{:.2f}", ss::smoothness()).c_str());
    }
}

} // namespace paimon::smoothscroll
