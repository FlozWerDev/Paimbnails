#pragma once
#include <Geode/Geode.hpp>

namespace paimon::icon_gradients {
// The draw hook binds the image per sprite, including during preview fades.
void setGradientImage(cocos2d::CCSprite* sprite, cocos2d::CCTexture2D* image);
}
