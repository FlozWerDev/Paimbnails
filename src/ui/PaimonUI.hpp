#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <functional>

namespace paimon::ui {

namespace palette {
inline constexpr cocos2d::ccColor3B background{11, 16, 29};
inline constexpr cocos2d::ccColor3B surface{20, 28, 45};
inline constexpr cocos2d::ccColor3B raised{29, 40, 61};
inline constexpr cocos2d::ccColor3B border{55, 72, 99};
inline constexpr cocos2d::ccColor3B text{237, 244, 255};
inline constexpr cocos2d::ccColor3B muted{162, 180, 205};
inline constexpr cocos2d::ccColor3B accent{116, 204, 255};
inline constexpr cocos2d::ccColor3B violet{182, 161, 255};
inline constexpr cocos2d::ccColor3B success{118, 224, 181};
inline constexpr cocos2d::ccColor3B warning{255, 208, 128};
inline constexpr cocos2d::ccColor3B danger{255, 137, 155};
}

bool motionEnabled();
float motionDuration(float seconds);
cocos2d::ccColor3B actionColor(char const* skin);
cocos2d::CCNodeRGBA* makeSurface(cocos2d::CCSize size,
    cocos2d::ccColor3B color = palette::surface, GLubyte opacity = 255,
    float radius = 8.f);
cocos2d::CCSprite* makeButtonFace(char const* text, cocos2d::CCSize size,
    cocos2d::ccColor3B color = palette::raised, float textScale = 0.36f);
CCMenuItemSpriteExtra* makeButton(char const* text, cocos2d::CCSize size,
    std::function<void()> onPress, cocos2d::ccColor3B color = palette::raised);
CCMenuItemToggler* makeSwitch(cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler callback, bool value, float scale = 1.f);
void animateIn(cocos2d::CCNode* node, float delay = 0.f, float distance = 6.f);
void decorateScene(cocos2d::CCNode* parent);

}
