#pragma once

#include "GifShapeRaster.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace paimon::gifimport {

// per cell: coverage count in bits 0..4, opaque count in 5..9,
// and the nine transparency-protection samples in 10..18.
// false leaves the caller free to run the CPU implementation.
#if defined(_WIN32) && defined(PAIM_GIF_COMPUTE)
bool computeSurfaceCoverage(std::span<ShapeXform const> shapes, int width, int height,
    bool protectTransparency, std::vector<std::uint32_t>& coverage);
bool computeQuantizedFrame(SourceFrame const& frame, std::span<std::uint8_t const> removed,
    std::span<std::int16_t const> lookup, int sourceWidth, int sourceHeight,
    int width, int height, int stride, int alphaThreshold, int colors,
    std::vector<std::int32_t>& cells);
void shutdownComputeDevice();
#else
inline bool computeSurfaceCoverage(std::span<ShapeXform const>, int, int,
    bool, std::vector<std::uint32_t>&) { return false; }
inline bool computeQuantizedFrame(SourceFrame const&, std::span<std::uint8_t const>,
    std::span<std::int16_t const>, int, int, int, int, int, int, int,
    std::vector<std::int32_t>&) { return false; }
inline void shutdownComputeDevice() {}
#endif

} // namespace paimon::gifimport
