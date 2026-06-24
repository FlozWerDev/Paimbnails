#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

// CursorIcoDecoder — decodes Windows cursors (.cur/.ico) and animated cursors
// (.ani) to RGBA8888. stb_image and cocos2d's CCImage don't understand these,
// so this converts them to raw RGBA that CursorManager re-encodes to PNG/GIF.
//
// Formats:
//  - .ico/.cur: ICONDIR + ICONDIRENTRY[] + images (BMP DIB or PNG). .cur stores
//    a hotspot instead of planes/bpp but the image payload is identical.
//  - .ani: RIFF ("ACON") container with a "fram" list of "icon" chunks, each a
//    full .ico/.cur (one animation frame).

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

// True if the content matches a format decodable here.
bool isSupported(uint8_t const* data, size_t size);

// Decode a .ico/.cur, choosing the highest-resolution image available.
DecodeResult decodeIco(uint8_t const* data, size_t size);

// Decode a full .ani (all frames, in playback order).
DecodeResult decodeAni(uint8_t const* data, size_t size);

// Generic entry point: detect the format and delegate.
DecodeResult decode(uint8_t const* data, size_t size);

} // namespace paimon::cursor_ico
