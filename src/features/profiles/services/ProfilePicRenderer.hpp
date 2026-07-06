#pragma once
#include <Geode/Geode.hpp>
#include <string>

struct ProfilePicConfig;

namespace paimon::profile_pic {

cocos2d::CCNode* composeProfilePicture(
    cocos2d::CCNode* imageNode,
    float targetSize,
    ProfilePicConfig const& config
);

}
