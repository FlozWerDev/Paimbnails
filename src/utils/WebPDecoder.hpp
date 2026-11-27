#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Static and animated WebP decoder backed by libwebp + libwebpdemux.
class WebPDecoder {
public:
    struct Frame {
        int delayMs = 100;
        std::vector<std::uint8_t> pixels; // RGBA8888
    };

    struct WebPData {
        int width = 0;
        int height = 0;
        bool isAnimated = false;
        std::vector<Frame> frames;
    };

    static bool isWebP(std::uint8_t const* data, std::size_t size);

    static bool getDimensions(
        std::uint8_t const* data,
        std::size_t size,
        int& width,
        int& height,
        bool& isAnimated
    );

    // maxFrames == 0 decodes all frames (capped safely); maxFrames == 1 decodes the first frame.
    static WebPData decode(
        std::uint8_t const* data,
        std::size_t size,
        int maxFrames = 0,
        int maxDimension = 8192
    );
};
