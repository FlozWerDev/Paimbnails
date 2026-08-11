#pragma once

#include <cstdint>
#include <utility>

struct DCColor { uint8_t r, g, b; };

namespace DominantColors {
    // Extract two dominant colors from an RGB24 buffer. Returns {A,B}.
    // If only one distinct color is found, both will be identical.
    std::pair<DCColor, DCColor> extract(const uint8_t* rgb, int width, int height);

    // Re-run extraction on a reduced overview and keep the pair that best
    // represents the full thumbnail.
    std::pair<DCColor, DCColor> extractReviewed(const uint8_t* rgb, int width, int height);
}
