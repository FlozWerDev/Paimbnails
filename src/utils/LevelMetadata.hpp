#pragma once

#include <Geode/binding/GJGameLevel.hpp>
#include <string>

namespace paimon {

// level fields for uploads; geometry omitted (length only). main thread; empty for null.
std::string collectLevelMetadata(GJGameLevel* level);

} // namespace paimon
