#include "ScoreCellSettingsPopup.hpp"
#include "../ScoreCellSettings.hpp"
#include "../../profiles/services/ProfileGradientEffects.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::scorecell {

namespace {
    size_t indexOf(std::vector<std::string> const& list, std::string const& v) {
        for (size_t i = 0; i < list.size(); ++i) if (list[i] == v) return i;
        return 0;
    }

    std::string nextOf(std::vector<std::string> const& list, std::string const& cur) {
        if (list.empty()) return cur;
        size_t i = (indexOf(list, cur) + 1) % list.size();
        return list[i];
    }

    float speedToSlider(float s)  { return std::clamp((s - 0.1f) / 4.9f, 0.f, 1.f); }
    float sliderToSpeed(float v)  { return 0.1f + std::clamp(v, 0.f, 1.f) * 4.9f; }
}

ScoreCellSettingsPopup* ScoreCellSettingsPopup::create() {
    auto ret = new ScoreCellSettingsPopup();
    if (ret && ret->initContents()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ScoreCellSettingsPopup::initContents() {
    if (!Popup::init(440.f, 360.f)) return false;

    this->setTitle("Score Cell FX");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 5);

    auto addSectionLabel = [&](const char* text, float x, float y) {
        auto lbl = CCLabelBMFont::create(text, "goldFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({x, y});
        m_mainLayer->addChild(lbl);
    };
    auto addSmall = [&](const char* text, float x, float y, float anchorX = 0.f) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.34f);
        lbl->setAnchorPoint({anchorX, 0.5f});
        lbl->setPosition({x, y});
        m_mainLayer->addChild(lbl);
        return lbl;
    };

    auto makeToggle = [&](bool on, SEL_MenuHandler sel, float x, float y) {
        auto off = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOff_001.png");
        if (!off) off = CCSprite::create();
        auto onS = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png");
        if (!onS) onS = CCSprite::create();
        auto t = CCMenuItemToggler::create(off, onS, this, sel);
        t->setScale(0.7f);
        t->setPosition({x, y});
        t->toggle(on);
        menu->addChild(t);
        return t;
    };

    auto makeCycle = [&](std::string const& text, SEL_MenuHandler sel, float x, float y) -> ButtonSprite* {
        auto spr = ButtonSprite::create(text.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setScale(0.74f);
        btn->setPosition({x, y});
        menu->addChild(btn);
        return spr;
    };

    auto addSlider = [&](float value, SEL_MenuHandler sel, float y) {
        auto s = Slider::create(this, sel, 0.7f);
        s->setPosition({cx, y});
        s->setValue(std::clamp(value, 0.f, 1.f));
        m_mainLayer->addChild(s);
        return s;
    };

    // ── Preview bar ─────────────────────────────────────────────
    float previewW = 300.f, previewH = 32.f;
    float py = content.height - 56.f;
    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({previewW, previewH});
    m_previewContainer->setAnchorPoint({0.5f, 0.5f});
    m_previewContainer->ignoreAnchorPointForPosition(false);
    m_previewContainer->setPosition({cx, py});
    m_mainLayer->addChild(m_previewContainer);
    rebuildPreview();

    const float kLeft  = 24.f;
    const float kRight = content.width - 18.f;

    // ── Gradient section ────────────────────────────────────────
    float y = py - 42.f;
    addSectionLabel("Icon Gradient", kLeft, y);
    addSmall("Enable", kRight - 24.f, y, 1.f);
    makeToggle(gradientEnabled(), menu_selector(ScoreCellSettingsPopup::onToggleGradient), kRight - 8.f, y);

    y -= 30.f;
    addSmall("Effect:", kLeft, y);
    m_effectBtnSprite = makeCycle(gradientEffect(), menu_selector(ScoreCellSettingsPopup::onCycleEffect), 118.f, y);

    y -= 32.f;
    addSmall("Speed", kLeft, y);
    auto speedSlider = addSlider(speedToSlider(gradientSpeed()), menu_selector(ScoreCellSettingsPopup::onSpeed), y);
    (void)speedSlider;
    m_speedLabel = addSmall("", kRight, y, 1.f);

    y -= 30.f;
    addSmall("Opacity", kLeft, y);
    addSlider(gradientOpacity() / 255.f, menu_selector(ScoreCellSettingsPopup::onOpacity), y);
    m_opacityLabel = addSmall("", kRight, y, 1.f);

    // ── Hover section ───────────────────────────────────────────
    y -= 38.f;
    addSectionLabel("Hover Animation", kLeft, y);
    addSmall("Enable", kRight - 24.f, y, 1.f);
    makeToggle(hoverEnabled(), menu_selector(ScoreCellSettingsPopup::onToggleHover), kRight - 8.f, y);

    y -= 30.f;
    addSmall("Type:", kLeft, y);
    m_hoverBtnSprite = makeCycle(hoverType(), menu_selector(ScoreCellSettingsPopup::onCycleHover), 110.f, y);

    y -= 32.f;
    addSmall("Power", kLeft, y);
    addSlider(hoverIntensity(), menu_selector(ScoreCellSettingsPopup::onIntensity), y);
    m_intensityLabel = addSmall("", kRight, y, 1.f);

    // ── Entrance section ────────────────────────────────────────
    y -= 38.f;
    addSectionLabel("Entrance", kLeft, y);
    addSmall("Type:", cx - 6.f, y);
    m_entranceBtnSprite = makeCycle(entranceType(), menu_selector(ScoreCellSettingsPopup::onCycleEntrance), cx + 110.f, y);

    // ── Done + Info buttons ─────────────────────────────────────
    auto doneSpr = ButtonSprite::create("Done", "bigFont.fnt", "GJ_button_01.png", 0.8f);
    auto doneBtn = CCMenuItemSpriteExtra::create(doneSpr, this, menu_selector(ScoreCellSettingsPopup::onClose));
    doneBtn->setPosition({cx, 26.f});
    menu->addChild(doneBtn);

    {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(18.f / maxDim);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ScoreCellSettingsPopup::onInfo));
        btn->setPosition({content.width - 16.f, content.height - 16.f});
        menu->addChild(btn);
    }

    refreshLabels();

    this->setZOrder(10520);
    this->setID("scorecell-settings-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}

void ScoreCellSettingsPopup::refreshLabels() {
    if (auto* spr = typeinfo_cast<ButtonSprite*>(m_effectBtnSprite.data()))
        spr->setString(gradientEffect().c_str());
    if (auto* spr = typeinfo_cast<ButtonSprite*>(m_hoverBtnSprite.data()))
        spr->setString(hoverType().c_str());
    if (auto* spr = typeinfo_cast<ButtonSprite*>(m_entranceBtnSprite.data()))
        spr->setString(entranceType().c_str());

    if (m_speedLabel) m_speedLabel->setString(fmt::format("{:.1f}x", gradientSpeed()).c_str());
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}%", static_cast<int>(gradientOpacity() / 255.f * 100.f)).c_str());
    if (m_intensityLabel) m_intensityLabel->setString(fmt::format("{}%", static_cast<int>(hoverIntensity() * 100.f)).c_str());
}

void ScoreCellSettingsPopup::rebuildPreview() {
    if (!m_previewContainer) return;
    m_previewContainer->removeAllChildrenWithCleanup(true);

    auto sz = m_previewContainer->getContentSize();

    // Dark backing so gradient opacity is visible.
    auto backing = paimon::SpriteHelper::createColorPanel(sz.width, sz.height, {12, 12, 16}, 255, 3.f);
    if (backing) {
        backing->setAnchorPoint({0.f, 0.f});
        backing->setPosition({0.f, 0.f});
        m_previewContainer->addChild(backing);
    }

    auto* gm = GameManager::sharedState();
    ccColor3B a = gm ? gm->colorForIdx(gm->getPlayerColor())  : ccColor3B{255, 80, 80};
    ccColor3B b = gm ? gm->colorForIdx(gm->getPlayerColor2()) : ccColor3B{80, 80, 255};

    auto stencil = paimon::SpriteHelper::createRectStencil(sz.width, sz.height);
    auto clip = CCClippingNode::create();
    clip->setStencil(stencil);
    clip->setAlphaThreshold(0.05f);
    clip->setContentSize(sz);
    clip->setAnchorPoint({0.f, 0.f});
    clip->setPosition({0.f, 0.f});
    m_previewContainer->addChild(clip);

    auto* grad = paimon::profilebg::AnimatedGradientLayer::create(a, b);
    if (grad) {
        grad->setContentSize(sz);
        grad->setAnchorPoint({0.5f, 0.5f});
        grad->ignoreAnchorPointForPosition(false);
        grad->setPosition({sz.width * 0.5f, sz.height * 0.5f});
        grad->setOpacity(static_cast<GLubyte>(gradientOpacity()));
        clip->addChild(grad);
        grad->setEffect(gradientEffect(), gradientSpeed());
    }

    // Hint label when gradient disabled.
    if (!gradientEnabled()) {
        auto hint = CCLabelBMFont::create("gradient off", "bigFont.fnt");
        hint->setScale(0.3f);
        hint->setOpacity(150);
        hint->setPosition({sz.width / 2.f, sz.height / 2.f});
        m_previewContainer->addChild(hint);
    }
}

void ScoreCellSettingsPopup::onToggleGradient(CCObject*) {
    setGradientEnabled(!gradientEnabled());
    rebuildPreview();
}

void ScoreCellSettingsPopup::onToggleHover(CCObject*) {
    setHoverEnabled(!hoverEnabled());
}

void ScoreCellSettingsPopup::onCycleEffect(CCObject*) {
    setGradientEffect(nextOf(gradientEffects(), gradientEffect()));
    refreshLabels();
    rebuildPreview();
}

void ScoreCellSettingsPopup::onCycleHover(CCObject*) {
    setHoverType(nextOf(hoverTypes(), hoverType()));
    refreshLabels();
}

void ScoreCellSettingsPopup::onCycleEntrance(CCObject*) {
    setEntranceType(nextOf(entranceTypes(), entranceType()));
    refreshLabels();
}

void ScoreCellSettingsPopup::onSpeed(CCObject* sender) {
    auto* s = typeinfo_cast<Slider*>(sender);
    if (!s) return;
    setGradientSpeed(sliderToSpeed(s->getValue()));
    refreshLabels();
    rebuildPreview();
}

void ScoreCellSettingsPopup::onOpacity(CCObject* sender) {
    auto* s = typeinfo_cast<Slider*>(sender);
    if (!s) return;
    setGradientOpacity(static_cast<int>(std::clamp(s->getValue(), 0.f, 1.f) * 255.f));
    refreshLabels();
    rebuildPreview();
}

void ScoreCellSettingsPopup::onIntensity(CCObject* sender) {
    auto* s = typeinfo_cast<Slider*>(sender);
    if (!s) return;
    setHoverIntensity(s->getValue());
    refreshLabels();
}

void ScoreCellSettingsPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        "Score Cell FX",
        "Customize how leaderboard cells look:\n\n"
        "<cy>Icon Gradient</c>: paints a gradient behind every cell using that "
        "player's own icon colors. Pick an animated <cj>effect</c>, speed and opacity.\n\n"
        "<cg>Hover Animation</c>: a fluid effect when your mouse is over a cell "
        "(scale, glow, lift, tilt or shine).\n\n"
        "<cp>Entrance</c>: how each cell's profile banner appears.\n\n"
        "Changes apply to cells as you scroll or reopen the list.",
        "OK"
    )->show();
}

void ScoreCellSettingsPopup::onClose(CCObject* sender) {
    auto cb = m_onCloseCb;
    Popup::onClose(sender);
    if (cb) cb();
}

} // namespace paimon::scorecell
