#pragma once

#include <cstdint>
#include <memory>

namespace paimon::rgba {

// Scales tightly packed RGBA8888 pixels. The returned buffer is owned by the
// caller, same channel order as input. libyuv when available; the fallback keeps host-side tests working.
std::unique_ptr<uint8_t[]> scale(
    uint8_t const* src,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight
);

} // namespace paimon::rgba
