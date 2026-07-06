#pragma once

#include <Geode/Geode.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// Blocks the calling thread until the response arrives.
geode::Result<std::vector<std::uint8_t>> syncFetchBytes(
    std::string url,
    std::chrono::seconds timeout = std::chrono::seconds(30));

// Uses GET (not HEAD) because PackGen's hosting redirects HEAD for missing
// files to a 200 SPA fallback, which would falsely report assets as existing.
geode::Result<bool> syncCheckExists(
    std::string url,
    std::chrono::seconds timeout = std::chrono::seconds(10));

}  // namespace paimon::texture_studio
