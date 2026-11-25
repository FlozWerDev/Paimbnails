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

// topmost palette index per sample on the plan grid; -1 where nothing paints.
// only meaningful for opaque paint geometry (Paint/Render/Free).
std::vector<std::int32_t> renderPlanFrameIndices(
    ImportPlan const& plan,
    int frame,
    int scale
);

} // namespace paimon::gifimport
