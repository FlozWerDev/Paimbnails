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

void prunePaintObjects(
    std::vector<Primitive>& objects,
    int width,
    int height,
    bool gridExact = true
);

// Exact merges only (blocks and rectangles whose union paints zero new pixels):
// what `repairPaintSeams` sews after the last prune.
void mergePaintSolids(std::vector<Primitive>& objects, bool gridExact = true);

void prunePaintObjectsByVisibility(
    std::vector<Primitive>& staticObjects,
    std::vector<VisibilityTrack>& tracks,
    int frameCount,
    int width,
    int height
);

std::vector<Primitive> paintSeamRepairs(
    std::vector<Primitive> const& objects,
    std::vector<std::int32_t> const& cells,
    std::vector<int> const& ranks,
    int width,
    int height,
    bool gridExact = true
);

std::vector<Primitive> vectorizePaint(
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
