#pragma once

#include <cstddef>
#include <cstdint>

namespace paimon::cache {

// disk-compaction policy for cached thumbnails. Pure and header-only so the
// portable regression suite can exercise it without Geode.

// below this size a re-encode costs more CPU than the bytes it can save.
inline constexpr std::size_t kCompactMinBytes = 96u * 1024;
// above this size a file is worth compacting even when its dimensions
// already fit the disk cap: a 1920px PNG screenshot is commonly >1 MB,
// while the same pixels as JPEG stay near 200 KB.
inline constexpr std::size_t kCompactLargeBytes = 256u * 1024;
// JPEG quality for transcoded opaque thumbnails.
inline constexpr int kCompactJpegQuality = 82;

// gIFs keep their animation and files that already hold JPEG bytes gain
// nothing from another lossy pass, so neither is ever compacted.
constexpr bool shouldCompactDiskImage(int width, int height, int maxDim,
                                      std::size_t byteSize, bool isGif,
                                      bool alreadyJpeg) {
    if (isGif || alreadyJpeg) return false;
    if (byteSize < kCompactMinBytes) return false;
    if (width > maxDim || height > maxDim) return true;
    return byteSize >= kCompactLargeBytes;
}

// true when every alpha byte is opaque. Only then may a thumbnail drop its
// alpha channel into JPEG without changing what is displayed.
inline bool isFullyOpaqueRgba(std::uint8_t const* rgba, std::size_t pixelCount) {
    if (!rgba) return false;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        if (rgba[i * 4 + 3] != 0xFF) return false;
    }
    return true;
}

// a lossy transcode must pay for its quality loss: keep it only when it
// saves at least a quarter of the stored bytes.
constexpr bool transcodeSavesEnough(std::size_t originalBytes, std::size_t encodedBytes) {
    return encodedBytes > 0 && encodedBytes * 4 <= originalBytes * 3;
}

} // namespace paimon::cache
