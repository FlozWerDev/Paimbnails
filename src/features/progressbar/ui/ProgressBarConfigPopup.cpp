#include "ProgressBarConfigPopup.hpp"
#include "../services/ProgressBarManager.hpp"
#include "ProgressBarEditOverlay.hpp"
#include "../../fonts/ui/FontPickerPopup.hpp"
#include "../../fonts/FontTag.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include <Geode/utils/string.hpp>

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/utils/cocos.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

// ──────────────────────────────────────────────────────────
// helpers
// ──────────────────────────────────────────────────────────

namespace {
float readSliderRange(Slider* s, float minV, float maxV) {
    if (!s || !s->getThumb()) return minV;
    float v = s->getThumb()->getValue();
    return minV + v * (maxV - minV);
}

float normFromValue(float val, float minV, float maxV) {
    if (maxV <= minV) return 0.f;
    return std::clamp((val - minV) / (maxV - minV), 0.f, 1.f);
}
} // namespace

// ──────────────────────────────────────────────────────────
// factory
// ──────────────────────────────────────────────────────────

ProgressBarConfigPopup* ProgressBarConfigPopup::create() {
    auto* ret = new ProgressBarConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProgressBarConfigPopup::init() {
    if (!Popup::init(400.f, 280.f)) return false;
    this->setTitle("Custom Progress Bar");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

    m_generalTab = CCNode::create();
    m_generalTab->setID("pb-general"_spr);
    m_generalTab->setContentSize(content);
    m_mainLayer->addChild(m_generalTab, 5);

    m_positionTab = CCNode::create();
    m_positionTab->setID("pb-position"_spr);
    m_positionTab->setContentSize(content);
    m_positionTab->setVisible(false);
    m_mainLayer->addChild(m_positionTab, 5);

    m_colorsTab = CCNode::create();
    m_colorsTab->setID("pb-colors"_spr);
    m_colorsTab->setContentSize(content);
    m_colorsTab->setVisible(false);
    m_mainLayer->addChild(m_colorsTab, 5);

    m_labelTab = CCNode::create();
    m_labelTab->setID("pb-label"_spr);
    m_labelTab->setContentSize(content);
    m_labelTab->setVisible(false);
    m_mainLayer->addChild(m_labelTab, 5);

    m_fxTab = CCNode::create();
    m_fxTab->setID("pb-fx"_spr);
    m_fxTab->setContentSize(content);
    m_fxTab->setVisible(false);
    m_mainLayer->addChild(m_fxTab, 5);

    createTabButtons();
    buildGeneralTab();
    buildPositionTab();
    buildColorsTab();
    buildLabelTab();
    buildFxTab();

    paimon::markDynamicPopup(this);
    return true;
}

void ProgressBarConfigPopup::onExit() {
    applyAndSave();
    Popup::onExit();
}

// ──────────────────────────────────────────────────────────
// tabs
// ──────────────────────────────────────────────────────────

void ProgressBarConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();
    float topY = content.height - 36.f;
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    struct Def { const char* label; int tag; float dx; };
    Def defs[] = {
        {"General",  0, -160.f},
        {"Pos/Size", 1,  -80.f},
        {"Colors",   2,    0.f},
        {"Label",    3,   80.f},
        {"FX",       4,  160.f},
    };

    for (auto const& d : defs) {
        auto spr = ButtonSprite::create(d.label);
        spr->setScale(0.45f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProgressBarConfigPopup::onTabSwitch));
        btn->setTag(d.tag);
        btn->setPosition({cx + d.dx, topY});
        menu->addChild(btn);
        m_tabs.push_back(btn);
    }

    onTabSwitch(m_tabs.front());
}

void ProgressBarConfigPopup::onTabSwitch(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_generalTab->setVisible(m_currentTab == 0);
    m_positionTab->setVisible(m_currentTab == 1);
    m_colorsTab->setVisible(m_currentTab == 2);
    m_labelTab->setVisible(m_currentTab == 3);
    if (m_fxTab) m_fxTab->setVisible(m_currentTab == 4);

    for (auto* tab : m_tabs) {
        auto* spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        if (tab->getTag() == m_currentTab) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(160);
        }
    }
}

// ──────────────────────────────────────────────────────────
// builder helpers (local lambdas are easier but these stay nested per-tab)
// ──────────────────────────────────────────────────────────

void ProgressBarConfigPopup::buildGeneralTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    auto& cfg = ProgressBarManager::get().config();

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_generalTab->addChild(menu, 2);

    float y = content.height - 70.f;

    auto addToggleRow = [&](char const* text, CCMenuItemToggler*& out,
                            bool value, SEL_MenuHandler cb) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 130.f, y});
        m_generalTab->addChild(lbl);

        out = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.7f);
        out->setPosition({cx + 130.f, y});
        out->toggle(value);
        menu->addChild(out);

        y -= 32.f;
    };

    addToggleRow("Enable Custom Bar", m_enableToggle, cfg.enabled,
        menu_selector(ProgressBarConfigPopup::onEnableToggled));
    addToggleRow("Vertical Orientation", m_verticalToggle, cfg.vertical,
        menu_selector(ProgressBarConfigPopup::onVerticalToggled));
    addToggleRow("Use Custom Position", m_useCustomPosToggle, cfg.useCustomPosition,
        menu_selector(ProgressBarConfigPopup::onUseCustomPosToggled));
    addToggleRow("Free Drag Mode", m_freeDragToggle, cfg.freeDragMode,
        menu_selector(ProgressBarConfigPopup::onFreeDragToggled));

    // Free-edit-mode button: hides the pause menu + popup so the user
    // can grab the bar/label directly and move/resize them.
    auto editSpr = ButtonSprite::create("Free Edit Mode", "goldFont.fnt", "GJ_button_02.png", 0.8f);
    editSpr->setScale(0.65f);
    auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this,
        menu_selector(ProgressBarConfigPopup::onEnterFreeEditMode));
    editBtn->setPosition({cx, 78.f});
    menu->addChild(editBtn);

    // reset + center buttons
    auto resetSpr = ButtonSprite::create("Reset Defaults", "goldFont.fnt", "GJ_button_06.png", 0.65f);
    resetSpr->setScale(0.55f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this,
        menu_selector(ProgressBarConfigPopup::onResetDefaults));
    resetBtn->setPosition({cx - 60.f, 35.f});
    menu->addChild(resetBtn);

    auto centerSpr = ButtonSprite::create("Center Bar", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    centerSpr->setScale(0.55f);
    auto centerBtn = CCMenuItemSpriteExtra::create(centerSpr, this,
        menu_selector(ProgressBarConfigPopup::onCenterPosition));
    centerBtn->setPosition({cx + 60.f, 35.f});
    menu->addChild(centerBtn);
}

void ProgressBarConfigPopup::buildPositionTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    auto& cfg = ProgressBarManager::get().config();

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // default custom pos if zero
    if (cfg.posX <= 0.f && cfg.posY <= 0.f) {
        cfg.posX = winSize.width / 2.f;
        cfg.posY = winSize.height - 20.f;
    }

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_positionTab->addChild(menu, 2);

    float y = content.height - 75.f;

    auto addSlider = [&](char const* text, Slider*& slider, CCLabelBMFont*& valLabel,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.4f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 170.f, y});
        m_positionTab->addChild(lbl);

        slider = Slider::create(this, cb, 0.8f);
        slider->setPosition({cx + 10.f, y});
        slider->setValue(normFromValue(val, minV, maxV));
        m_positionTab->addChild(slider);

        valLabel = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        valLabel->setScale(0.4f);
        valLabel->setPosition({cx + 160.f, y});
        m_positionTab->addChild(valLabel);

        y -= 30.f;
    };

    addSlider("Pos X", m_posXSlider, m_posXLabel,
        cfg.posX, 0.f, winSize.width,
        menu_selector(ProgressBarConfigPopup::onPosXChanged), "{:.0f}");
    addSlider("Pos Y", m_posYSlider, m_posYLabel,
        cfg.posY, 0.f, winSize.height,
        menu_selector(ProgressBarConfigPopup::onPosYChanged), "{:.0f}");
    addSlider("Length", m_scaleLenSlider, m_scaleLenLabel,
        cfg.scaleLength, 0.1f, 3.f,
        menu_selector(ProgressBarConfigPopup::onScaleLenChanged), "{:.2f}");
    addSlider("Thickness", m_scaleThickSlider, m_scaleThickLabel,
        cfg.scaleThickness, 0.1f, 3.f,
        menu_selector(ProgressBarConfigPopup::onScaleThickChanged), "{:.2f}");
    addSlider("Opacity", m_opacitySlider, m_opacityLabel,
        static_cast<float>(cfg.opacity), 0.f, 255.f,
        menu_selector(ProgressBarConfigPopup::onOpacityChanged), "{:.0f}");
}

void ProgressBarConfigPopup::buildColorsTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    auto& cfg = ProgressBarManager::get().config();

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_colorsTab->addChild(menu, 2);

    float y = content.height - 75.f;

    auto addColorRow = [&](char const* text, CCMenuItemToggler*& toggle, bool value,
                           SEL_MenuHandler toggleCb, ccColor3B color,
                           CCLayerColor*& preview, SEL_MenuHandler pickCb) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 160.f, y});
        m_colorsTab->addChild(lbl);

        toggle = CCMenuItemToggler::createWithStandardSprites(this, toggleCb, 0.6f);
        toggle->setPosition({cx - 30.f, y});
        toggle->toggle(value);
        menu->addChild(toggle);

        preview = CCLayerColor::create({color.r, color.g, color.b, 255}, 40.f, 22.f);
        preview->setPosition({cx + 50.f, y - 11.f});
        m_colorsTab->addChild(preview);

        auto pickSpr = ButtonSprite::create("Pick", "goldFont.fnt", "GJ_button_04.png", 0.7f);
        pickSpr->setScale(0.55f);
        auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, pickCb);
        pickBtn->setPosition({cx + 140.f, y});
        menu->addChild(pickBtn);

        y -= 36.f;
    };

    addColorRow("Fill Color", m_useFillColorToggle, cfg.useCustomFillColor,
        menu_selector(ProgressBarConfigPopup::onUseFillColorToggled),
        cfg.fillColor, m_fillColorPreview,
        menu_selector(ProgressBarConfigPopup::onPickFillColor));
    addColorRow("Background", m_useBgColorToggle, cfg.useCustomBgColor,
        menu_selector(ProgressBarConfigPopup::onUseBgColorToggled),
        cfg.bgColor, m_bgColorPreview,
        menu_selector(ProgressBarConfigPopup::onPickBgColor));

    auto info = CCLabelBMFont::create(
        "Toggle ON to tint. Tap Pick to choose color.",
        "chatFont.fnt");
    info->setScale(0.5f);
    info->setAlignment(kCCTextAlignmentCenter);
    info->setPosition({cx, 60.f});
    info->setOpacity(170);
    m_colorsTab->addChild(info);
}

void ProgressBarConfigPopup::buildLabelTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    auto& cfg = ProgressBarManager::get().config();

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_labelTab->addChild(menu, 2);

    float y = content.height - 75.f;

    // toggles
    auto lbl1 = CCLabelBMFont::create("Show Percentage", "bigFont.fnt");
    lbl1->setScale(0.45f);
    lbl1->setAnchorPoint({0.f, 0.5f});
    lbl1->setPosition({cx - 160.f, y});
    m_labelTab->addChild(lbl1);
    m_showPctToggle = CCMenuItemToggler::createWithStandardSprites(this,
        menu_selector(ProgressBarConfigPopup::onShowPctToggled), 0.6f);
    m_showPctToggle->setPosition({cx + 130.f, y});
    m_showPctToggle->toggle(cfg.showPercentage);
    menu->addChild(m_showPctToggle);
    y -= 30.f;

    // color toggle + preview + pick
    auto lbl2 = CCLabelBMFont::create("Custom Color", "bigFont.fnt");
    lbl2->setScale(0.45f);
    lbl2->setAnchorPoint({0.f, 0.5f});
    lbl2->setPosition({cx - 160.f, y});
    m_labelTab->addChild(lbl2);
    m_usePctColorToggle = CCMenuItemToggler::createWithStandardSprites(this,
        menu_selector(ProgressBarConfigPopup::onUsePctColorToggled), 0.6f);
    m_usePctColorToggle->setPosition({cx - 30.f, y});
    m_usePctColorToggle->toggle(cfg.useCustomPercentageColor);
    menu->addChild(m_usePctColorToggle);
    m_pctColorPreview = CCLayerColor::create(
        {cfg.percentageColor.r, cfg.percentageColor.g, cfg.percentageColor.b, 255}, 40.f, 22.f);
    m_pctColorPreview->setPosition({cx + 50.f, y - 11.f});
    m_labelTab->addChild(m_pctColorPreview);
    auto pickSpr = ButtonSprite::create("Pick", "goldFont.fnt", "GJ_button_04.png", 0.7f);
    pickSpr->setScale(0.55f);
    auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this,
        menu_selector(ProgressBarConfigPopup::onPickPctColor));
    pickBtn->setPosition({cx + 140.f, y});
    menu->addChild(pickBtn);
    y -= 32.f;

    // Font picker row
    auto fontLbl = CCLabelBMFont::create("Font", "bigFont.fnt");
    fontLbl->setScale(0.45f);
    fontLbl->setAnchorPoint({0.f, 0.5f});
    fontLbl->setPosition({cx - 160.f, y});
    m_labelTab->addChild(fontLbl);

    auto fontSpr = ButtonSprite::create(
        cfg.percentageFont.empty() ? "Default" : cfg.percentageFont.c_str(),
        "goldFont.fnt", "GJ_button_04.png", 0.7f);
    fontSpr->setScale(0.55f);
    auto fontBtn = CCMenuItemSpriteExtra::create(fontSpr, this,
        menu_selector(ProgressBarConfigPopup::onPickFont));
    fontBtn->setPosition({cx + 80.f, y});
    menu->addChild(fontBtn);
    y -= 32.f;

    // sliders
    auto addSlider = [&](char const* text, Slider*& slider, CCLabelBMFont*& valLabel,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.4f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 170.f, y});
        m_labelTab->addChild(lbl);

        slider = Slider::create(this, cb, 0.8f);
        slider->setPosition({cx + 10.f, y});
        slider->setValue(normFromValue(val, minV, maxV));
        m_labelTab->addChild(slider);

        valLabel = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        valLabel->setScale(0.4f);
        valLabel->setPosition({cx + 160.f, y});
        m_labelTab->addChild(valLabel);

        y -= 28.f;
    };

    addSlider("Scale", m_pctScaleSlider, m_pctScaleLabel,
        cfg.percentageScale, 0.3f, 2.5f,
        menu_selector(ProgressBarConfigPopup::onPctScaleChanged), "{:.2f}");
    addSlider("Offset X", m_pctOffXSlider, m_pctOffXLabel,
        cfg.percentageOffsetX, -200.f, 200.f,
        menu_selector(ProgressBarConfigPopup::onPctOffXChanged), "{:.0f}");
    addSlider("Offset Y", m_pctOffYSlider, m_pctOffYLabel,
        cfg.percentageOffsetY, -200.f, 200.f,
        menu_selector(ProgressBarConfigPopup::onPctOffYChanged), "{:.0f}");
}

// ──────────────────────────────────────────────────────────
// callbacks
// ──────────────────────────────────────────────────────────

void ProgressBarConfigPopup::applyAndSave() {
    ProgressBarManager::get().saveConfig();
}

void ProgressBarConfigPopup::onEnableToggled(CCObject*) {
    ProgressBarManager::get().config().enabled = !m_enableToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onVerticalToggled(CCObject*) {
    ProgressBarManager::get().config().vertical = !m_verticalToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onUseCustomPosToggled(CCObject*) {
    ProgressBarManager::get().config().useCustomPosition = !m_useCustomPosToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onFreeDragToggled(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    cfg.freeDragMode = !m_freeDragToggle->isToggled();
    // Free drag implies enabled + custom position
    if (cfg.freeDragMode) {
        cfg.enabled = true;
        cfg.useCustomPosition = true;
        if (m_enableToggle) m_enableToggle->toggle(true);
        if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(true);
        PaimonNotify::create("Free Drag enabled — drag the bar in pause", NotificationIcon::Info)->show();
    }
    applyAndSave();
}
void ProgressBarConfigPopup::onUseFillColorToggled(CCObject*) {
    ProgressBarManager::get().config().useCustomFillColor = !m_useFillColorToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onUseBgColorToggled(CCObject*) {
    ProgressBarManager::get().config().useCustomBgColor = !m_useBgColorToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onShowPctToggled(CCObject*) {
    ProgressBarManager::get().config().showPercentage = !m_showPctToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onUsePctColorToggled(CCObject*) {
    ProgressBarManager::get().config().useCustomPercentageColor = !m_usePctColorToggle->isToggled();
    applyAndSave();
}

void ProgressBarConfigPopup::onPickFillColor(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    auto cur = ccc4(cfg.fillColor.r, cfg.fillColor.g, cfg.fillColor.b, 255);
    auto* popup = geode::ColorPickPopup::create(cur);
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& color) {
        auto& c = ProgressBarManager::get().config();
        c.fillColor = {color.r, color.g, color.b};
        c.useCustomFillColor = true;
        if (m_useFillColorToggle) m_useFillColorToggle->toggle(true);
        if (m_fillColorPreview) m_fillColorPreview->setColor(c.fillColor);
        applyAndSave();
    });
    popup->show();
}

void ProgressBarConfigPopup::onPickBgColor(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    auto cur = ccc4(cfg.bgColor.r, cfg.bgColor.g, cfg.bgColor.b, 255);
    auto* popup = geode::ColorPickPopup::create(cur);
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& color) {
        auto& c = ProgressBarManager::get().config();
        c.bgColor = {color.r, color.g, color.b};
        c.useCustomBgColor = true;
        if (m_useBgColorToggle) m_useBgColorToggle->toggle(true);
        if (m_bgColorPreview) m_bgColorPreview->setColor(c.bgColor);
        applyAndSave();
    });
    popup->show();
}

void ProgressBarConfigPopup::onPickPctColor(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    auto cur = ccc4(cfg.percentageColor.r, cfg.percentageColor.g, cfg.percentageColor.b, 255);
    auto* popup = geode::ColorPickPopup::create(cur);
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& color) {
        auto& c = ProgressBarManager::get().config();
        c.percentageColor = {color.r, color.g, color.b};
        c.useCustomPercentageColor = true;
        if (m_usePctColorToggle) m_usePctColorToggle->toggle(true);
        if (m_pctColorPreview) m_pctColorPreview->setColor(c.percentageColor);
        applyAndSave();
    });
    popup->show();
}

void ProgressBarConfigPopup::onResetDefaults(CCObject*) {
    ProgressBarManager::get().resetToDefaults();
    PaimonNotify::create("Progress bar reset", NotificationIcon::Success)->show();
    // close popup so UI rebuilds next open
    this->onClose(nullptr);
}

void ProgressBarConfigPopup::onPickFont(CCObject*) {
    auto* picker = paimon::fonts::FontPickerPopup::create(
        [](std::string const& fontTag) {
            // FontPickerPopup hands us an already-formatted tag
            // ("<f:big> ") or "" for the None button. Pass it straight
            // to parseFontTag — wrapping it again would double-nest it.
            auto res = paimon::fonts::parseFontTag(fontTag);
            // No tag → revert to GD's default percentage font.
            std::string fntFile = res.hasTag ? res.fontFile : std::string("bigFont.fnt");
            ProgressBarManager::get().config().percentageFont = fntFile;
            ProgressBarManager::get().saveConfig();
            PaimonNotify::create("Font: " + fntFile, NotificationIcon::Success)->show();
        }
    );
    if (picker) picker->show();
}

void ProgressBarConfigPopup::onEnterFreeEditMode(CCObject*) {
    // Make sure the feature is enabled so changes take effect.
    auto& cfg = ProgressBarManager::get().config();
    if (!cfg.enabled) {
        cfg.enabled = true;
        if (m_enableToggle) m_enableToggle->toggle(true);
    }
    ProgressBarManager::get().saveConfig();
    ProgressBarEditOverlay::enterEditMode();
}

void ProgressBarConfigPopup::onCenterPosition(CCObject*) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& cfg = ProgressBarManager::get().config();
    cfg.posX = winSize.width / 2.f;
    cfg.posY = winSize.height - 20.f;
    cfg.useCustomPosition = true;
    if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(true);
    if (m_posXSlider) m_posXSlider->setValue(normFromValue(cfg.posX, 0.f, winSize.width));
    if (m_posYSlider) m_posYSlider->setValue(normFromValue(cfg.posY, 0.f, winSize.height));
    if (m_posXLabel)  m_posXLabel->setString(fmt::format("{:.0f}", cfg.posX).c_str());
    if (m_posYLabel)  m_posYLabel->setString(fmt::format("{:.0f}", cfg.posY).c_str());
    applyAndSave();
}

// ── sliders ──

void ProgressBarConfigPopup::onPosXChanged(CCObject*) {
    if (!m_posXSlider) return;
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& c = ProgressBarManager::get().config();
    c.posX = readSliderRange(m_posXSlider, 0.f, winSize.width);
    c.useCustomPosition = true;
    if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(true);
    if (m_posXLabel) m_posXLabel->setString(fmt::format("{:.0f}", c.posX).c_str());
}
void ProgressBarConfigPopup::onPosYChanged(CCObject*) {
    if (!m_posYSlider) return;
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& c = ProgressBarManager::get().config();
    c.posY = readSliderRange(m_posYSlider, 0.f, winSize.height);
    c.useCustomPosition = true;
    if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(true);
    if (m_posYLabel) m_posYLabel->setString(fmt::format("{:.0f}", c.posY).c_str());
}
void ProgressBarConfigPopup::onScaleLenChanged(CCObject*) {
    if (!m_scaleLenSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.scaleLength = readSliderRange(m_scaleLenSlider, 0.1f, 3.f);
    if (m_scaleLenLabel) m_scaleLenLabel->setString(fmt::format("{:.2f}", c.scaleLength).c_str());
}
void ProgressBarConfigPopup::onScaleThickChanged(CCObject*) {
    if (!m_scaleThickSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.scaleThickness = readSliderRange(m_scaleThickSlider, 0.1f, 3.f);
    if (m_scaleThickLabel) m_scaleThickLabel->setString(fmt::format("{:.2f}", c.scaleThickness).c_str());
}
void ProgressBarConfigPopup::onOpacityChanged(CCObject*) {
    if (!m_opacitySlider) return;
    auto& c = ProgressBarManager::get().config();
    c.opacity = static_cast<int>(readSliderRange(m_opacitySlider, 0.f, 255.f));
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}", c.opacity).c_str());
}
void ProgressBarConfigPopup::onPctScaleChanged(CCObject*) {
    if (!m_pctScaleSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.percentageScale = readSliderRange(m_pctScaleSlider, 0.3f, 2.5f);
    if (m_pctScaleLabel) m_pctScaleLabel->setString(fmt::format("{:.2f}", c.percentageScale).c_str());
}
void ProgressBarConfigPopup::onPctOffXChanged(CCObject*) {
    if (!m_pctOffXSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.percentageOffsetX = readSliderRange(m_pctOffXSlider, -200.f, 200.f);
    if (m_pctOffXLabel) m_pctOffXLabel->setString(fmt::format("{:.0f}", c.percentageOffsetX).c_str());
}
void ProgressBarConfigPopup::onPctOffYChanged(CCObject*) {
    if (!m_pctOffYSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.percentageOffsetY = readSliderRange(m_pctOffYSlider, -200.f, 200.f);
    if (m_pctOffYLabel) m_pctOffYLabel->setString(fmt::format("{:.0f}", c.percentageOffsetY).c_str());
}

// ──────────────────────────────────────────────────────────
// FX tab (color modes + custom textures)
// ──────────────────────────────────────────────────────────

namespace {
    const char* modeName(BarColorMode m) {
        switch (m) {
            case BarColorMode::Pulse:   return "Pulse";
            case BarColorMode::Rainbow: return "Rainbow";
            case BarColorMode::Solid:
            default:                    return "Solid";
        }
    }
    void setBtnLabel(CCMenuItemSpriteExtra* btn, const char* text) {
        if (!btn) return;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->setString(text);
        }
    }
    std::string shortPath(std::string const& p) {
        if (p.empty()) return "(no file)";
        std::filesystem::path fp(p);
        auto name = geode::utils::string::pathToString(fp.filename());
        if (name.size() > 24) name = name.substr(0, 22) + "..";
        return name;
    }
}

void ProgressBarConfigPopup::buildFxTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    auto& cfg = ProgressBarManager::get().config();

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_fxTab->addChild(menu, 2);

    float y = content.height - 75.f;

    auto addModeRow = [&](char const* title, BarColorMode mode,
                          ccColor3B c2, CCMenuItemSpriteExtra*& modeBtn,
                          CCLayerColor*& preview2, SEL_MenuHandler cycleCb,
                          SEL_MenuHandler pickCb) {
        auto lbl = CCLabelBMFont::create(title, "bigFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 175.f, y});
        m_fxTab->addChild(lbl);

        // mode cycler
        auto modeSpr = ButtonSprite::create(modeName(mode), "bigFont.fnt",
            "GJ_button_04.png", 0.7f);
        modeSpr->setScale(0.5f);
        modeBtn = CCMenuItemSpriteExtra::create(modeSpr, this, cycleCb);
        modeBtn->setPosition({cx - 50.f, y});
        menu->addChild(modeBtn);

        // secondary color preview (used for Pulse)
        preview2 = CCLayerColor::create({c2.r, c2.g, c2.b, 255}, 40.f, 22.f);
        preview2->setPosition({cx + 40.f, y - 11.f});
        m_fxTab->addChild(preview2);

        auto pickSpr = ButtonSprite::create("Pick 2", "goldFont.fnt",
            "GJ_button_04.png", 0.7f);
        pickSpr->setScale(0.5f);
        auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, pickCb);
        pickBtn->setPosition({cx + 135.f, y});
        menu->addChild(pickBtn);

        y -= 30.f;
    };

    addModeRow("Fill Mode",  cfg.fillColorMode, cfg.fillColor2,
               m_fillModeBtn, m_fillColor2Preview,
               menu_selector(ProgressBarConfigPopup::onCycleFillMode),
               menu_selector(ProgressBarConfigPopup::onPickFillColor2));
    addModeRow("Bg Mode",    cfg.bgColorMode,   cfg.bgColor2,
               m_bgModeBtn,   m_bgColor2Preview,
               menu_selector(ProgressBarConfigPopup::onCycleBgMode),
               menu_selector(ProgressBarConfigPopup::onPickBgColor2));
    addModeRow("Label Mode", cfg.pctColorMode,  cfg.pctColor2,
               m_pctModeBtn,  m_pctColor2Preview,
               menu_selector(ProgressBarConfigPopup::onCyclePctMode),
               menu_selector(ProgressBarConfigPopup::onPickPctColor2));

    // Animation speed slider.
    y -= 6.f;
    auto speedLbl = CCLabelBMFont::create("Anim Speed", "bigFont.fnt");
    speedLbl->setScale(0.4f);
    speedLbl->setAnchorPoint({0.f, 0.5f});
    speedLbl->setPosition({cx - 175.f, y});
    m_fxTab->addChild(speedLbl);

    m_colorAnimSpeedSlider = Slider::create(this,
        menu_selector(ProgressBarConfigPopup::onColorAnimSpeedChanged), 0.8f);
    m_colorAnimSpeedSlider->setPosition({cx + 15.f, y});
    m_colorAnimSpeedSlider->setValue(normFromValue(cfg.colorAnimSpeed, 0.1f, 5.f));
    m_fxTab->addChild(m_colorAnimSpeedSlider);

    m_colorAnimSpeedLabel = CCLabelBMFont::create(
        fmt::format("{:.2f}", cfg.colorAnimSpeed).c_str(), "bigFont.fnt");
    m_colorAnimSpeedLabel->setScale(0.4f);
    m_colorAnimSpeedLabel->setPosition({cx + 150.f, y});
    m_fxTab->addChild(m_colorAnimSpeedLabel);

    y -= 34.f;

    // ── Custom Textures ──
    auto hdr = CCLabelBMFont::create("Custom Textures (PNG/JPG/GIF)", "goldFont.fnt");
    hdr->setScale(0.5f);
    hdr->setPosition({cx, y});
    m_fxTab->addChild(hdr);
    y -= 28.f;

    auto addTexRow = [&](char const* title, CCMenuItemToggler*& toggle,
                         bool value, std::string const& path,
                         CCLabelBMFont*& pathLbl,
                         SEL_MenuHandler toggleCb,
                         SEL_MenuHandler pickCb,
                         SEL_MenuHandler clearCb) {
        auto lbl = CCLabelBMFont::create(title, "bigFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 175.f, y});
        m_fxTab->addChild(lbl);

        toggle = CCMenuItemToggler::createWithStandardSprites(this, toggleCb, 0.55f);
        toggle->setPosition({cx - 80.f, y});
        toggle->toggle(value);
        menu->addChild(toggle);

        auto pickSpr = ButtonSprite::create("Browse", "goldFont.fnt",
            "GJ_button_04.png", 0.7f);
        pickSpr->setScale(0.48f);
        auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, pickCb);
        pickBtn->setPosition({cx - 15.f, y});
        menu->addChild(pickBtn);

        auto clrSpr = ButtonSprite::create("X", "bigFont.fnt",
            "GJ_button_06.png", 0.7f);
        clrSpr->setScale(0.45f);
        auto clrBtn = CCMenuItemSpriteExtra::create(clrSpr, this, clearCb);
        clrBtn->setPosition({cx + 40.f, y});
        menu->addChild(clrBtn);

        pathLbl = CCLabelBMFont::create(shortPath(path).c_str(), "chatFont.fnt");
        pathLbl->setScale(0.45f);
        pathLbl->setAnchorPoint({0.f, 0.5f});
        pathLbl->setPosition({cx + 70.f, y});
        pathLbl->setOpacity(180);
        m_fxTab->addChild(pathLbl);

        y -= 28.f;
    };

    addTexRow("Fill Image", m_useFillTexToggle, cfg.useFillTexture,
              cfg.fillTexturePath, m_fillTexPathLabel,
              menu_selector(ProgressBarConfigPopup::onUseFillTextureToggled),
              menu_selector(ProgressBarConfigPopup::onPickFillTexture),
              menu_selector(ProgressBarConfigPopup::onClearFillTexture));
    addTexRow("Bg Image", m_useBgTexToggle, cfg.useBgTexture,
              cfg.bgTexturePath, m_bgTexPathLabel,
              menu_selector(ProgressBarConfigPopup::onUseBgTextureToggled),
              menu_selector(ProgressBarConfigPopup::onPickBgTexture),
              menu_selector(ProgressBarConfigPopup::onClearBgTexture));

    auto info = CCLabelBMFont::create(
        "Pulse = blend primary<->secondary   Rainbow = HSV cycle",
        "chatFont.fnt");
    info->setScale(0.42f);
    info->setAlignment(kCCTextAlignmentCenter);
    info->setPosition({cx, 42.f});
    info->setOpacity(160);
    m_fxTab->addChild(info);
}

void ProgressBarConfigPopup::refreshFxTab() {
    auto& cfg = ProgressBarManager::get().config();
    setBtnLabel(m_fillModeBtn, modeName(cfg.fillColorMode));
    setBtnLabel(m_bgModeBtn,   modeName(cfg.bgColorMode));
    setBtnLabel(m_pctModeBtn,  modeName(cfg.pctColorMode));
    if (m_fillColor2Preview) m_fillColor2Preview->setColor(cfg.fillColor2);
    if (m_bgColor2Preview)   m_bgColor2Preview->setColor(cfg.bgColor2);
    if (m_pctColor2Preview)  m_pctColor2Preview->setColor(cfg.pctColor2);
    if (m_fillTexPathLabel)  m_fillTexPathLabel->setString(shortPath(cfg.fillTexturePath).c_str());
    if (m_bgTexPathLabel)    m_bgTexPathLabel->setString(shortPath(cfg.bgTexturePath).c_str());
}

// ── Mode cyclers ──
static BarColorMode nextMode(BarColorMode m) {
    int i = (static_cast<int>(m) + 1) % 3;
    return static_cast<BarColorMode>(i);
}
void ProgressBarConfigPopup::onCycleFillMode(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.fillColorMode = nextMode(c.fillColorMode);
    refreshFxTab();
    applyAndSave();
}
void ProgressBarConfigPopup::onCycleBgMode(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.bgColorMode = nextMode(c.bgColorMode);
    refreshFxTab();
    applyAndSave();
}
void ProgressBarConfigPopup::onCyclePctMode(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.pctColorMode = nextMode(c.pctColorMode);
    refreshFxTab();
    applyAndSave();
}

// ── Secondary color pickers ──

void ProgressBarConfigPopup::onPickFillColor2(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    auto* popup = geode::ColorPickPopup::create(ccc4(c.fillColor2.r, c.fillColor2.g, c.fillColor2.b, 255));
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& col) {
        auto& cc = ProgressBarManager::get().config();
        cc.fillColor2 = {col.r, col.g, col.b};
        if (m_fillColor2Preview) m_fillColor2Preview->setColor(cc.fillColor2);
        applyAndSave();
    });
    popup->show();
}
void ProgressBarConfigPopup::onPickBgColor2(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    auto* popup = geode::ColorPickPopup::create(ccc4(c.bgColor2.r, c.bgColor2.g, c.bgColor2.b, 255));
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& col) {
        auto& cc = ProgressBarManager::get().config();
        cc.bgColor2 = {col.r, col.g, col.b};
        if (m_bgColor2Preview) m_bgColor2Preview->setColor(cc.bgColor2);
        applyAndSave();
    });
    popup->show();
}
void ProgressBarConfigPopup::onPickPctColor2(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    auto* popup = geode::ColorPickPopup::create(ccc4(c.pctColor2.r, c.pctColor2.g, c.pctColor2.b, 255));
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& col) {
        auto& cc = ProgressBarManager::get().config();
        cc.pctColor2 = {col.r, col.g, col.b};
        if (m_pctColor2Preview) m_pctColor2Preview->setColor(cc.pctColor2);
        applyAndSave();
    });
    popup->show();
}

void ProgressBarConfigPopup::onColorAnimSpeedChanged(CCObject*) {
    if (!m_colorAnimSpeedSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.colorAnimSpeed = readSliderRange(m_colorAnimSpeedSlider, 0.1f, 5.f);
    if (m_colorAnimSpeedLabel)
        m_colorAnimSpeedLabel->setString(fmt::format("{:.2f}", c.colorAnimSpeed).c_str());
}

// ── Texture toggles + picks ──
void ProgressBarConfigPopup::onUseFillTextureToggled(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.useFillTexture = !c.useFillTexture;
    applyAndSave();
}
void ProgressBarConfigPopup::onUseBgTextureToggled(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.useBgTexture = !c.useBgTexture;
    applyAndSave();
}
void ProgressBarConfigPopup::onPickFillTexture(CCObject*) {
    WeakRef<ProgressBarConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        auto opt = std::move(res).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;
        auto popup = self.lock();
        if (!popup) return;

        auto imported = paimon::assets::importToBucket(*opt, "progressbar_fill", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import fill image", NotificationIcon::Error)->show();
            return;
        }

        auto& c = ProgressBarManager::get().config();
        c.fillTexturePath = paimon::assets::normalizePathString(imported.path);
        c.useFillTexture = true;
        if (popup->m_useFillTexToggle) popup->m_useFillTexToggle->toggle(true);
        static_cast<ProgressBarConfigPopup*>(popup.data())->refreshFxTab();
        static_cast<ProgressBarConfigPopup*>(popup.data())->applyAndSave();
        PaimonNotify::create("Fill image set", NotificationIcon::Success)->show();
    });
}
void ProgressBarConfigPopup::onPickBgTexture(CCObject*) {
    WeakRef<ProgressBarConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        auto opt = std::move(res).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;
        auto popup = self.lock();
        if (!popup) return;

        auto imported = paimon::assets::importToBucket(*opt, "progressbar_bg", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import background image", NotificationIcon::Error)->show();
            return;
        }

        auto& c = ProgressBarManager::get().config();
        c.bgTexturePath = paimon::assets::normalizePathString(imported.path);
        c.useBgTexture = true;
        if (popup->m_useBgTexToggle) popup->m_useBgTexToggle->toggle(true);
        static_cast<ProgressBarConfigPopup*>(popup.data())->refreshFxTab();
        static_cast<ProgressBarConfigPopup*>(popup.data())->applyAndSave();
        PaimonNotify::create("Bg image set", NotificationIcon::Success)->show();
    });
}
void ProgressBarConfigPopup::onClearFillTexture(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.fillTexturePath.clear();
    c.useFillTexture = false;
    if (m_useFillTexToggle) m_useFillTexToggle->toggle(false);
    refreshFxTab();
    applyAndSave();
}
void ProgressBarConfigPopup::onClearBgTexture(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.bgTexturePath.clear();
    c.useBgTexture = false;
    if (m_useBgTexToggle) m_useBgTexToggle->toggle(false);
    refreshFxTab();
    applyAndSave();
}
