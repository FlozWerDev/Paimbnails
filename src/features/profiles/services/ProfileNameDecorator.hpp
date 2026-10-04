#pragma once
#include <Geode/Geode.hpp>
#include "ProfilePicCustomizer.hpp"

namespace paimon::profile_name {

cocos2d::CCLabelBMFont* applyStatic(cocos2d::CCLabelBMFont* label, PicNameConfig const& cfg);

// only attaches a per-frame animator when the config actually animates.
void decorate(cocos2d::CCLabelBMFont* label, PicNameConfig const& cfg);

}
