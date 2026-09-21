#pragma once

// Applies a user's global icon without including the More Icons API.

namespace cocos2d { class CCNode; }

namespace paimon::globalicon {

// Replaces the SimplePlayer under searchRoot with the user's global "cube"
// icon if available (and More Icons + the setting are on). No-op otherwise.
void renderProfileCube(cocos2d::CCNode* searchRoot, int accountID);

} // namespace paimon::globalicon
