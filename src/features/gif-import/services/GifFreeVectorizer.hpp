#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// Traces one color with the whole decoration library: per blob the best-covering
// object wins, paint tracing keeps what no object covers well. Each figure's
// `stamp` is its index into `stampVariants()`; the plan collects and reindexes
// at the end.
std::vector<Primitive> vectorizeFree(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked = {},
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true
);

} // namespace paimon::gifimport
