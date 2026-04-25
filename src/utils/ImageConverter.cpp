#include "ImageConverter.hpp"
#include <Geode/Geode.hpp>
#include <prevter.imageplus/include/events.hpp>
#include <Geode/utils/file.hpp>
#include <filesystem>

// stb_image_write como fallback si ImagePlus no esta disponible
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    void stbiWriteToVector(void* context, void* data, int size) {
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    }
}

std::vector<uint8_t> ImageConverter::rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height) {
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    rgbToRgbaFast(rgbData.data(), rgba.data(), pixelCount);
    return rgba;
}

void ImageConverter::rgbToRgbaFast(uint8_t const* rgb, uint8_t* rgbaOut, size_t pixelCount) {
    // Step 1: Fill all alpha channels at once (compiler vectorizes this)
    for (size_t i = 0; i < pixelCount; ++i) {
        rgbaOut[i * 4 + 3] = 255;
    }
    
    // Step 2: Interleave RGB values (also auto-vectorized by compiler)
    for (size_t i = 0; i < pixelCount; ++i) {
        rgbaOut[i * 4 + 0] = rgb[i * 3 + 0];
        rgbaOut[i * 4 + 1] = rgb[i * 3 + 1];
        rgbaOut[i * 4 + 2] = rgb[i * 3 + 2];
    }
}

void ImageConverter::rgbaToRgbFast(uint8_t const* rgba, uint8_t* rgbOut, size_t pixelCount) {
    // Drop alpha channel — auto-vectorized by compiler
    for (size_t i = 0; i < pixelCount; ++i) {
        rgbOut[i * 3 + 0] = rgba[i * 4 + 0];
        rgbOut[i * 3 + 1] = rgba[i * 4 + 1];
        rgbOut[i * 3 + 2] = rgba[i * 4 + 2];
    }
}

bool ImageConverter::rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    if (!rgba || width == 0 || height == 0) return false;

    // ImagePlus encode primero
    if (imgp::isAvailable()) {
        auto result = imgp::encode::png(rgba, width, height, true);
        if (result.isOk()) {
            outPngData = std::move(result.unwrap());
            return true;
        }
    }

    // fallback: stb_image_write
    outPngData.clear();
    outPngData.reserve(static_cast<size_t>(width) * height);
    int ok = stbi_write_png_to_func(stbiWriteToVector, &outPngData,
        static_cast<int>(width), static_cast<int>(height), 4, rgba,
        static_cast<int>(width) * 4);
    return ok != 0;
}

bool ImageConverter::saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath) {
    std::vector<uint8_t> pngData;
    if (!rgbaToPngBuffer(rgba, width, height, pngData)) {
        log::error("[ImageConverter] PNG encode failed");
        return false;
    }
    auto res = geode::utils::file::writeBinary(filePath, pngData);
    if (res.isErr()) {
        log::error("[ImageConverter] Cannot write PNG file: {}", res.unwrapErr());
        return false;
    }
    return true;
}

bool ImageConverter::rgbaToWebpBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality) {
    if (!rgba || width == 0 || height == 0) return false;

    if (imgp::isAvailable()) {
        auto result = imgp::encode::webp(rgba, width, height, true, quality);
        if (result.isOk()) {
            outData = std::move(result.unwrap());
            return true;
        }
    }

    log::warn("[ImageConverter] WebP encode not available (ImagePlus required)");
    return false;
}

bool ImageConverter::rgbaToJxlBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality) {
    if (!rgba || width == 0 || height == 0) return false;

    if (imgp::isAvailable()) {
        auto result = imgp::encode::jpegxl(rgba, width, height, true, quality);
        if (result.isOk()) {
            outData = std::move(result.unwrap());
            return true;
        }
    }

    log::warn("[ImageConverter] JPEG XL encode not available (ImagePlus required)");
    return false;
}

bool ImageConverter::rgbaToQoiBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData) {
    if (!rgba || width == 0 || height == 0) return false;

    if (imgp::isAvailable()) {
        auto result = imgp::encode::qoi(rgba, width, height, true);
        if (result.isOk()) {
            outData = std::move(result.unwrap());
            return true;
        }
    }

    log::warn("[ImageConverter] QOI encode not available (ImagePlus required)");
    return false;
}

bool ImageConverter::saveRGBAToWebP(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath, float quality) {
    std::vector<uint8_t> webpData;
    if (!rgbaToWebpBuffer(rgba, width, height, webpData, quality)) {
        log::error("[ImageConverter] WebP encode failed");
        return false;
    }
    auto res = geode::utils::file::writeBinary(filePath, webpData);
    if (res.isErr()) {
        log::error("[ImageConverter] Cannot write WebP file: {}", res.unwrapErr());
        return false;
    }
    return true;
}

bool ImageConverter::rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    bool isRgba = (rgbData.size() == static_cast<size_t>(width) * height * 4);
    
    if (isRgba) {
        return rgbaToPngBuffer(rgbData.data(), width, height, outPngData);
    }

    auto rgba = rgbToRgba(rgbData, width, height);
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
    
    // lee cabecera
    RGBHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || header.width == 0 || header.height == 0) {
        log::error("[ImageConverter] Invalid RGB header in file: {}", rgbFilePath);
        return false;
    }
    
    // lee datos rgb
    size_t rgbSize = static_cast<size_t>(header.width) * header.height * 3;
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
