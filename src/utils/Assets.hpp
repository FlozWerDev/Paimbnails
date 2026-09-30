#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>
#include <string>

namespace Assets {

// button from assets/buttons/<key>.txt if it exists, otherwise fallback.
cocos2d::CCSprite* loadButtonSprite(
    std::string const& key,
    std::string const& defaultContent,
    geode::CopyableFunction<cocos2d::CCSprite*()> fallback
);

} // namespace assets

