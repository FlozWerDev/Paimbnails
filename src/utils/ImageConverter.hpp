#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>
#include <Geode/Geode.hpp>

// Image conversion helpers; file I/O uses Unicode-safe filesystem paths.
class ImageConverter {
public:
    static std::vector<uint8_t> rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height);

    // RGB24 → RGBA32, writing into a pre-allocated pixelCount*4 buffer.
    static void rgbToRgbaFast(uint8_t const* rgb, uint8_t* rgbaOut, size_t pixelCount);

    // RGBA32 → RGB24, writing into a pre-allocated pixelCount*3 buffer.
    static void rgbaToRgbFast(uint8_t const* rgba, uint8_t* rgbOut, size_t pixelCount);
    
    static bool rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);
    
    static bool rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);

    // quality is 0..100; 100 is lossless.
    static bool rgbaToWebpBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality = 75.f);

    static bool rgbaToJxlBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality = 75.f);

    static bool rgbaToQoiBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData);

    static bool saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath);

    static bool saveRGBAToWebP(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath, float quality = 75.f);

    static bool loadRgbFileToPng(std::string const& rgbFilePath, std::vector<uint8_t>& outPngData);
    static bool loadRgbFile(std::string const& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight);

private:
    struct RGBHeader {
        uint32_t width;
        uint32_t height;
    };
};
