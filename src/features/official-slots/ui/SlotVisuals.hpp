#pragma once

// Shared drawing helpers for the slot UI.
//
// Both the panel cards and the editor preview have to show the same thing: a
// vanilla difficulty face with its rate glow, a star count, and optionally the
// three coins. Keeping the builders here is what stops the two screens from
// drifting apart, which is the usual way a preview stops matching the result.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

namespace paimon::officialslots::ui {

// Vanilla difficulty face with the rate glow already applied. Null when the
// game's sprite frames are missing, so every caller has to check.
cocos2d::CCNode* createDifficultyBadge(Difficulty difficulty, Tier tier, float scale);

// Star (or moon) count drawn with the game's own icon, laid out as "12 *".
// Returns null when the icon is unavailable rather than drawing a bare number.
cocos2d::CCNode* createStarBadge(int stars, bool platformer, float scale);

// The three silver coins shown on official pages.
cocos2d::CCNode* createCoinRow(float scale);

// Dark rounded panel used behind cards and sections.
cocos2d::CCNode* createCardBackground(cocos2d::CCSize size, bool highlighted);

} // namespace paimon::officialslots::ui
