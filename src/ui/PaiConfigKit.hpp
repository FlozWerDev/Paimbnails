#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include "PaimonUI.hpp"
#include <functional>
#include <string>
#include <vector>

namespace paimon::configkit {

constexpr auto    kCardColor  = paimon::ui::palette::ink;
constexpr GLubyte kCardAlpha  = 75;
constexpr GLubyte kRowAlpha   = 55;
constexpr auto    kTitleColor = paimon::ui::palette::text;
constexpr auto    kDescColor  = paimon::ui::palette::muted;
constexpr auto    kValueColor = paimon::ui::palette::gold;
constexpr auto    kOnColor    = paimon::ui::palette::success;
constexpr auto    kOffColor   = paimon::ui::palette::dim;

constexpr float cardInnerWidth(float cardWidth) { return cardWidth - 20.f; }

cocos2d::CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr);

cocos2d::CCNode* makeSliderRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV,
    std::function<std::string(double)> format,
    std::function<void(double)> onChange,
    Slider** outSlider = nullptr,
    cocos2d::CCLabelBMFont** outValue = nullptr);

cocos2d::CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    cocos2d::CCLabelBMFont** outLabel = nullptr,
    std::function<void()> onGear = nullptr);

cocos2d::CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress,
    paimon::ui::Btn skin = paimon::ui::Btn::Green);

cocos2d::CCNode* makeColorRow(
    float width,
    char const* title, char const* desc,
    cocos2d::ccColor3B value,
    std::function<void(cocos2d::ccColor3B)> onChange,
    cocos2d::CCSprite** outSwatch = nullptr);

cocos2d::CCNode* makeHint(float width, char const* text);

cocos2d::CCNode* makeCard(
    float width,
    char const* title, cocos2d::ccColor3B accent,
    std::vector<cocos2d::CCNode*> const& rows);

cocos2d::CCNode* makeHeroToggle(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr,
    cocos2d::CCLabelBMFont** outStateLabel = nullptr);

void setHeroStateLabel(cocos2d::CCLabelBMFont* label, bool on);

geode::ScrollLayer* makeScrollStack(
    cocos2d::CCSize size,
    std::vector<cocos2d::CCNode*> const& items,
    float gap = 8.f);

bool queueWheelScroll(geode::ScrollLayer* scrollLayer, float x, float y,
    float& targetY, bool& targetSet, float speed = 16.f);
void stepWheelScroll(geode::ScrollLayer* scrollLayer,
    float& targetY, bool& targetSet, float dt);

// flalertlayer::show uses the scene m_zorder, so a popup alert can end up underneath.
void showAbove(FLAlertLayer* alert, cocos2d::CCNode* owner);

constexpr float kTabBarHeight = 24.f;
cocos2d::CCNode* makeTabBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect);

}
