#pragma once

// One builder for panel cards and editor preview so the two never drift apart.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

namespace paimon::officialslots::ui {

// Vanilla difficulty face with the rate glow already applied. Null when the
// game's sprite frames are missing, so every caller has to check.
cocos2d::CCNode* createDifficultyBadge(Difficulty difficulty, Tier tier, float scale);

// Null when the icon is unavailable rather than a bare number.
cocos2d::CCNode* createStarBadge(int stars, float scale);

cocos2d::CCNode* createCoinRow(float scale);

cocos2d::CCNode* createCardBackground(cocos2d::CCSize size);

} // namespace paimon::officialslots::ui
