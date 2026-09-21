#pragma once

// Shared "#12345" label for the Visible IDs hooks; badges register here so one
// keyboard hook flips them all on Shift changes instead of polling every frame.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::info {

// Colour/opacity chosen in the settings, applied to every badge.
cocos2d::ccColor3B idBadgeColor();
GLubyte idBadgeOpacity();

// False while Shift-to-reveal is on and Shift is not held.
bool idBadgesVisible();

// Creates the label already styled and registered. Caller positions it and adds
// it to the cell. Returns nullptr when the id is not worth showing.
cocos2d::CCLabelBMFont* makeIdBadge(std::string const& text, float scale = 0.4f);

// Makes each glyph pixel invert the content already drawn behind it.
void applyAdaptiveIdBadgeContrast(cocos2d::CCLabelBMFont* label);

// Registered by the keyboard hook; visible for the hook only.
void setShiftHeld(bool held);

} // namespace paimon::info
