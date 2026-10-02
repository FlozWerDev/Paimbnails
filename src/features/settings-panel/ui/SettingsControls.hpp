#pragma once

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <string>
#include <vector>

namespace paimon::settings_ui {

constexpr float ROW_HEIGHT = 36.f;
constexpr float HEADER_HEIGHT = 28.f;
constexpr float LABEL_X = 10.f;

cocos2d::CCNode* createToggleRow(
    const char* label,
    bool initialValue,
    std::function<void(bool)> onChange,
    float width
);

cocos2d::CCNode* createSliderRow(
    const char* label,
    float initialValue,
    float minVal,
    float maxVal,
    std::function<void(float)> onChange,
    float width
);

cocos2d::CCNode* createIntSliderRow(
    const char* label,
    int initialValue,
    int minVal,
    int maxVal,
    std::function<void(int)> onChange,
    float width
);

cocos2d::CCNode* createDropdownRow(
    const char* label,
    std::string const& initialValue,
    std::vector<std::string> const& options,
    std::function<void(std::string const&)> onChange,
    float width
);

cocos2d::CCNode* createButtonRow(
    const char* label,
    const char* buttonText,
    std::function<void()> onPress,
    float width
);

cocos2d::CCNode* createLinkRow(
    const char* label,
    std::function<void()> onOpen,
    float width
);

cocos2d::CCNode* createTextInputRow(
    const char* label,
    std::string const& initialValue,
    const char* placeholder,
    int maxChars,
    std::function<void(std::string const&)> onChange,
    float width
);

cocos2d::CCNode* createHintRow(
    const char* text,
    float width
);

cocos2d::CCNode* createSectionHeader(const char* title, float width);

// collapsible header: toggles contentcontainer on tap.
cocos2d::CCNode* createCollapsibleHeader(
    const char* title,
    float width,
    cocos2d::CCNode* contentContainer,
    bool initiallyExpanded,
    std::function<void()> onToggle = nullptr
);

} // namespace paimon::settings_ui
