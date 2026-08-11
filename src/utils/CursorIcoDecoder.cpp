#include "CursorIcoDecoder.hpp"
#include "ImageLoadHelper.hpp"
#include "FormatDetect.hpp"
#include <Geode/loader/Log.hpp>
#include <cstring>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::cursor_ico {

namespace {

// Bounds-checked little-endian reads.
inline uint16_t rd16(uint8_t const* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t rd32(uint8_t const* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24));
}
inline int32_t rd32s(uint8_t const* p) {
    return static_cast<int32_t>(rd32(p));
}

constexpr int kMaxDim = 1024; // cursors should stay small

// Decode a BMP/DIB or PNG icon payload.
bool decodeIconImage(uint8_t const* img, size_t imgSize, DecodedFrame& out) {
    if (imgSize < 8) return false;

// Decode embedded PNGs without CCTexture2D so import is GL-independent.
    if (paimon::format::isPng(img, imgSize)) {
        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(
            img, static_cast<int>(imgSize), &w, &h, &channels, 4);
        if (!pixels) return false;
        if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) {
            stbi_image_free(pixels);
            return false;
        }
        out.width  = w;
        out.height = h;
        size_t n = static_cast<size_t>(w) * h * 4;
        out.rgba.assign(pixels, pixels + n);
        stbi_image_free(pixels);
        return true;
    }

// DIB height includes the color image and the 1bpp AND mask.
    if (imgSize < 40) return false;
    uint32_t headerSize = rd32(img + 0);
    if (headerSize < 40) return false;

    int32_t  w        = rd32s(img + 4);
    int32_t  hRaw     = rd32s(img + 8);
    uint16_t bpp      = rd16(img + 14);
    uint32_t compress = rd32(img + 16);

    if (w <= 0 || w > kMaxDim) return false;
    int32_t h = hRaw / 2;
    if (h <= 0 || h > kMaxDim) return false;
    if (compress != 0) return false;

    size_t pixelCount = static_cast<size_t>(w) * h;
    out.width  = w;
    out.height = h;
    out.rgba.assign(pixelCount * 4, 0);

    uint8_t const* p = img + headerSize;
    uint8_t const* end = img + imgSize;

// DIB rows are bottom-up; output rows are top-down.
    auto setPixel = [&](int x, int yTop, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        size_t idx = (static_cast<size_t>(yTop) * w + x) * 4;
        out.rgba[idx + 0] = r;
        out.rgba[idx + 1] = g;
        out.rgba[idx + 2] = b;
        out.rgba[idx + 3] = a;
    };

    if (bpp == 32) {
// Rows are 4-byte aligned and stored as BGRA.
        size_t rowBytes = static_cast<size_t>(w) * 4;
        for (int y = 0; y < h; ++y) {
            uint8_t const* row = p + static_cast<size_t>(y) * rowBytes;
            if (row + rowBytes > end) break;
            int yTop = h - 1 - y;
            for (int x = 0; x < w; ++x) {
                uint8_t const* px = row + static_cast<size_t>(x) * 4;
                setPixel(x, yTop, px[2], px[1], px[0], px[3]);
            }
        }
        return true;
    }

    if (bpp == 24) {
    size_t rowBytes = ((static_cast<size_t>(w) * 3 + 3) / 4) * 4;
        size_t colorBytes = rowBytes * h;
        uint8_t const* maskBase = p + colorBytes;
    size_t maskRowBytes = ((static_cast<size_t>(w) + 31) / 32) * 4;
        for (int y = 0; y < h; ++y) {
            uint8_t const* row = p + static_cast<size_t>(y) * rowBytes;
            if (row + rowBytes > end) break;
            uint8_t const* maskRow = maskBase + static_cast<size_t>(y) * maskRowBytes;
            int yTop = h - 1 - y;
            for (int x = 0; x < w; ++x) {
                uint8_t const* px = row + static_cast<size_t>(x) * 3;
                uint8_t a = 255;
                if (maskRow + (x / 8) < end) {
                    uint8_t maskByte = maskRow[x / 8];
                    bool transparent = (maskByte >> (7 - (x % 8))) & 1;
                    if (transparent) a = 0;
                }
                setPixel(x, yTop, px[2], px[1], px[0], a);
            }
        }
        return true;
    }

    if (bpp == 8 || bpp == 4 || bpp == 1) {
        int paletteCount = 1 << bpp;
        uint8_t const* palette = img + headerSize;
    size_t paletteBytes = static_cast<size_t>(paletteCount) * 4;
        uint8_t const* bits = palette + paletteBytes;
        if (bits >= end) return false;

        size_t rowBits = static_cast<size_t>(w) * bpp;
    size_t rowBytes = ((rowBits + 31) / 32) * 4;
        size_t colorBytes = rowBytes * h;
        uint8_t const* maskBase = bits + colorBytes;
        size_t maskRowBytes = ((static_cast<size_t>(w) + 31) / 32) * 4;

        for (int y = 0; y < h; ++y) {
            uint8_t const* row = bits + static_cast<size_t>(y) * rowBytes;
            if (row + rowBytes > end) break;
            uint8_t const* maskRow = maskBase + static_cast<size_t>(y) * maskRowBytes;
            int yTop = h - 1 - y;
            for (int x = 0; x < w; ++x) {
                int index = 0;
                if (bpp == 8) {
                    index = row[x];
                } else if (bpp == 4) {
                    uint8_t byte = row[x / 2];
                    index = (x & 1) ? (byte & 0x0F) : (byte >> 4);
} else {
                    uint8_t byte = row[x / 8];
                    index = (byte >> (7 - (x % 8))) & 1;
                }
                if (index >= paletteCount) index = 0;
                uint8_t const* pe = palette + static_cast<size_t>(index) * 4;
                uint8_t a = 255;
                if (maskRow + (x / 8) < end) {
                    uint8_t maskByte = maskRow[x / 8];
                    bool transparent = (maskByte >> (7 - (x % 8))) & 1;
                    if (transparent) a = 0;
                }
                setPixel(x, yTop, pe[2], pe[1], pe[0], a);
            }
        }
        return true;
    }

    log::warn("[CursorIcoDecoder] Unsupported icon bpp: {}", bpp);
    return false;
}

// Decode the highest-resolution frame from a .ico/.cur.
bool decodeIcoInternal(uint8_t const* data, size_t size, DecodedFrame& out) {
    if (size < 6) return false;
    uint16_t reserved = rd16(data + 0);
    uint16_t type     = rd16(data + 2);
    uint16_t count    = rd16(data + 4);
    if (reserved != 0 || (type != 1 && type != 2) || count == 0) return false;

    size_t dirSize = 6 + static_cast<size_t>(count) * 16;
    if (size < dirSize) return false;

    int bestIdx = -1;
    long bestArea = -1;
    for (int i = 0; i < count; ++i) {
        uint8_t const* e = data + 6 + static_cast<size_t>(i) * 16;
        int w = e[0] == 0 ? 256 : e[0];
        int h = e[1] == 0 ? 256 : e[1];
        long area = static_cast<long>(w) * h;
        if (area > bestArea) { bestArea = area; bestIdx = i; }
    }
    if (bestIdx < 0) return false;

    uint8_t const* e = data + 6 + static_cast<size_t>(bestIdx) * 16;
    uint32_t bytesInRes  = rd32(e + 8);
    uint32_t imageOffset = rd32(e + 12);
// Reject offset + size overflow before reading the entry.
    if (bytesInRes == 0 || imageOffset > size || bytesInRes > size - imageOffset) return false;

    return decodeIconImage(data + imageOffset, bytesInRes, out);
}

}

bool isIco(uint8_t const* data, size_t size) {
    return size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00;
}
bool isCur(uint8_t const* data, size_t size) {
    return size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x02 && data[3] == 0x00;
}
bool isAni(uint8_t const* data, size_t size) {
    return size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "ACON", 4) == 0;
}
bool isSupported(uint8_t const* data, size_t size) {
    return isIco(data, size) || isCur(data, size) || isAni(data, size);
}

DecodeResult decodeIco(uint8_t const* data, size_t size) {
    DecodeResult res;
    DecodedFrame frame;
    if (!decodeIcoInternal(data, size, frame)) {
        res.error = "ico_decode_failed";
        return res;
    }
    res.success = true;
    res.animated = false;
    res.frames.push_back(std::move(frame));
    return res;
}

DecodeResult decodeAni(uint8_t const* data, size_t size) {
    DecodeResult res;
    if (!isAni(data, size)) { res.error = "not_ani"; return res; }

// Parse RIFF/ACON chunks for rates, sequence, and embedded icon frames.
uint32_t defaultJiffies = 6;     // ~100 ms
std::vector<DecodedFrame> icons;
std::vector<uint32_t> rates;
std::vector<uint32_t> seq;

auto const* p = data + 12;
    auto const* end = data + size;

    while (p + 8 <= end) {
        char id[5] = {0};
        memcpy(id, p, 4);
        uint32_t chunkSize = rd32(p + 4);
        uint8_t const* body = p + 8;
        if (body + chunkSize > end) break;

        if (memcmp(id, "anih", 4) == 0 && chunkSize >= 36) {
            defaultJiffies = rd32(body + 28);
            if (defaultJiffies == 0) defaultJiffies = 6;
        } else if (memcmp(id, "rate", 4) == 0) {
            int n = static_cast<int>(chunkSize / 4);
            for (int i = 0; i < n; ++i) rates.push_back(rd32(body + i * 4));
        } else if (memcmp(id, "seq ", 4) == 0) {
            int n = static_cast<int>(chunkSize / 4);
            for (int i = 0; i < n; ++i) seq.push_back(rd32(body + i * 4));
        } else if (memcmp(id, "LIST", 4) == 0 && chunkSize >= 4 && memcmp(body, "fram", 4) == 0) {
            uint8_t const* lp = body + 4;
            uint8_t const* lend = body + chunkSize;
            while (lp + 8 <= lend) {
                char sid[5] = {0};
                memcpy(sid, lp, 4);
                uint32_t sSize = rd32(lp + 4);
                uint8_t const* sBody = lp + 8;
                if (sBody + sSize > lend) break;
                if (memcmp(sid, "icon", 4) == 0) {
                    DecodedFrame frame;
                    if (decodeIcoInternal(sBody, sSize, frame)) {
                        icons.push_back(std::move(frame));
                    }
                }
                lp = sBody + sSize + (sSize & 1);
            }
        }

p = body + chunkSize + (chunkSize & 1);
    }

    if (icons.empty()) { res.error = "ani_no_frames"; return res; }

    std::vector<DecodedFrame> out;
    auto jiffiesToMs = [](uint32_t j) -> int {
        if (j == 0) j = 6;
        return static_cast<int>(j * 1000.0 / 60.0);
    };

    size_t steps = !seq.empty() ? seq.size() : icons.size();
    for (size_t s = 0; s < steps; ++s) {
        size_t iconIdx = !seq.empty() ? seq[s] : s;
        if (iconIdx >= icons.size()) iconIdx = icons.size() - 1;

        DecodedFrame f = icons[iconIdx];
        uint32_t jiffies = (s < rates.size()) ? rates[s] : defaultJiffies;
        f.delayMs = jiffiesToMs(jiffies);
        out.push_back(std::move(f));
    }

    res.success = true;
    res.animated = out.size() > 1;
    res.frames = std::move(out);
    return res;
}

DecodeResult decode(uint8_t const* data, size_t size) {
    if (isAni(data, size)) return decodeAni(data, size);
    if (isIco(data, size) || isCur(data, size)) return decodeIco(data, size);
    DecodeResult res;
    res.error = "unsupported_format";
    return res;
}

}
