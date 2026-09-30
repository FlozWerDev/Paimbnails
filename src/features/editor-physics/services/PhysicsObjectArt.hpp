#pragma once

#include "PhysicsWorkspace.hpp"

namespace paimon::editorphysics {

// stand-in for one captured object in the preview: a real gameobject when the
// game can build one, else a copy of the art it is drawing right now.
cocos2d::CCNode* buildObjectArt(BodyVisual const& visual);

} // namespace paimon::editorphysics
