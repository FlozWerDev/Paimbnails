#pragma once

#include <cstdint>

namespace paimon {

// RGBA buffer (top-down, no padding) to clipboard. Windows writes DIBV5 + DIB + PNG
// in one open; other platforms stub false.
bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height);

} // namespace paimon
