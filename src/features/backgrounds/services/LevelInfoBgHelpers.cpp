#include "LevelInfoBgHelpers.hpp"

#include <Geode/Geode.hpp>
#include <cmath>

using namespace geode::prelude;

namespace paimon::levelinfo {

std::string makeLevelInfoBlurCacheKey(int levelID, int thumbnailIndex, std::string const& bgStyle, int intensity, cocos2d::CCSize const& targetSize) {
    return fmt::format(
        "levelinfo:{}:{}:{}:{}:{}x{}",
        levelID,
        thumbnailIndex,
        bgStyle,
        intensity,
        static_cast<int>(std::round(targetSize.width)),
        static_cast<int>(std::round(targetSize.height))
    );
}

}
