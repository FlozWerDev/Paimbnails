#pragma once

// applies a user's global icon without including the more icons api.

namespace cocos2d { class CCNode; }

namespace paimon::globalicon {

// replaces the simpleplayer under searchroot with the user's global "cube"
// icon if available (and more icons + the setting are on). no-op otherwise.
void renderProfileCube(cocos2d::CCNode* searchRoot, int accountID);

} // namespace paimon::globalicon
