#pragma once

#include <Geode/Geode.hpp>

namespace paimon::translate::ui {

// named user object present on a CommentCell while the translator repaints its
// text, so loadFromComment hooks can tell a language swap from a fresh cell.
char const* textSwapMarker();

// fades and lifts freshly swapped text into place. TextAreas animate line by
// line. Does nothing when Smooth UI motion is off.
void animateTextSwap(cocos2d::CCNode* textNode);

}
