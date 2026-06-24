#pragma once
//
// Synchronous HTTP fetch helpers for use from CPU-bound background threads.
// Wraps Geode 5's WebRequest getSync()/postSync() with Result ergonomics
// and a configurable timeout.
//

#include <Geode/Geode.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// Synchronous GET. Blocks the calling thread until the response arrives.
// Returns Ok(body bytes) on success, Err with a human-readable message
// on failure.
geode::Result<std::vector<std::uint8_t>> syncFetchBytes(
    std::string url,
    std::chrono::seconds timeout = std::chrono::seconds(30));

// Synchronous HEAD-equivalent: does a GET and returns true if the
// response is OK. We use GET (instead of HEAD) because PackGen's
// hosting redirects HEAD requests for missing files to a 200 SPA fallback,
// which would falsely report assets as existing.
geode::Result<bool> syncCheckExists(
    std::string url,
    std::chrono::seconds timeout = std::chrono::seconds(10));

}  // namespace paimon::texture_studio
