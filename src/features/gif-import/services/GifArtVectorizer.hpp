#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// `spare` puede pisarse: otra capa lo tapa y permite fusionar en rectangulos.
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
