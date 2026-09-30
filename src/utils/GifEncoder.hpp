#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// minimal animated gif for .ani cursors.

namespace paimon::gif {

struct EncodeFrame {
    int width = 0;
    int height = 0;
    int delayMs = 100;
    std::vector<uint8_t> rgba;   // width*height*4 rgba8888 (top-down)
};

// empty on failure.
std::vector<uint8_t> encode(std::vector<EncodeFrame> const& frames,
                            uint8_t alphaThreshold = 128);

} // namespace paimon::gif
