#pragma once
#include <Geode/Geode.hpp>
#include "../../../ui/PaimonUI.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>

#include <functional>
#include <string>
#include <vector>

namespace geode { class ScrollLayer; }

namespace paimon::icon_maker::gdkit {

constexpr cocos2d::ccColor3B kTitleColor = paimon::ui::palette::text;
constexpr cocos2d::ccColor3B kDescColor  = paimon::ui::palette::muted;
constexpr cocos2d::ccColor3B kValueColor = paimon::ui::palette::gold;
constexpr cocos2d::ccColor3B kGoldColor  = paimon::ui::palette::gold;
constexpr cocos2d::ccColor3B kPlateColor = paimon::ui::palette::ink;
constexpr GLubyte            kPlateAlpha = 80;

// usable row width inside a card.
constexpr float cardInnerWidth(float cardWidth) { return cardWidth - 20.f; }

// large gd window frame (gj_square01).
cocos2d::CCNode* makeWindow(cocos2d::CCSize size);

// row plate (gj_square05).
cocos2d::CCNode* makePlate(float width, float height,
                           cocos2d::ccColor3B color = kPlateColor,
                           GLubyte opacity = kPlateAlpha);

// scaled action button using buttonsprite + goldfont.
CCMenuItemSpriteExtra* makeButton(char const* text, char const* sprite,
                                  float scale, std::function<void()> onPress);

// scaled list-style tab face.
cocos2d::CCSprite* makeTabFace(char const* text, bool selected,
                               float maxW, float maxH);

cocos2d::CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr);

// title/value row with a full-width slider below.
cocos2d::CCNode* makeSliderRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV,
    std::function<std::string(double)> format,
    std::function<void(double)> onChange,
    Slider** outSlider = nullptr,
    cocos2d::CCLabelBMFont** outValue = nullptr);

// like makesliderrow but with a writable box and step arrows, for exact values.
cocos2d::CCNode* makeNumberRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV, double step, int decimals,
    std::function<void(double)> onChange);

cocos2d::CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    cocos2d::CCLabelBMFont** outLabel = nullptr);

cocos2d::CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress);

// standalone informational text.
cocos2d::CCNode* makeHint(float width, char const* text);

// card with a gold title, accent dot, separator, and rows.
cocos2d::CCNode* makeCard(
    float width,
    char const* title, cocos2d::ccColor3B accent,
    std::vector<cocos2d::CCNode*> const& rows);

// stack rows/cards vertically inside a scrolllayer.
geode::ScrollLayer* makeScrollStack(
    cocos2d::CCSize size,
    std::vector<cocos2d::CCNode*> const& items,
    float gap = 8.f);

// gd-style tabs with selected/unselected states.
constexpr float kTabBarHeight = paimon::configkit::kTabBarHeight;
cocos2d::CCNode* makeTabBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect);

// smooth wheel scrolling shared by the other popups.
bool queueWheelScroll(geode::ScrollLayer* scrollLayer, float x, float y,
                      float& targetY, bool& targetSet, float speed = 16.f);
void stepWheelScroll(geode::ScrollLayer* scrollLayer,
                     float& targetY, bool& targetSet, float dt);

}
