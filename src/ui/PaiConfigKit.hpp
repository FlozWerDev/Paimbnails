#pragma once

// Shared building blocks for configuration popups: labeled rows, visible
// values, section cards, and consistent scrolling.

#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <functional>
#include <string>
#include <vector>

namespace paimon::configkit {

// Shared palette.
constexpr cocos2d::ccColor3B kCardColor  = {14, 18, 32};
constexpr GLubyte            kCardAlpha  = 145;
constexpr cocos2d::ccColor3B kTitleColor = {255, 255, 255};
constexpr cocos2d::ccColor3B kDescColor  = {166, 176, 198};
constexpr cocos2d::ccColor3B kValueColor = {255, 222, 120};
constexpr cocos2d::ccColor3B kOnColor    = {120, 255, 140};
constexpr cocos2d::ccColor3B kOffColor   = {150, 155, 170};

// Usable row width inside a card.
constexpr float cardInnerWidth(float cardWidth) { return cardWidth - 20.f; }

// Rows use anchor {0,0} and their own height.

// Toggle row with optional description.
cocos2d::CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr);

// Slider row with a visible value.
cocos2d::CCNode* makeSliderRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV,
    std::function<std::string(double)> format,
    std::function<void(double)> onChange,
    Slider** outSlider = nullptr,
    cocos2d::CCLabelBMFont** outValue = nullptr);

// Cycling selector with an optional gear callback.
cocos2d::CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    cocos2d::CCLabelBMFont** outLabel = nullptr,
    std::function<void()> onGear = nullptr);

// Action button with title and description.
cocos2d::CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress);

// Color row with a Geode picker and optional swatch output.
cocos2d::CCNode* makeColorRow(
    float width,
    char const* title, char const* desc,
    cocos2d::ccColor3B value,
    std::function<void(cocos2d::ccColor3B)> onChange,
    cocos2d::CCSprite** outSwatch = nullptr);

// Wrapped informational note.
cocos2d::CCNode* makeHint(float width, char const* text);

// Rounded card with a title and stacked rows.
cocos2d::CCNode* makeCard(
    float width,
    char const* title, cocos2d::ccColor3B accent,
    std::vector<cocos2d::CCNode*> const& rows);

// Large enable toggle with a synchronized state label.
cocos2d::CCNode* makeHeroToggle(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr,
    cocos2d::CCLabelBMFont** outStateLabel = nullptr);

// Update a hero toggle's state label.
void setHeroStateLabel(cocos2d::CCLabelBMFont* label, bool on);

// Stack cards/rows in a ScrollLayer and reset the scroll position.
geode::ScrollLayer* makeScrollStack(
    cocos2d::CCSize size,
    std::vector<cocos2d::CCNode*> const& items,
    float gap = 8.f);

// Smooth wheel scrolling: queueWheelScroll consumes wheel input, while
// stepWheelScroll eases toward the queued target each frame.
bool queueWheelScroll(geode::ScrollLayer* scrollLayer, float x, float y,
    float& targetY, bool& targetSet, float speed = 16.f);
void stepWheelScroll(geode::ScrollLayer* scrollLayer,
    float& targetY, bool& targetSet, float dt);

// Fixed-height tab bar that restyles itself and calls onSelect(index).
constexpr float kTabBarHeight = 26.f;
cocos2d::CCNode* makeTabBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect);

}
