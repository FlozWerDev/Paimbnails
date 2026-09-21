#pragma once

#include <cstdint>

namespace paimon {

// Copy an RGBA buffer (top-down, no padding) to the clipboard as an image. Windows
// opens the clipboard once (foreground HWND owner) for DIBV5 + DIB + PNG; other platforms stub false.
bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height);

} // namespace paimon
