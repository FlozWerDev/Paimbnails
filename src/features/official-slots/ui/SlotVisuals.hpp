#pragma once

// one builder for panel cards and editor preview so the two never drift apart.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::officialslots::ui {

std::string officialName(int officialId);
std::string slotDisplayName(Slot const& slot);

// face-only vanilla difficulty icon with its rate glow. null when the
// game's sprite frames are missing, so every caller has to check.
cocos2d::CCNode* createDifficultyBadge(Difficulty difficulty, Tier tier, float scale);

// null when the icon is unavailable rather than a bare number.
cocos2d::CCNode* createStarBadge(int stars, float scale);

// preserve the native star icon, typography and right-aligned number on a main-level page.
void updatePageStars(cocos2d::CCLabelBMFont* label, cocos2d::CCSprite* icon, int stars);

// paint decorative progress using the native left inset and text-to-icon spacing.
void updatePageOrbs(cocos2d::CCLabelBMFont* label, cocos2d::CCSprite* icon, int normalPercent);

cocos2d::CCNode* createCoinRow(float scale);

cocos2d::CCNode* createCardBackground(cocos2d::CCSize size);

} // namespace paimon::officialslots::ui
