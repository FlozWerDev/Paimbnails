#pragma once

#include <Geode/cocos/base_nodes/CCNode.h>

namespace paidraw {

// Creates the PaiDraw icon from GD assets (GJ_paintBtn_001.png +
// GJ_starsIcon_001.png). Returns a targetSize x targetSize CCNode.
cocos2d::CCNode* createPaiDrawIcon(float targetSize = 32.f);

} // namespace paidraw
