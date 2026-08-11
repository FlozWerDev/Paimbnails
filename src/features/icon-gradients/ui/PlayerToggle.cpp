#include "PlayerToggle.hpp"
#include "GradientLayer.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

PlayerToggle* PlayerToggle::create(GradientLayer* layer) {
    PlayerToggle* ret = new PlayerToggle(layer);

    ret->init();
    ret->autorelease();

    return ret;
}

void PlayerToggle::toggle(bool toggled) {
    m_isToggled = toggled;

    m_p1Sprite->updateBGImage(toggled ? "GJ_button_04.png" : "GJ_button_01.png");
    m_p2Sprite->updateBGImage(toggled ? "GJ_button_01.png" : "GJ_button_04.png");

    m_p1Btn->setEnabled(toggled);
    m_p2Btn->setEnabled(!toggled);
}

void PlayerToggle::onP1(CCObject*) {
    toggle(false);

    m_layer->onPlayerToggle(this);
}

void PlayerToggle::onP2(CCObject*) {
    toggle(true);

    m_layer->onPlayerToggle(this);
}

bool PlayerToggle::isToggled() {
    return m_isToggled;
}

bool PlayerToggle::init() {
    if (!CCNode::init()) return false;

    setContentSize({36.f, 42.f});
    setAnchorPoint({0.5f, 0.5f});

    CCMenu* menu = CCMenu::create();
    menu->setPosition(getContentSize() / 2.f);

    addChild(menu);

    m_p1Sprite = ButtonSprite::create(
        "P1", 46, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.45f
    );
    m_p1Sprite->setScale(0.7f);

    m_p1Btn = CCMenuItemSpriteExtra::create(m_p1Sprite, this, menu_selector(PlayerToggle::onP1));
    m_p1Btn->setPosition({0.f, 10.f});

    menu->addChild(m_p1Btn);

    m_p2Sprite = ButtonSprite::create(
        "P2", 46, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.45f
    );
    m_p2Sprite->setScale(0.7f);

    m_p2Btn = CCMenuItemSpriteExtra::create(m_p2Sprite, this, menu_selector(PlayerToggle::onP2));
    m_p2Btn->setPosition({0.f, -10.f});

    menu->addChild(m_p2Btn);

    toggle(false);

    return true;
}
