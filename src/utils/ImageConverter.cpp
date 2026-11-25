#include "FilesystemPath.hpp"
#include "ImageConverter.hpp"
#include <Geode/Geode.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    void stbiWriteToVector(void* context, void* data, int size) {
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    }

    // E4: pre-alloc cap coherente con LocalThumbs kMaxRgbPixels; el decode de
    // red nunca debe reservar antes de validar dimensiones.
    constexpr uint64_t kMaxPixels = 16ull * 1024 * 1024;
    constexpr uint32_t kMaxDim = 16384;
}

std::vector<uint8_t> ImageConverter::rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height) {
    uint64_t const pixels64 = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim || pixels64 > kMaxPixels) {
        log::error("[ImageConverter] RGB dimensions exceed cap: {}x{}", width, height);
        return {};
    }
    if (rgbData.size() < static_cast<size_t>(pixels64 * 3)) {
        log::error("[ImageConverter] RGB data smaller than dimensions: {} < {}", rgbData.size(), pixels64 * 3);
        return {};
    }
    size_t pixelCount = static_cast<size_t>(pixels64);
    std::vector<uint8_t> rgba(pixelCount * 4);
    rgbToRgbaFast(rgbData.data(), rgba.data(), pixelCount);
    return rgba;
}

void ImageConverter::rgbToRgbaFast(uint8_t const* rgb, uint8_t* rgbaOut, size_t pixelCount) {
    if (pixelCount == 0) return;

    for (size_t i = 0; i < pixelCount; ++i) {
        uint32_t pixel =
            static_cast<uint32_t>(rgb[i * 3 + 0]) |
            (static_cast<uint32_t>(rgb[i * 3 + 1]) << 8) |
            (static_cast<uint32_t>(rgb[i * 3 + 2]) << 16) |
            (0xFFu << 24);
        std::memcpy(rgbaOut + i * 4, &pixel, sizeof(pixel));
    }
}

void ImageConverter::rgbaToRgbFast(uint8_t const* rgba, uint8_t* rgbOut, size_t pixelCount) {
    for (size_t i = 0; i < pixelCount; ++i) {
        rgbOut[i * 3 + 0] = rgba[i * 4 + 0];
        rgbOut[i * 3 + 1] = rgba[i * 4 + 1];
        rgbOut[i * 3 + 2] = rgba[i * 4 + 2];
    }
}

bool ImageConverter::rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    if (!rgba || width == 0 || height == 0) return false;
    uint64_t const pixels64 = static_cast<uint64_t>(width) * height;
    if (width > kMaxDim || height > kMaxDim || pixels64 > kMaxPixels) {
        log::error("[ImageConverter] PNG dimensions exceed cap: {}x{}", width, height);
        return false;
    }

    outPngData.clear();
    outPngData.reserve(static_cast<size_t>(width) * height);
    int ok = stbi_write_png_to_func(stbiWriteToVector, &outPngData,
        static_cast<int>(width), static_cast<int>(height), 4, rgba,
        static_cast<int>(width) * 4);
    return ok != 0;
}

bool ImageConverter::rgbaToJpegBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, int quality) {
    if (!rgba || width == 0 || height == 0) return false;
    uint64_t const pixels64 = static_cast<uint64_t>(width) * height;
    if (width > kMaxDim || height > kMaxDim || pixels64 > kMaxPixels) {
        log::error("[ImageConverter] JPEG dimensions exceed cap: {}x{}", width, height);
        return false;
    }

    // stb writes JPEG from RGB; dropping alpha is the caller's decision.
    std::vector<uint8_t> rgb(static_cast<size_t>(pixels64) * 3);
    rgbaToRgbFast(rgba, rgb.data(), static_cast<size_t>(pixels64));

    outData.clear();
    outData.reserve(static_cast<size_t>(pixels64) / 4);
    int const clampedQuality = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
    int ok = stbi_write_jpg_to_func(stbiWriteToVector, &outData,
        static_cast<int>(width), static_cast<int>(height), 3, rgb.data(), clampedQuality);
    return ok != 0 && !outData.empty();
}

bool ImageConverter::saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath) {
    std::vector<uint8_t> pngData;
    if (!rgbaToPngBuffer(rgba, width, height, pngData)) {
        log::error("[ImageConverter] PNG encode failed");
        return false;
    }
    auto res = geode::utils::file::writeBinary(paimon::paths::toSdkPath(filePath), pngData);
    if (res.isErr()) {
        log::error("[ImageConverter] Cannot write PNG file: {}", res.unwrapErr());
        return false;
    }
    return true;
}

bool ImageConverter::rgbaToWebpBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>&, float) {
    if (!rgba || width == 0 || height == 0) return false;

    log::warn("[ImageConverter] WebP encode not available (requires ImagePlus mod)");
    return false;
}

bool ImageConverter::rgbaToJxlBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>&, float) {
    if (!rgba || width == 0 || height == 0) return false;

    log::warn("[ImageConverter] JPEG XL encode not available (requires ImagePlus mod)");
    return false;
}

bool ImageConverter::rgbaToQoiBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>&) {
    if (!rgba || width == 0 || height == 0) return false;

    log::warn("[ImageConverter] QOI encode not available (requires ImagePlus mod)");
    return false;
}

bool ImageConverter::saveRGBAToWebP(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath, float quality) {
    std::vector<uint8_t> webpData;
    if (!rgbaToWebpBuffer(rgba, width, height, webpData, quality)) {
        log::error("[ImageConverter] WebP encode failed");
        return false;
    }
    auto res = geode::utils::file::writeBinary(paimon::paths::toSdkPath(filePath), webpData);
    if (res.isErr()) {
        log::error("[ImageConverter] Cannot write WebP file: {}", res.unwrapErr());
        return false;
    }
    return true;
}

bool ImageConverter::rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    uint64_t const pixels64 = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim || pixels64 > kMaxPixels) {
        log::error("[ImageConverter] PNG dimensions exceed cap: {}x{}", width, height);
        return false;
    }

    bool isRgba = (rgbData.size() == static_cast<size_t>(width) * height * 4);

    if (isRgba) {
        return rgbaToPngBuffer(rgbData.data(), width, height, outPngData);
    }

    if (rgbData.size() < static_cast<size_t>(pixels64 * 3)) {
        log::error("[ImageConverter] RGB data smaller than dimensions: {} < {}", rgbData.size(), pixels64 * 3);
        return false;
    }
    auto rgba = rgbToRgba(rgbData, width, height);
    if (rgba.empty()) return false;
    return rgbaToPngBuffer(rgba.data(), width, height, outPngData);
}

bool ImageConverter::loadRgbFileToPng(std::string const& rgbFilePath, std::vector<uint8_t>& outPngData) {
    std::vector<uint8_t> rgbData;
    uint32_t width, height;

    if (!loadRgbFile(rgbFilePath, rgbData, width, height)) {
        return false;
    }

    return rgbToPng(rgbData, width, height, outPngData);
}

bool ImageConverter::loadRgbFile(std::string const& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight) {
    std::ifstream in(rgbFilePath, std::ios::binary);
    if (!in) {
        log::error("[ImageConverter] Failed to open RGB file: {}", rgbFilePath);
        return false;
    }

    RGBHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || header.width == 0 || header.height == 0) {
        log::error("[ImageConverter] Invalid RGB header in file: {}", rgbFilePath);
        return false;
    }

    uint64_t const pixels = static_cast<uint64_t>(header.width) * header.height;
    if (header.width > 16384 || header.height > 16384 || pixels > 16ull * 1024 * 1024) {
        log::error("[ImageConverter] RGB dimensions too large in file: {}", rgbFilePath);
        return false;
    }
    size_t rgbSize = static_cast<size_t>(pixels * 3);
    auto headerPos = in.tellg();
    in.seekg(0, std::ios::end);
    auto fileEnd = in.tellg();
    in.seekg(headerPos, std::ios::beg);
    size_t available = (fileEnd > headerPos) ? static_cast<size_t>(fileEnd - headerPos) : 0;
    if (rgbSize == 0 || rgbSize > available) {
        log::error("[ImageConverter] RGB data size {} exceeds file contents {} (corrupt header?)", rgbSize, available);
        return false;
    }
    outRgbData.resize(rgbSize);
    in.read(reinterpret_cast<char*>(outRgbData.data()), rgbSize);

    if (!in) {
        log::error("[ImageConverter] Failed to read RGB data from file: {}", rgbFilePath);
        return false;
    }

    outWidth = header.width;
    outHeight = header.height;

    return true;
}
