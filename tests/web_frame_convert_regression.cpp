#include "../src/features/backgrounds/services/WebFrameConvert.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace paimon::wallpaper;

namespace {

// Every pixel gets a distinct BGRA pattern so a channel swap or a skipped
// pixel cannot pass by accident.
void fillSource(std::vector<std::uint8_t>& source) {
    for (std::size_t i = 0; i + 3 < source.size(); i += 4) {
        auto const index = static_cast<std::uint8_t>(i / 4);
        source[i + 0] = static_cast<std::uint8_t>(index * 7u + 3u);   // B
        source[i + 1] = static_cast<std::uint8_t>(index * 11u + 5u);  // G
        source[i + 2] = static_cast<std::uint8_t>(index * 13u + 9u);  // R
        source[i + 3] = static_cast<std::uint8_t>(index);             // A (ignored)
    }
}

void check(std::size_t pixels) {
    constexpr std::uint8_t kGuard = 0xAB;
    std::vector<std::uint8_t> source(pixels * 4);
    fillSource(source);

    std::vector<std::uint8_t> destination(pixels * 4 + 8, kGuard);
    convertBgraToRgba(destination.data() + 4, source.data(), pixels);

    assert(destination[0] == kGuard);
    for (std::size_t i = 0; i < pixels; ++i) {
        auto const offset = 4 + i * 4;
        assert(destination[offset + 0] == source[i * 4 + 2]);
        assert(destination[offset + 1] == source[i * 4 + 1]);
        assert(destination[offset + 2] == source[i * 4 + 0]);
        assert(destination[offset + 3] == 0xFF);
    }
    for (std::size_t i = pixels * 4 + 4; i < destination.size(); ++i) {
        assert(destination[i] == kGuard);
    }
}

} // namespace

int main() {
    // Includes sizes below, at and above the four-pixel vector stride, plus
    // tails so the scalar fallback is exercised after the SIMD body.
    for (std::size_t pixels : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3},
             std::size_t{4}, std::size_t{5}, std::size_t{7}, std::size_t{8}, std::size_t{15},
             std::size_t{16}, std::size_t{17}, std::size_t{63}, std::size_t{64}, std::size_t{65},
             std::size_t{1600}, std::size_t{1601}}) {
        check(pixels);
    }
    std::printf("PASS: bgra to rgba conversion (simd + scalar tail)\n");
    return 0;
}
