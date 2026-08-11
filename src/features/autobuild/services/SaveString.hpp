#pragma once

// Helpers around GD object save strings ("1,2895,2,45,3,2775,...").
//
// Templates keep the original save string of every captured object instead of a
// handful of properties, so a generated copy carries colors, HSV, groups, links
// and every key the mod does not model. Building only rewrites the keys it has
// to: position, and the id shifts the user asked for.

#include <set>
#include <string>

namespace paimon::autobuild {

struct IdShift {
    int colors = 0;
    int groups = 0;
    int layers = 0;
    int zOrder = 0;
    int addGroup = 0;  // group added to every placed object, 0 = none
};

// Same object, moved to (x, y) and with the requested id shifts applied.
// The result ends with ';' so several of them concatenate into a level string.
std::string retarget(std::string const& save, float x, float y, IdShift const& shift);

// The save string without its position keys: two objects that share it are the
// same shape, which is what piece deduplication compares.
std::string shapeKey(std::string const& save);

int objectIdOf(std::string const& save);
bool positionOf(std::string const& save, float& x, float& y);

// Color channels the object references, so a build only imports the palette it
// actually uses instead of overwriting the whole level.
void collectColorIds(std::string const& save, std::set<int>& out);

} // namespace paimon::autobuild
