#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// `spare` may be stepped on: another layer covers it and allows rectangle merges.
std::vector<Primitive> packBlocks(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& spare = {}
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

std::vector<std::uint8_t> renderPlanFrame(
    ImportPlan const& plan,
    int frame,
    int scale,
    bool antialias
);

} // namespace paimon::gifimport
