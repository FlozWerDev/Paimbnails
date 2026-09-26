#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// Traces one color with ellipses only: biggest fitting where the blob is
// fattest, stretched where the blob gives, until no cell stays unpainted. Every
// object comes from the same GD circle (one sprite sheet), so Z order between
// them holds and none must hide under a square.
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
