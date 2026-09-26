#pragma once

#include <cstdint>
#include <utility>

struct DCColor { uint8_t r, g, b; };

namespace DominantColors {
    // two dominant colors from RGB24; identical when only one exists.
    std::pair<DCColor, DCColor> extract(const uint8_t* rgb, int width, int height);

    // re-run on a reduced overview; keep the most representative pair.
    std::pair<DCColor, DCColor> extractReviewed(const uint8_t* rgb, int width, int height);
}
