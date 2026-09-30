#pragma once
#include <Geode/Geode.hpp>
#include <string>

// stencil node for a shape, centered in the given contentsize.
// geometric shapes or scale9 sprites (*.png).
cocos2d::CCNode* createShapeStencil(std::string const& shapeName, float size);

// outline-only variant, for frames matching the stencil shape.
cocos2d::CCNode* createShapeBorder(std::string const& shapeName, float size, float thickness, cocos2d::ccColor3B color, GLubyte opacity = 255);

std::vector<std::pair<std::string, std::string>> getGeometricShapes();
