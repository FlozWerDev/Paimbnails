#include "../src/features/thumbnails/services/CacheImagePolicy.hpp"

#include <array>
#include <cassert>

int main() {
    using namespace paimon::cache;
    constexpr int kMaxDim = 1920;

    // GIFs and already-JPEG files are never re-encoded.
    assert(!shouldCompactDiskImage(4000, 2000, kMaxDim, 2'000'000, true, false));
    assert(!shouldCompactDiskImage(4000, 2000, kMaxDim, 2'000'000, false, true));
    // Small files are not worth the CPU, even when oversized.
    assert(!shouldCompactDiskImage(4000, 2000, kMaxDim, 1024, false, false));
    // Oversized dimensions compact once the file is big enough.
    assert(shouldCompactDiskImage(4000, 2000, kMaxDim, 200'000, false, false));
    // In-cap dimensions compact only when the file itself is large.
    assert(!shouldCompactDiskImage(1280, 720, kMaxDim, 200'000, false, false));
    assert(shouldCompactDiskImage(1280, 720, kMaxDim, 300'000, false, false));

    // Only fully opaque pixels may drop alpha into JPEG.
    constexpr std::array<std::uint8_t, 8> opaque{1, 2, 3, 255, 4, 5, 6, 255};
    constexpr std::array<std::uint8_t, 8> translucent{1, 2, 3, 255, 4, 5, 6, 254};
    assert(isFullyOpaqueRgba(opaque.data(), 2));
    assert(!isFullyOpaqueRgba(translucent.data(), 2));
    assert(!isFullyOpaqueRgba(nullptr, 2));
    assert(isFullyOpaqueRgba(opaque.data(), 0));

    // A transcode must save at least a quarter of the bytes.
    assert(transcodeSavesEnough(1000, 750));
    assert(!transcodeSavesEnough(1000, 751));
    assert(!transcodeSavesEnough(1000, 0));
    assert(!transcodeSavesEnough(0, 0));
}
