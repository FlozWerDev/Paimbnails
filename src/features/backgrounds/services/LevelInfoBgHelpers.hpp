#pragma once

// Z-order constants and blur-cache helper for the LevelInfoLayer background.

#include <cocos2d.h>
#include <string>

namespace paimon::levelinfo {

// Z-order constants for LevelInfoLayer background layering
inline constexpr int kBackgroundZOrder = -4;
inline constexpr int kExtraDarknessZOrder = -3; // extra darkness, separate from the bg
inline constexpr int kEffectsZOrder   = -2;
inline constexpr int kOverlayZOrder   = -1;

std::string makeLevelInfoBlurCacheKey(int levelID, int thumbnailIndex, std::string const& bgStyle, int intensity, cocos2d::CCSize const& targetSize);

} // namespace paimon::levelinfo
