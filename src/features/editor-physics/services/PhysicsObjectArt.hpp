#pragma once

#include "PhysicsWorkspace.hpp"

namespace paimon::editorphysics {

// Stand-in for one captured object in the preview: a real GameObject when the
// game can build one, else a copy of the art it is drawing right now.
cocos2d::CCNode* buildObjectArt(BodyVisual const& visual);

} // namespace paimon::editorphysics
