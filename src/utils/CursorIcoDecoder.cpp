#include "CursorIcoDecoder.hpp"
#include "ImageLoadHelper.hpp"
#include "FormatDetect.hpp"
#include <Geode/loader/Log.hpp>
#include <cstring>
#include <algorithm>
#include <limits>
#include <memory>

using namespace geode::prelude;

namespace paimon::cursor_ico {

namespace {

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

bool decodeIconImage(uint8_t const* img, size_t imgSize, DecodedFrame& out) {
    if (!img || imgSize < 8) return false;

    // png cursors decode on workers without touching gl.
    if (paimon::format::isPng(img, imgSize)) {
        if (imgSize > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(img, static_cast<int>(imgSize), &w, &h, &channels) ||
            w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) return false;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(img, static_cast<int>(imgSize), &w, &h, &channels, 4),
            &stbi_image_free);
        if (!pixels || w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) return false;
        out.rgba.assign(pixels.get(), pixels.get() + static_cast<size_t>(w) * h * 4);
        out.width = w;
        out.height = h;
        return true;
    }

    if (imgSize < 40) return false;
    size_t const headerSize = rd32(img);
    if (headerSize < 40 || headerSize > imgSize) return false;
    int const w = rd32s(img + 4);
    // dib height includes the color plane and the one-bit transparency mask.
    int const h = rd32s(img + 8) / 2;
    uint16_t const bpp = rd16(img + 14);
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim || rd32(img + 16) != 0) return false;
    if (bpp != 32 && bpp != 24 && bpp != 8 && bpp != 4 && bpp != 1) {
        log::warn("[CursorIcoDecoder] Unsupported icon bpp: {}", bpp);
        return false;
    }

    size_t paletteCount = bpp <= 8 ? size_t{1} << bpp : 0;
    if (paletteCount) {
        auto const usedColors = rd32(img + 32);
        if (usedColors > paletteCount) return false;
        if (usedColors) paletteCount = usedColors;
    }
    size_t const paletteBytes = paletteCount * 4;
    if (paletteBytes > imgSize - headerSize) return false;
    size_t const pixelsOffset = headerSize + paletteBytes;
    size_t const rowBytes = (static_cast<size_t>(w) * bpp + 31) / 32 * 4;
    size_t const colorBytes = rowBytes * h;
    if (colorBytes > imgSize - pixelsOffset) return false;
    size_t const maskOffset = pixelsOffset + colorBytes;
    size_t const maskRowBytes = (static_cast<size_t>(w) + 31) / 32 * 4;

    out.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    out.width = w;
    out.height = h;
    auto const* palette = img + headerSize;
    for (int y = 0; y < h; ++y) {
        auto const* row = img + pixelsOffset + static_cast<size_t>(y) * rowBytes;
        auto* dst = out.rgba.data() + static_cast<size_t>(h - 1 - y) * w * 4;
        for (int x = 0; x < w; ++x) {
            uint8_t const* color;
            if (bpp >= 24) {
                color = row + static_cast<size_t>(x) * (bpp / 8);
            } else {
                unsigned const bits = row[static_cast<size_t>(x) * bpp / 8];
                unsigned const shift = 8 - bpp - (x * bpp % 8);
                size_t const index = (bits >> shift) & ((1u << bpp) - 1);
                if (index >= paletteCount) return false;
                color = palette + index * 4;
            }
            uint8_t alpha = bpp == 32 ? color[3] : 255;
            if (bpp != 32) {
                size_t const maskByte = maskOffset + static_cast<size_t>(y) * maskRowBytes + x / 8;
                if (maskByte < imgSize && ((img[maskByte] >> (7 - x % 8)) & 1)) alpha = 0;
            }
            dst[x * 4] = color[2];
            dst[x * 4 + 1] = color[1];
            dst[x * 4 + 2] = color[0];
            dst[x * 4 + 3] = alpha;
        }
    }
    return true;
}

bool decodeIcoInternal(uint8_t const* data, size_t size, DecodedFrame& out) {
    if (!data || size < 6) return false;
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
    // reject offset+size overflow before reading.
    if (bytesInRes == 0 || imageOffset > size || bytesInRes > size - imageOffset) return false;

    return decodeIconImage(data + imageOffset, bytesInRes, out);
}

}

bool isIco(uint8_t const* data, size_t size) {
    return data && size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00;
}
bool isCur(uint8_t const* data, size_t size) {
    return data && size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x02 && data[3] == 0x00;
}
bool isAni(uint8_t const* data, size_t size) {
    return data && size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "ACON", 4) == 0;
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
    size_t const riffSize = rd32(data + 4);
    if (riffSize < 4 || riffSize > size - 8) { res.error = "ani_truncated"; return res; }

    constexpr size_t kMaxFrames = 256;
    constexpr size_t kMaxDecodedBytes = 64 * 1024 * 1024;
    uint32_t defaultJiffies = 6;
    std::vector<DecodedFrame> icons;
    std::vector<uint32_t> rates;
    std::vector<uint32_t> seq;
    size_t decodedBytes = 0;
    auto const* p = data + 12;
    auto const* end = data + 8 + riffSize;
    while (static_cast<size_t>(end - p) >= 8) {
        size_t const chunkSize = rd32(p + 4);
        auto const* body = p + 8;
        if (chunkSize > static_cast<size_t>(end - body)) { res.error = "ani_truncated"; return res; }
        if (std::memcmp(p, "anih", 4) == 0 && chunkSize >= 36) {
            defaultJiffies = rd32(body + 28);
        } else if (std::memcmp(p, "rate", 4) == 0 || std::memcmp(p, "seq ", 4) == 0) {
            auto& values = std::memcmp(p, "rate", 4) == 0 ? rates : seq;
            size_t const count = chunkSize / 4;
            if (count > kMaxFrames - values.size()) { res.error = "ani_too_large"; return res; }
            for (size_t i = 0; i < count; ++i) values.push_back(rd32(body + i * 4));
        } else if (std::memcmp(p, "LIST", 4) == 0 && chunkSize >= 4 && std::memcmp(body, "fram", 4) == 0) {
            auto const* item = body + 4;
            auto const* listEnd = body + chunkSize;
            while (static_cast<size_t>(listEnd - item) >= 8) {
                size_t const itemSize = rd32(item + 4);
                auto const* icon = item + 8;
                if (itemSize > static_cast<size_t>(listEnd - icon)) { res.error = "ani_truncated"; return res; }
                if (std::memcmp(item, "icon", 4) == 0) {
                    DecodedFrame frame;
                    if (decodeIcoInternal(icon, itemSize, frame)) {
                        if (icons.size() >= kMaxFrames || frame.rgba.size() > kMaxDecodedBytes - decodedBytes) {
                            res.error = "ani_too_large";
                            return res;
                        }
                        decodedBytes += frame.rgba.size();
                        icons.push_back(std::move(frame));
                    }
                }
                item = icon + itemSize;
                if ((itemSize & 1) && item < listEnd) ++item;
            }
        }
        p = body + chunkSize;
        if ((chunkSize & 1) && p < end) ++p;
    }
    if (icons.empty()) { res.error = "ani_no_frames"; return res; }

    size_t const steps = seq.empty() ? icons.size() : seq.size();
    std::vector<DecodedFrame> frames;
    frames.reserve(steps);
    decodedBytes = 0;
    for (size_t step = 0; step < steps; ++step) {
        size_t const index = std::min<size_t>(seq.empty() ? step : seq[step], icons.size() - 1);
        if (icons[index].rgba.size() > kMaxDecodedBytes - decodedBytes) {
            res.error = "ani_too_large";
            return res;
        }
        decodedBytes += icons[index].rgba.size();
        auto frame = icons[index];
        uint32_t jiffies = step < rates.size() ? rates[step] : defaultJiffies;
        if (!jiffies) jiffies = 6;
        frame.delayMs = static_cast<int>(std::clamp<uint64_t>(static_cast<uint64_t>(jiffies) * 1000 / 60,
                                                            1, std::numeric_limits<int>::max()));
        frames.push_back(std::move(frame));
    }
    res.success = true;
    res.animated = frames.size() > 1;
    res.frames = std::move(frames);
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
