#pragma once

// visual primitives shared by the two info popups (level stats and level info),
// so both read as one system: same panels, same fonts, same colors.

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::info::blocks {

constexpr cocos2d::ccColor3B kLabel{175, 175, 175};
constexpr cocos2d::ccColor3B kValue{255, 255, 255};
constexpr cocos2d::ccColor3B kAccent{255, 222, 120};

cocos2d::CCNode* makeBlock(float width, float height, GLubyte opacity = 75);

cocos2d::CCLabelBMFont* addText(cocos2d::CCNode* parent, char const* text, char const* font,
                                float scale, cocos2d::ccColor3B color, cocos2d::CCPoint pos,
                                cocos2d::CCPoint anchor, float maxWidth = 0.f);

// first frame that actually exists in the loaded sheets. frame names have moved
// between gd versions, so every icon lists its alternatives.
cocos2d::CCSprite* firstFrame(std::vector<char const*> const& candidates);

void addBar(cocos2d::CCNode* parent, float x, float y, float width, float height,
            float fill, cocos2d::ccColor3B color);

// blurred thumbnail as popup backdrop, dimmed for panel contrast. the caller
// places it and picks the z.
cocos2d::CCNode* makeBackdrop(cocos2d::CCSprite* blurred, cocos2d::CCSize const& area,
                              GLubyte darkness, bool fadeIn);

} // namespace paimon::info::blocks
