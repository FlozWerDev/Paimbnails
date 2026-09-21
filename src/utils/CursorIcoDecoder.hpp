#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

// Decodes .cur/.ico/.ani to RGBA.

namespace paimon::cursor_ico {

struct DecodedFrame {
    int width = 0;
    int height = 0;
    int delayMs = 100;            // frame duration (only relevant for .ani)
    std::vector<uint8_t> rgba;    // width*height*4 RGBA8888 (top-down)
};

struct DecodeResult {
    bool success = false;
    bool animated = false;
    std::vector<DecodedFrame> frames;
    std::string error;
};

// .ico = "00 00 01 00", .cur = "00 00 02 00"
bool isIco(uint8_t const* data, size_t size);
bool isCur(uint8_t const* data, size_t size);
// .ani = "RIFF" .... "ACON"
bool isAni(uint8_t const* data, size_t size);

bool isSupported(uint8_t const* data, size_t size);

// Elige la imagen mas grande.
DecodeResult decodeIco(uint8_t const* data, size_t size);

DecodeResult decodeAni(uint8_t const* data, size_t size);

DecodeResult decode(uint8_t const* data, size_t size);

} // namespace paimon::cursor_ico
