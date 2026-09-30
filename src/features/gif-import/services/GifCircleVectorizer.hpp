#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// one circle sprite keeps all traced ellipses in the same z layer.
std::vector<Primitive> vectorizeCircles(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked = {},
    std::vector<std::uint8_t> const& empty = {}
);

} // namespace paimon::gifimport
