#pragma once

// shared "#12345" label for the visible ids hooks; badges register here so one
// keyboard hook flips them all on shift changes instead of polling every frame.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::info {

// colour/opacity chosen in the settings, applied to every badge.
cocos2d::ccColor3B idBadgeColor();
GLubyte idBadgeOpacity();

// false while shift-to-reveal is on and shift is not held.
bool idBadgesVisible();

// creates the label already styled and registered. caller positions it and adds
// it to the cell. returns nullptr when the id is not worth showing.
cocos2d::CCLabelBMFont* makeIdBadge(std::string const& text, float scale = 0.4f);

// attach-or-nothing: creates the badge under `parent` tagged `nodeID`, unless
// one is already there. returns the new badge, or nullptr.
cocos2d::CCLabelBMFont* attachIdBadge(cocos2d::CCNode* parent, char const* nodeID,
                                      std::string const& text, float scale,
                                      cocos2d::CCPoint anchor, cocos2d::CCPoint position);

// makes each glyph pixel invert the content already drawn behind it.
void applyAdaptiveIdBadgeContrast(cocos2d::CCLabelBMFont* label);

// registered by the keyboard hook; visible for the hook only.
void setShiftHeld(bool held);

} // namespace paimon::info
