#pragma once

#include <cstdint>

namespace paimon {

// Copy an RGBA buffer (top-down, row-major, no padding) to the system clipboard
// as an image. Input must be byte-order R,G,B,A (same as FramebufferCapture
// after flipVertical). On Windows it registers three formats in one clipboard
// open: CF_DIBV5 (modern apps), CF_DIB (legacy/Office), and "PNG"
// (Discord/browsers). Other platforms: stub returning false. Returns true if at
// least one format was set. Thread-safe; uses the foreground HWND as owner.
bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height);

} // namespace paimon
