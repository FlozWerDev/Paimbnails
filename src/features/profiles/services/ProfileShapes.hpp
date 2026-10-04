#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// one outline feeds both stencil and border so they can never drift apart when scaled.
namespace paimon::profile_shapes {

// closed outline sampled in a unit box [0,1]x[0,1], y-up, centered on 0.5.
std::vector<cocos2d::CCPoint> unitOutline(std::string const& shapeName);

bool isKnown(std::string const& shapeName);

// legacy stencilSprite ids map to the closest modern shape so saved configs
// keep rendering. unknown ids fall back to "circle".
std::string canonicalId(std::string const& shapeName);

// fill node sized to `size`, origin at (0,0). alpha-threshold clipping gives
// a crisp edge against the clipper.
cocos2d::CCNode* createFill(std::string const& shapeName, float size);

// outline ring tracing the same silhouette, centered in a `size` box.
cocos2d::CCNode* createOutline(std::string const& shapeName, float size, float thickness, cocos2d::ccColor3B color, GLubyte opacity);

// picker entries: id + short label, ordered for the grid.
std::vector<std::pair<std::string, std::string>> pickerShapes();

}
