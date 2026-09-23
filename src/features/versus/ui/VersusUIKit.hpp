#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <string>

namespace paimon::versus::ui {

inline constexpr cocos2d::ccColor3B kAccent = {255, 226, 140};
inline constexpr cocos2d::ccColor3B kMuted  = {172, 180, 200};
inline constexpr cocos2d::ccColor3B kGood   = {140, 230, 160};
inline constexpr cocos2d::ccColor3B kBad    = {240, 130, 140};

// Height of the caption strip inside a panel, so callers can lay out under it.
inline constexpr float kCaptionH = 22.f;

cocos2d::CCNode* makePanel(cocos2d::CCSize size, std::string const& caption);

// The rectangle under the caption strip, in the panel's own coordinates.
cocos2d::CCRect panelBody(cocos2d::CCSize size);

cocos2d::CCLabelBMFont* makeText(std::string const& text, char const* font, float scale,
                                 cocos2d::CCPoint const& pos);

// A tab or toggle. Colour it afterwards with styleTab; nothing here tracks
// which one is selected.
CCMenuItemSpriteExtra* makeTab(std::string const& label, float width, cocos2d::CCObject* target,
                               cocos2d::SEL_MenuHandler callback);
void styleTab(CCMenuItemSpriteExtra* tab, bool active);

CCMenuItemSpriteExtra* makeAction(std::string const& label, float width, char const* skin,
                                  float scale, cocos2d::CCObject* target,
                                  cocos2d::SEL_MenuHandler callback);

// An icon with its name beside it: the four hub shortcuts and nothing else.
CCMenuItemSpriteExtra* makeIconRow(char const* frameName, std::string const& label, float width,
                                   cocos2d::CCObject* target, cocos2d::SEL_MenuHandler callback);

// Thin rule used to separate rows inside a panel.
cocos2d::CCNode* makeDivider(float width);

} // namespace paimon::versus::ui
