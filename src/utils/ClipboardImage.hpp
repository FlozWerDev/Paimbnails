#pragma once

#include <cstdint>

namespace paimon {

// rgba buffer (top-down, no padding) to clipboard. windows writes dibv5 + dib + png
// in one open; other platforms stub false.
bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height);

} // namespace paimon
