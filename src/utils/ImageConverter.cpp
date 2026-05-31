#include "ImageConverter.hpp"
#include <Geode/Geode.hpp>

#include <Geode/utils/file.hpp>
#include <cstring>
#include <filesystem>

// stb_image_write para encodificado nativo (PNG, BMP, TGA, JPG)
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
    if (pixelCount == 0) return;

    // Un solo loop con store de 32 bits por pixel:
    //  - evita la doble pasada sobre el buffer (alpha fill + interleave)
    //    que el codigo anterior hacia y que dominaba el cache thrashing
    //    para imagenes grandes (~8MB por cada pasada en 1920x1080).
    //  - clang auto-vectoriza este patron a SSE2 con pshufb-like shuffles
    //    en x64, manteniendo el fast path sin intrinsics explicitos
    //    (evita requerir ssse3 globalmente en el project).
    //  - la escritura secuencial en bloques de 4 bytes es cache-friendly
    //    y coincide con el layout que espera CCTexture2D::initWithData.
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
    // Drop alpha channel — auto-vectorized by compiler en los casos comunes.
    // Mantener un solo loop con escrituras secuenciales para no forzar otra pasada.
    for (size_t i = 0; i < pixelCount; ++i) {
        rgbOut[i * 3 + 0] = rgba[i * 4 + 0];
        rgbOut[i * 3 + 1] = rgba[i * 4 + 1];
        rgbOut[i * 3 + 2] = rgba[i * 4 + 2];
    }
}

bool ImageConverter::rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    if (!rgba || width == 0 || height == 0) return false;

    // stb_image_write directo — sin IPC ni dependencias externas
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

    // WebP encoding requiere ImagePlus (opcional) — no disponible nativamente
    log::warn("[ImageConverter] WebP encode not available (requires ImagePlus mod)");
    return false;
}

bool ImageConverter::rgbaToJxlBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality) {
    if (!rgba || width == 0 || height == 0) return false;

    // JPEG XL encoding requiere ImagePlus (opcional) — no disponible nativamente
    log::warn("[ImageConverter] JPEG XL encode not available (requires ImagePlus mod)");
    return false;
}

bool ImageConverter::rgbaToQoiBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData) {
    if (!rgba || width == 0 || height == 0) return false;

    // QOI encoding requiere ImagePlus (opcional) — no disponible nativamente
    log::warn("[ImageConverter] QOI encode not available (requires ImagePlus mod)");
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
