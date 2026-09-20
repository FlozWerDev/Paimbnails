#include "IconButton.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

IconButton* IconButton::create(CCObject* target, SEL_MenuHandler callback, IconType type, bool secondPlayer) {
    auto ret = new IconButton();

    ret->m_type = type;
    ret->m_isSecondPlayer = secondPlayer;

    if (!ret->init(target, callback)) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

bool IconButton::init(CCObject* target, SEL_MenuHandler callback) {
    m_icon = GradientUtils::createIcon(m_type, m_isSecondPlayer);

    CCSize buttonSize = {30.f, 30.f};
    CCPoint center = buttonSize / 2.f;

    auto background = CCSprite::create("GJ_button_04.png");
    background->setScale(0.66f);
    background->setPosition(center);

    GradientUtils::fitIcon(m_icon, {24.f, 24.f}, center);

    CCNode* container = CCNode::create();
    container->setContentSize(buttonSize);
    container->addChild(background);
    container->addChild(m_icon);

    CCPoint dotPos = center + ccp(9.f, -9.f);

    m_dot = ColorNode::create(false);
    m_dot->setScale(0.3f);
    m_dot->setPosition(dotPos);

    container->addChild(m_dot);

    m_secondDot = ColorNode::create(false);
    m_secondDot->setScale(0.3f);
    m_secondDot->setPosition(dotPos);
    m_secondDot->setHidden(true, 0.f);

    container->addChild(m_secondDot);

    m_select = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
    m_select->setScale(0.62f);
    m_select->setPosition(center);
    m_select->setVisible(false);

    container->addChild(m_select);

    return CCMenuItemSpriteExtra::init(container, nullptr, target, callback);
}

void IconButton::paintDot(GradientConfig const& config, int id, bool blend, int extra) {
    GradientUtils::applyGradient(m_dot->getSprite(), config, m_type,
        static_cast<ColorType>(-1), id, blend, false, false, extra);
}

void IconButton::settleDot(float wait) {
    runAction(CCSequence::create(
        CCDelayTime::create(wait),
        CCCallFunc::create(this, callfunc_selector(IconButton::onAnimationEnded)),
        nullptr
    ));
}

void IconButton::setLocked(bool locked, bool instant) {
    m_isLocked = locked;

    if (!instant)
        m_dot->flash(0.3f);

    if (locked) {
        m_dot->setHidden(false, 0.f, true);
        return;
    }

    m_didForce = true;

    float wait = instant ? 0.f : 0.1f;
    paintDot(m_currentConfig, 1, true, 121);
    m_dot->setHidden(true, wait);
    settleDot(wait);
}

void IconButton::applyGradient(bool force, ColorType colorType, bool transition, bool all, bool secondPlayer) {
    GradientConfig previousConfig = m_currentConfig;

    m_currentConfig = GradientUtils::getSavedConfig(m_type, colorType, secondPlayer);

    if (all) {
        Gradient gradient = GradientUtils::getGradient(m_type, secondPlayer);
        GradientUtils::applyGradient(m_icon, gradient, false, secondPlayer, 121);
    } else {
        GradientUtils::applyGradient(m_icon, m_currentConfig, colorType, false, secondPlayer, 121);
    }

    // An empty slot shows the raw player color; a filled one stays white.
    ccColor3B dot = m_currentConfig.isEmpty(colorType, secondPlayer)
        ? GradientUtils::getPlayerColor(colorType, secondPlayer)
        : ccc3(255, 255, 255);
    m_dot->setColor(dot);
    m_secondDot->setColor(dot);

    if (!transition || !isLocked() || previousConfig == m_currentConfig) {
        paintDot(m_currentConfig, -1, false, 123);
        return;
    }

    paintDot(m_currentConfig, -1, true, 123);
    GradientUtils::applyGradient(m_secondDot->getSprite(), previousConfig, m_type, static_cast<ColorType>(-1), -1, true, false, false, 124);

    m_secondDot->setHidden(false, 0.f);
    m_secondDot->setHidden(true, 0.1f);

    m_didForce = force;

    settleDot(0.1f);
}

void IconButton::updateSprite(bool secondPlayer) {
    m_isSecondPlayer = secondPlayer;

    m_icon->updatePlayerFrame(GradientUtils::getIconID(m_type, secondPlayer), m_type);

    GradientUtils::fitIcon(m_icon, {24.f, 24.f}, {15.f, 15.f});
}

void IconButton::onAnimationEnded() {
    paintDot(m_currentConfig, -1, false, 123);

    m_dot->setHidden(!m_isLocked, 0.f, true);
    m_secondDot->setHidden(true, 0.f, true);
}

IconType IconButton::getType() {
    return m_type;
}

bool IconButton::isLocked() {
    return m_isLocked;
}

void IconButton::setSelected(bool selected) {
    m_select->setVisible(selected);
}

void IconButton::setColor(ColorType colorType, bool white, bool secondPlayer) {
    GradientUtils::setIconColors(m_icon, colorType, white, secondPlayer);
}
