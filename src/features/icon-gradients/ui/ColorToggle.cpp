// Channel toggle: color swatch previewing one gradient channel, optional index
// badge, crossfade sprite for transitions. After zilko's "Icon Gradients" (independent implementation; idea credit zilko144, unlicensed).

#include "ColorToggle.hpp"
#include "GradientLayer.hpp"
#include "../GradientUtils.hpp"

#include <utility>

using namespace geode::prelude;
using namespace paimon::icon_gradients;

ColorToggle* ColorToggle::create(CCObject* target, SEL_MenuHandler callback, ColorType colorType, GradientLayer* layer, bool number, float scale, bool shouldCache) {
    auto ret = new ColorToggle();

    ret->m_colorType = colorType;
    ret->m_layer = layer;
    ret->m_shouldCache = shouldCache;

    if (!ret->init(target, callback, number, scale)) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

void ColorToggle::fitToSprite() {
    setContentSize(m_sprite->getContentSize());
    setAnchorPoint({0, 0});
}

namespace {

// Fresh color-button swatch at the toggle's scale.
CCSprite* makeSwatch(float scale) {
    CCSprite* swatch = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    swatch->setScale(0.6f * scale);
    return swatch;
}

} // namespace

void ColorToggle::addSelectOverlay(float scale) {
    m_select = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
    m_select->setScale(0.7f * scale);
    m_select->setVisible(false);

    CCPoint center = m_sprite->getContentSize() * m_sprite->getScale() / 2.f;
    m_select->setPosition(center);

    addChild(m_select);
}

// Index badge shown on numbered channel buttons.
static CCLabelBMFont* makeIndexBadge(std::string const& text, float scale) {
    CCLabelBMFont* badge = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    badge->setScale(0.315f * scale);
    badge->setPosition({16, 8});
    return badge;
}

bool ColorToggle::init(CCObject* target, SEL_MenuHandler callback, bool number, float scale) {
    m_sprite = makeSwatch(scale);

    addChild(m_sprite);

    if (!number) {
        addSelectOverlay(scale);

        fitToSprite();

        return CCMenuItemSpriteExtra::init(m_sprite, nullptr, target, callback);
    }

    // The White/Line channels display each other's index.
    std::string numberStr = std::to_string(static_cast<int>(m_colorType));
    if (numberStr == "4") {
        numberStr = "5";
    } else if (numberStr == "5") {
        numberStr = "4";
    }

    m_numberLabel = makeIndexBadge(numberStr, scale);

    addChild(m_numberLabel, 10);

    m_secondSprite = makeSwatch(scale);
    m_secondSprite->setOpacity(0);

    addChild(m_secondSprite);

    addSelectOverlay(scale);

    fitToSprite();

    // Capture first: a failed init discards the button, so positioning the
    // crossfade sprite before checking is unobservable.
    bool ok = CCMenuItemSpriteExtra::init(m_sprite, nullptr, target, callback);

    m_secondSprite->setPosition(m_sprite->getPosition());
    m_secondSprite->setZOrder(m_sprite->getZOrder() + 1);

    return ok;
}

void ColorToggle::setSelected(bool selected) {
    // Order is free: setEnabled reads neither flag nor overlay.
    m_isSelected = selected;
    m_select->setVisible(selected);
    setEnabled(!m_isSelected);
}

void ColorToggle::setColor(const ccColor3B& tint, float time) {
    auto action = CCTintTo::create(time, tint.r, tint.g, tint.b);
    m_sprite->runAction(action);
}

ccColor3B ColorToggle::slotColor(GradientConfig const& config, bool second) const {
    if (config.isEmpty(m_colorType, second))
        return GradientUtils::getPlayerColor(m_colorType, second);
    return ccc3(255, 255, 255);
}

void ColorToggle::paintSlot(CCSprite* slot, GradientConfig const& config, bool blend) const {
    GradientUtils::applyGradient(slot, config, static_cast<IconType>(-1), m_colorType,
        static_cast<int>(m_colorType), blend, false, false, m_shouldCache ? 120 : -4732);
}

void ColorToggle::applyGradient(GradientConfig config, bool /*force*/, bool transition) {
    if (m_secondSprite && config == m_currentConfig) return;

    // Read once: nothing below flips the player side mid-call.
    bool second = m_layer->isSecondPlayer();

    if (config.isEmpty(m_colorType, second))
        config.points.push_back(SimplePoint{{0, 0}, GradientUtils::getPlayerColor(m_colorType, second)});

    ccColor3B targetColor = slotColor(config, second);

    if (transition && !m_currentConfig.isEmpty(m_colorType, second) && m_secondSprite)
        m_secondSprite->setColor({255, 255, 255});

    // crossfade only with a second sprite; empty targets still run the fade below.
    bool animate = transition && m_secondSprite;
    bool empty = config.isEmpty(m_colorType, second);

    if (animate && !empty)
        paintSlot(m_secondSprite, m_currentConfig, true);

    m_sprite->setColor(targetColor);

    if (m_secondSprite && (!animate || empty))
        m_secondSprite->setColor(targetColor);

    // Taken by value; nothing below reads the parameter again.
    m_currentConfig = std::move(config);

    m_sprite->setOpacity(255);

    if (!animate) {
        paintSlot(m_sprite, m_currentConfig, false);
        return;
    }

    m_secondSprite->setOpacity(0);

    for (CCSprite* spr : {m_sprite, m_secondSprite}) spr->stopAllActions();

    paintSlot(m_sprite, m_currentConfig, true);

    m_secondSprite->setOpacity(255);

    auto fade = CCFadeTo::create(0.2f, 0);
    auto done = CCCallFunc::create(this, callfunc_selector(ColorToggle::onAnimationEnded));
    m_secondSprite->runAction(CCSequence::create(fade, done, nullptr));
}

void ColorToggle::onAnimationEnded() {
    m_secondSprite->setColor(slotColor(m_currentConfig, m_layer->isSecondPlayer()));

    paintSlot(m_secondSprite, m_currentConfig, true);

    m_sprite->setOpacity(255);
    m_secondSprite->setOpacity(0);
}

void ColorToggle::setForceDisabled(bool off) {
    // sandwich: first call runs under the previous flag, second applies the new one.
    setEnabled(!off);

    m_forceDisabled = off;

    setEnabled(!off);

    auto dimTo = [](CCNode* node, GLubyte opacity) {
        node->runAction(CCFadeTo::create(0.2f, opacity));
    };

    if (m_numberLabel)
        dimTo(m_numberLabel, off ? 60 : 255);

    if (m_dimSprite) {
        dimTo(m_dimSprite, off ? 170 : 0);
        return;
    }

    m_dimSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    m_dimSprite->setPosition(getContentSize() / 2.f);
    m_dimSprite->setScale(m_sprite->getScale());
    m_dimSprite->setColor({0, 0, 0});
    m_dimSprite->setOpacity(0);
    dimTo(m_dimSprite, off ? 170 : 0);

    addChild(m_dimSprite, 9);
}

bool ColorToggle::isSelected() { return m_isSelected; }
ColorType ColorToggle::getColorType() { return m_colorType; }
CCSprite* ColorToggle::getMainSprite() { return m_sprite; }

void ColorToggle::setEnabled(bool enabled) {
    if (m_forceDisabled) return;

    CCMenuItemSpriteExtra::setEnabled(enabled);

    if (m_secondSprite) return;
    m_sprite->setOpacity(enabled ? 255 : 85);
}
