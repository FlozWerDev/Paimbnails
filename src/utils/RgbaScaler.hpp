#pragma once

#include <cstdint>
#include <memory>

namespace paimon::rgba {

// tightly packed rgba8888; caller owns the result. libyuv when present,
// fallback otherwise (keeps host-side tests working).
std::unique_ptr<uint8_t[]> scale(
    uint8_t const* src,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight
);

} // namespace paimon::rgba
