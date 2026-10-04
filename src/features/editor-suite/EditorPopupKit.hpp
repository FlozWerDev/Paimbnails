#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/NineSlice.hpp>

#include <functional>
#include <string>

// Building blocks shared by the editor tool popups (physics lab, gif import).
namespace paimon::editor::kit {

inline constexpr float kValueBoxWidth = 54.f;

namespace tint {
inline constexpr cocos2d::ccColor3B white{255, 255, 255};
inline constexpr cocos2d::ccColor3B gold{255, 210, 90};
inline constexpr cocos2d::ccColor3B label{235, 235, 245};
inline constexpr cocos2d::ccColor3B muted{165, 180, 210};
inline constexpr cocos2d::ccColor3B off{170, 170, 180};
inline constexpr cocos2d::ccColor3B cyan{120, 235, 255};
inline constexpr cocos2d::ccColor3B orange{255, 190, 95};
inline constexpr cocos2d::ccColor3B red{255, 140, 90};
inline constexpr cocos2d::ccColor3B green{140, 255, 150};
inline constexpr cocos2d::ccColor3B yellow{255, 200, 90};
inline constexpr cocos2d::ccColor3B sky{150, 220, 255};
inline constexpr cocos2d::ccColor3B cream{255, 230, 120};
inline constexpr cocos2d::ccColor3B violet{205, 160, 255};
inline constexpr cocos2d::ccColor3B pink{255, 150, 220};
inline constexpr cocos2d::ccColor3B silver{200, 200, 215};
} // namespace tint

// Mod frame first, then the gd fallback frame, then an empty sprite: never null.
cocos2d::CCSprite* glyph(char const* paim, char const* fallback, float size,
    cocos2d::ccColor3B color = tint::white);

CCMenuItemSpriteExtra* toolButton(char const* paim, char const* fallback,
    geode::EditorBaseColor color, float size, std::function<void()> onPress);
// The callback receives the state the toggler is switching to.
CCMenuItemToggler* toolToggle(char const* paim, char const* fallback,
    geode::EditorBaseColor onColor, float size, bool value, std::function<void(bool)> onChange);

CCMenuItemSpriteExtra* arrowButton(bool right, float height, std::function<void()> onPress);

struct Pill {
    CCMenuItemSpriteExtra* button = nullptr;
    cocos2d::CCNode* body = nullptr;
    cocos2d::CCNode* background = nullptr;
    cocos2d::CCSprite* icon = nullptr;
    cocos2d::CCLabelBMFont* label = nullptr;
    cocos2d::ccColor3B iconColor = tint::white;
    float labelScale = 0.5f;
    bool goldFont = true;
};

// GJ_button skinned capsule with an optional glyph; a null texture draws no plate.
Pill pill(char const* paim, char const* fallback, char const* text, char const* texture,
    cocos2d::CCSize size, std::function<void()> onPress, char const* font = "goldFont.fnt",
    cocos2d::ccColor3B iconColor = tint::white);
void setPillText(Pill& pill, std::string const& text);
void setPillIcon(Pill& pill, char const* paim, char const* fallback);
void setPillSkin(Pill& pill, char const* texture);

geode::NineSlice* inset(cocos2d::CCRect const& area, GLubyte opacity);
cocos2d::CCNode* header(char const* paim, char const* fallback, char const* text, float width);

struct StepperSpec {
    char const* icon;
    char const* fallback;
    char const* title;
    cocos2d::ccColor3B color;
};
// One option row filling `row`: glyph, title, [-] value [+]. Returns the value label.
cocos2d::CCLabelBMFont* stepper(cocos2d::CCNode* parent, cocos2d::CCMenu* menu,
    StepperSpec const& spec, cocos2d::CCRect const& row, bool stripe,
    std::function<void(int)> onStep);
void setValue(cocos2d::CCLabelBMFont* value, std::string const& text);

cocos2d::CCNode* checker(cocos2d::CCSize size, float cell, cocos2d::ccColor3B dark,
    cocos2d::ccColor3B light);

cocos2d::CCLabelBMFont* label(char const* text, char const* font, float scale,
    cocos2d::ccColor3B color = tint::white);
// Single line while it stays at minSingle or above, otherwise word-wrapped into the box.
void setWrapped(cocos2d::CCLabelBMFont* label, std::string const& text, cocos2d::CCSize box,
    float maxScale, float minSingle);

} // namespace paimon::editor::kit
