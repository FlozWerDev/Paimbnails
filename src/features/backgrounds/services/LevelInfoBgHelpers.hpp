#pragma once

#include <cocos2d.h>
#include <string>

namespace paimon::levelinfo {

inline constexpr int kBackgroundZOrder = -4;
inline constexpr int kExtraDarknessZOrder = -3;
inline constexpr int kEffectsZOrder   = -2;
inline constexpr int kOverlayZOrder   = -1;

std::string makeLevelInfoBlurCacheKey(int levelID, int thumbnailIndex, std::string const& bgStyle, int intensity, cocos2d::CCSize const& targetSize);

}
