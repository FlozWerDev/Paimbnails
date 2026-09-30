#pragma once
#include <cstdint>
#include <cstddef>

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
    return data && size >= 6
        && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8'
        && (data[4] == '7' || data[4] == '9') && data[5] == 'a';
}

inline bool isPng(uint8_t const* data, size_t size) {
    return data && size >= 4
        && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
}

inline bool isJpeg(uint8_t const* data, size_t size) {
    return data && size >= 3
        && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

inline bool isWebp(uint8_t const* data, size_t size) {
    return data && size >= 12
        && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
        && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P';
}

inline bool isMp4(uint8_t const* data, size_t size) {
    return data && size >= 8
        && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p';
}

} // namespace paimon::format
