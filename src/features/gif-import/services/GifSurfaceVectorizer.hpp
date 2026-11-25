#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

enum class SurfaceFit { Adaptive, MaximalRuns, Continuous };

// fits continuous filled bands, including areas hidden by later colors.
std::vector<Primitive> vectorizePaintSurface(
    std::vector<int> const& positions, int width, int height, int color, int layer,
    std::vector<std::uint8_t> const& coveredLater, float maximumDeviation = 0.48f,
    SurfaceFit fitting = SurfaceFit::Adaptive);

// replace isolated round regions without refitting unrelated parts of a color.
std::vector<Primitive> roundPaintRegions(std::vector<Primitive> const& existing,
    std::vector<int> const& positions, int width, int height, int color, int layer,
    std::vector<std::uint8_t> const& coveredLater);

} // namespace paimon::gifimport
