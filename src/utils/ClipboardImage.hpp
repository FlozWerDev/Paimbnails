#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace paimon {

struct ClipboardImageData {
    std::vector<uint8_t> encodedBytes;
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool isEncodedFile = false;
};

// rgba buffer (top-down, no padding) to clipboard. windows writes dibv5 + dib + png
// in one open; other platforms stub false.
bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height);

bool clipboardHasImage();
std::optional<ClipboardImageData> readImageFromClipboard();

} // namespace paimon
