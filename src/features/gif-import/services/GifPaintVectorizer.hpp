#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

constexpr int kPaintSublayers = 3;

std::vector<int> paintOrder(
    std::vector<GridFrame> const& frames,
    int colors,
    int width,
    int height
);

std::vector<Primitive> vectorizePaint(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank
);

} // namespace paimon::gifimport
