#pragma once

// one builder for panel cards and editor preview so the two never drift apart.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::officialslots::ui {

std::string officialName(int officialId);
std::string slotDisplayName(Slot const& slot);

// vanilla difficulty face with the rate glow already applied. null when the
// game's sprite frames are missing, so every caller has to check.
cocos2d::CCNode* createDifficultyBadge(Difficulty difficulty, Tier tier, float scale);

// null when the icon is unavailable rather than a bare number.
cocos2d::CCNode* createStarBadge(int stars, float scale);

cocos2d::CCNode* createCoinRow(float scale);

cocos2d::CCNode* createCardBackground(cocos2d::CCSize size);

} // namespace paimon::officialslots::ui
