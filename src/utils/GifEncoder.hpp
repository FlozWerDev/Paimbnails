#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// GifEncoder — minimal animated GIF89a encoder (LZW + global palette).
// Needed because the mod's animation pipeline only understands GIF, so animated
// Windows cursors (.ani) must be re-encoded to an animated GIF (stb_image_write
// can't write GIF).
//
// Features:
//  - Global palette of up to 255 colors (most frequent) + 1 reserved
//    transparency index. Pixels with alpha < threshold are transparent.
//  - Infinite loop (NETSCAPE2.0 extension).
//  - Per-frame delay in centiseconds.

namespace paimon::gif {

struct EncodeFrame {
    int width = 0;
    int height = 0;
    int delayMs = 100;
    std::vector<uint8_t> rgba;   // width*height*4 RGBA8888 (top-down)
};

// Encode frames to an animated GIF89a. All frames are logically sized to the
// first (.ani frames are normally the same size). Returns the GIF bytes, or
// empty on failure.
std::vector<uint8_t> encode(std::vector<EncodeFrame> const& frames,
                            uint8_t alphaThreshold = 128);

} // namespace paimon::gif
