#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <string_view>

// image format detection by magic bytes; inline and dependency-free.
namespace paimon::format {

enum class ImageFormat {
    Unknown,
    PNG,
    JPEG,
    GIF,
    WebP,
    BMP,
    QOI,
    TIFF,
    MP4,
    APNG    // animated png (detected as png first)
};

inline ImageFormat detect(uint8_t const* data, size_t size) {
    if (!data || size < 2) return ImageFormat::Unknown;

    if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return ImageFormat::PNG;

    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return ImageFormat::JPEG;

    if (size >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8'
        && (data[4] == '7' || data[4] == '9') && data[5] == 'a')
        return ImageFormat::GIF;

    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
        && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P')
        return ImageFormat::WebP;

    if (data[0] == 'B' && data[1] == 'M')
        return ImageFormat::BMP;

    if (size >= 4 && data[0] == 'q' && data[1] == 'o' && data[2] == 'i' && data[3] == 'f')
        return ImageFormat::QOI;

    if (size >= 4 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A && data[3] == 0x00)
        || (data[0] == 'M' && data[1] == 'M' && data[2] == 0x00 && data[3] == 0x2A)))
        return ImageFormat::TIFF;

    if (size >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p')
        return ImageFormat::MP4;

    return ImageFormat::Unknown;
}

inline bool isGif(uint8_t const* data, size_t size) {
    return detect(data, size) == ImageFormat::GIF;
}

inline bool isPng(uint8_t const* data, size_t size) {
    return detect(data, size) == ImageFormat::PNG;
}

inline bool isJpeg(uint8_t const* data, size_t size) {
    return detect(data, size) == ImageFormat::JPEG;
}

inline bool isWebp(uint8_t const* data, size_t size) {
    return detect(data, size) == ImageFormat::WebP;
}

inline bool isMp4(uint8_t const* data, size_t size) {
    return detect(data, size) == ImageFormat::MP4;
}

inline bool probeWebpDimensions(uint8_t const* data, size_t size, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    if (!isWebp(data, size) || size < 25) return false;
    uint32_t riffSize = static_cast<uint32_t>(data[4])
        | (static_cast<uint32_t>(data[5]) << 8)
        | (static_cast<uint32_t>(data[6]) << 16)
        | (static_cast<uint32_t>(data[7]) << 24);
    if (riffSize < 12) return false;

    auto tagIs = [&](char a, char b, char c, char d) {
        return data[12] == static_cast<uint8_t>(a)
            && data[13] == static_cast<uint8_t>(b)
            && data[14] == static_cast<uint8_t>(c)
            && data[15] == static_cast<uint8_t>(d);
    };

    if (tagIs('V', 'P', '8', 'X') && size >= 30) {
        outWidth = 1 + (static_cast<int>(data[24]) | (static_cast<int>(data[25]) << 8) | (static_cast<int>(data[26]) << 16));
        outHeight = 1 + (static_cast<int>(data[27]) | (static_cast<int>(data[28]) << 8) | (static_cast<int>(data[29]) << 16));
        return outWidth > 0 && outHeight > 0;
    }
    if (tagIs('V', 'P', '8', ' ') && size >= 30) {
        if (data[23] != 0x9d || data[24] != 0x01 || data[25] != 0x2a) return false;
        outWidth = (static_cast<int>(data[26]) | (static_cast<int>(data[27]) << 8)) & 0x3FFF;
        outHeight = (static_cast<int>(data[28]) | (static_cast<int>(data[29]) << 8)) & 0x3FFF;
        return outWidth > 0 && outHeight > 0;
    }
    if (tagIs('V', 'P', '8', 'L') && size >= 25 && data[20] == 0x2F) {
        uint8_t b0 = data[21], b1 = data[22], b2 = data[23], b3 = data[24];
        outWidth = 1 + (static_cast<int>(b0) | ((static_cast<int>(b1) & 0x3F) << 8));
        outHeight = 1 + ((static_cast<int>(b1) >> 6) | (static_cast<int>(b2) << 2) | ((static_cast<int>(b3) & 0x0F) << 10));
        return outWidth > 0 && outHeight > 0;
    }
    return false;
}

inline bool probeImageDimensions(uint8_t const* data, size_t size, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    auto fmt = detect(data, size);
    if (fmt == ImageFormat::PNG && size >= 24 &&
        data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
        outWidth = (static_cast<int>(data[16]) << 24) | (static_cast<int>(data[17]) << 16)
            | (static_cast<int>(data[18]) << 8) | static_cast<int>(data[19]);
        outHeight = (static_cast<int>(data[20]) << 24) | (static_cast<int>(data[21]) << 16)
            | (static_cast<int>(data[22]) << 8) | static_cast<int>(data[23]);
        return outWidth > 0 && outHeight > 0;
    }
    if (fmt == ImageFormat::GIF && size >= 13) {
        outWidth = static_cast<int>(data[6]) | (static_cast<int>(data[7]) << 8);
        outHeight = static_cast<int>(data[8]) | (static_cast<int>(data[9]) << 8);
        return outWidth > 0 && outHeight > 0;
    }
    if (fmt == ImageFormat::WebP) {
        return probeWebpDimensions(data, size, outWidth, outHeight);
    }
    if (fmt == ImageFormat::JPEG && size >= 20) {
        size_t off = 2;
        while (off + 4 <= size) {
            if (data[off] != 0xFF) break;
            uint8_t marker = data[off + 1];
            if (marker == 0xFF) { ++off; continue; }
            if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
                off += 2; continue;
            }
            if (marker == 0xD9 || marker == 0xDA) break;
            uint16_t len = (static_cast<uint16_t>(data[off + 2]) << 8) | static_cast<uint16_t>(data[off + 3]);
            if (len < 2 || off + 2 + len > size) break;
            if ((marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC3) && len >= 7) {
                outHeight = (static_cast<int>(data[off + 5]) << 8) | static_cast<int>(data[off + 6]);
                outWidth = (static_cast<int>(data[off + 7]) << 8) | static_cast<int>(data[off + 8]);
                return outWidth > 0 && outHeight > 0;
            }
            off += 2 + len;
        }
    }
    return false;
}

inline bool isSafeImageHeader(uint8_t const* data, size_t size, int maxDim = 4096) {
    int w = 0, h = 0;
    if (!probeImageDimensions(data, size, w, h)) return false;
    return w > 0 && h > 0 && w <= maxDim && h <= maxDim;
}

inline bool isSafePlistBytes(uint8_t const* data, size_t size) {
    if (!data || size < 16 || size > 4u * 1024 * 1024) return false;
    if (size >= 6 && std::string_view(reinterpret_cast<char const*>(data), 6) == "bplist") {
        return true;
    }
    std::string_view sv(reinterpret_cast<char const*>(data), size);
    if (sv.find('\0') != std::string_view::npos) return false;
    if (sv.starts_with("\xEF\xBB\xBF")) sv.remove_prefix(3);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    if (!sv.starts_with("<?xml") && !sv.starts_with("<!DOCTYPE plist") && !sv.starts_with("<plist")) {
        return false;
    }
    auto containsCaseInsensitive = [](std::string_view haystack, std::string_view needle) {
        return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }) != haystack.end();
    };
    if (containsCaseInsensitive(sv, "<!entity") || containsCaseInsensitive(sv, "<!element") ||
        containsCaseInsensitive(sv, "<!attlist") || containsCaseInsensitive(sv, "<!notation")) {
        return false;
    }
    return containsCaseInsensitive(sv, "<plist") && containsCaseInsensitive(sv, "</plist>");
}

} // namespace paimon::format
