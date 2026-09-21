#pragma once
#include <Geode/Geode.hpp>
#include <string>

// Create a stencil node with the given shape, centered with the given contentSize.
// Geometric shapes or Scale9 sprites (any name ending in .png).
cocos2d::CCNode* createShapeStencil(std::string const& shapeName, float size);

// Create a node with the OUTLINE of the given shape (not filled).
// Useful for drawing frames that follow the same shape as the stencil.
cocos2d::CCNode* createShapeBorder(std::string const& shapeName, float size, float thickness, cocos2d::ccColor3B color, GLubyte opacity = 255);

// List of available geometric shapes (does not include Scale9 sprites)
std::vector<std::pair<std::string, std::string>> getGeometricShapes();
