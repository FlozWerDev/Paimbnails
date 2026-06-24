#pragma once

// Deterministic "random" preview zone selection.
//// length. Deterministic so a level always previews the same zone (re-generation
#include <cstdint>

namespace paimon::autopreview {

inline float zoneFraction(int levelID) {
    // Knuth multiplicative hash -> spread the low bits.
    uint32_t h = static_cast<uint32_t>(levelID) * 2654435761u;
    float f = static_cast<float>(h % 100000u) / 100000.0f; // [0,1)
    constexpr float lo = 0.12f;
    constexpr float hi = 0.85f;
    return lo + f * (hi - lo);
}

} // namespace paimon::autopreview
