#pragma once

#include "../GifImportTypes.hpp"

#include <span>

namespace paimon::gifimport {

struct SurfaceImage {
    int width;
    int height;
    std::span<Color const> palette;
    std::span<int const> ranks;
    std::span<std::vector<std::uint8_t> const> coveredLater;
    std::span<std::uint8_t const> rgba;
};

// replace detailed grid repairs only when a cheaper surface fits the source.
void optimizePaintSurfaces(std::vector<Primitive>& objects,
    std::vector<std::vector<int>> const& positions, SurfaceImage const& image);

} // namespace paimon::gifimport
