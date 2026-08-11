#pragma once

#include <Geode/binding/GJGameLevel.hpp>
#include <string>

namespace paimon {

// Serializes level fields for thumbnail uploads. Geometry is omitted; only its
// length is kept. Must run on the main thread and returns empty for null.
std::string collectLevelMetadata(GJGameLevel* level);

} // namespace paimon
