#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

std::vector<Primitive> packBlocks(
    std::vector<int> const& positions,
    int width,
    int height,
    int color
);

std::vector<Primitive> vectorizeArt(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked = {}
);

std::vector<std::uint8_t> renderPlanFrame(
    ImportPlan const& plan,
    int frame,
    int scale
);

} // namespace paimon::gifimport
