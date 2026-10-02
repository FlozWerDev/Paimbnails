#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <functional>
#include <string>

namespace paimon::ui {

// Tuned for GD backgrounds (GJ_square01 brown, black square02 insets).
namespace palette {
inline constexpr cocos2d::ccColor3B text{255, 255, 255};
inline constexpr cocos2d::ccColor3B muted{205, 205, 215};
inline constexpr cocos2d::ccColor3B dim{150, 150, 160};
inline constexpr cocos2d::ccColor3B gold{255, 210, 90};
inline constexpr cocos2d::ccColor3B accent{255, 210, 90};
inline constexpr cocos2d::ccColor3B info{110, 220, 255};
inline constexpr cocos2d::ccColor3B violet{205, 160, 255};
inline constexpr cocos2d::ccColor3B success{130, 255, 130};
inline constexpr cocos2d::ccColor3B warning{255, 175, 75};
inline constexpr cocos2d::ccColor3B danger{255, 110, 110};
inline constexpr cocos2d::ccColor3B ink{0, 0, 0};
}

// GJ_button_01..06
enum class Btn { Green = 1, Cyan, Pink, Gray, Blue, Red };
char const* buttonTexture(Btn skin);

// GJ_square01..07
enum class Bg { Brown = 1, Blue, Green, Purple, Dark, Light, White };
char const* popupTexture(Bg bg);

bool motionEnabled();
float motionDuration(float seconds);

// Rounded GD panel (square02b tinted), anchored bottom-left.
geode::NineSlice* makeInset(cocos2d::CCSize size, GLubyte opacity = 90,
    cocos2d::ccColor3B color = palette::ink);

// width is the final on-screen width (0 = fit the text); scale is the whole sprite scale.
ButtonSprite* makeButtonSprite(char const* text, Btn skin = Btn::Green,
    float width = 0.f, float scale = 0.7f, char const* font = "goldFont.fnt");
CCMenuItemSpriteExtra* makeButton(char const* text, std::function<void()> onPress,
    Btn skin = Btn::Green, float width = 0.f, float scale = 0.7f,
    char const* font = "goldFont.fnt");
// Same as above with a raw GJ_button_0X.png texture name.
ButtonSprite* makeButtonSprite(char const* text, char const* texture,
    float width = 0.f, float scale = 0.7f, char const* font = "goldFont.fnt");
CCMenuItemSpriteExtra* makeButton(char const* text, std::function<void()> onPress,
    char const* texture, float width = 0.f, float scale = 0.7f,
    char const* font = "goldFont.fnt");
void setButtonSkin(CCMenuItemSpriteExtra* button, Btn skin);

geode::CircleButtonSprite* makeCircleSprite(char const* frame,
    geode::CircleBaseColor color = geode::CircleBaseColor::Green,
    geode::CircleBaseSize size = geode::CircleBaseSize::Small, float topScale = 1.f);
CCMenuItemSpriteExtra* makeCircleButton(char const* frame, std::function<void()> onPress,
    geode::CircleBaseColor color = geode::CircleBaseColor::Green,
    geode::CircleBaseSize size = geode::CircleBaseSize::Small, float topScale = 1.f);
CCMenuItemSpriteExtra* makeFrameButton(char const* frame, float scale,
    std::function<void()> onPress);

CCMenuItemToggler* makeSwitch(cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler callback, bool value, float scale = 0.7f);
// onChange receives the new state.
CCMenuItemToggler* makeToggle(bool value, std::function<void(bool)> onChange,
    float scale = 0.7f);

CCMenuItemSpriteExtra* makeInfoButton(std::string const& title, std::string const& body,
    float scale = 0.6f);

cocos2d::CCLabelBMFont* makeTitle(char const* text, float maxWidth, float scale = 0.6f);
cocos2d::CCLabelBMFont* makeLabel(char const* text, float maxWidth, float scale = 0.4f,
    cocos2d::ccColor3B color = palette::text);
cocos2d::CCLabelBMFont* makeText(char const* text, float wrapWidth, float scale = 0.5f,
    cocos2d::ccColor3B color = palette::muted,
    cocos2d::CCTextAlignment align = cocos2d::kCCTextAlignmentLeft);
cocos2d::CCSprite* makeDivider(float width, cocos2d::ccColor3B color = palette::gold,
    GLubyte opacity = 150);

// Inset with an optional gold heading; children go between the heading and the bottom padding.
cocos2d::CCNode* makePanel(cocos2d::CCSize size, char const* title = nullptr,
    GLubyte opacity = 90);
constexpr float kPanelHeader = 22.f;

void addCorners(cocos2d::CCNode* to, cocos2d::CCSize size,
    geode::SideArtStyle style = geode::SideArtStyle::PopupGold, float scale = 0.45f,
    bool top = false);

void animateIn(cocos2d::CCNode* node, float delay = 0.f, float distance = 6.f);

// GD gradient background plus side art; returns the gradient sprite.
cocos2d::CCSprite* decorateScene(cocos2d::CCNode* parent,
    cocos2d::ccColor3B tint = {0, 102, 255}, bool sideArt = true);
cocos2d::CCLabelBMFont* addSceneTitle(cocos2d::CCNode* parent, char const* text,
    float maxWidth = 260.f);
CCMenuItemSpriteExtra* makeBackButton(std::function<void()> onPress);

}
