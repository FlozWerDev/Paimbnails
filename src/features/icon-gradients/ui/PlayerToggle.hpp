#pragma once
#include <Geode/Geode.hpp>

namespace paimon::icon_gradients {

using namespace geode::prelude;

class GradientLayer;

class PlayerToggle : public CCNode {

private:

    CCMenuItemSpriteExtra* m_p1Btn = nullptr;
    CCMenuItemSpriteExtra* m_p2Btn = nullptr;

    ButtonSprite* m_p1Sprite = nullptr;
    ButtonSprite* m_p2Sprite = nullptr;

    GradientLayer* m_layer;

    bool m_isToggled = false;

    bool init() override;

    void onP1(CCObject*);
    void onP2(CCObject*);

    PlayerToggle(GradientLayer* layer)
        : m_layer(layer) {}

public:

    static PlayerToggle* create(GradientLayer* layer);

    void toggle(bool);

    bool isToggled();

};

} // namespace paimon::icon_gradients
