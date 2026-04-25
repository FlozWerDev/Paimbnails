#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>
#include <Geode/Geode.hpp>

/**
 * Utility class for image format conversions.
 * Centralizes all RGB/RGBA/PNG conversion logic to avoid duplication.
 * All file I/O uses std::filesystem::path for proper Unicode support on Windows.
 */
class ImageConverter {
public:
    static std::vector<uint8_t> rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height);

    /**
     * Fast RGB24 → RGBA32 conversion. Writes directly into a pre-allocated buffer.
     * Uses batched alpha fill (memset) instead of per-pixel alpha assignment.
     * ~2-3x faster than the per-pixel loop for large images.
     *
     * @param rgb     Source RGB24 data (3 bytes per pixel)
     * @param rgbaOut Destination RGBA32 buffer (must be pixelCount*4 bytes)
     * @param pixelCount Number of pixels
     */
    static void rgbToRgbaFast(uint8_t const* rgb, uint8_t* rgbaOut, size_t pixelCount);

    /**
     * Fast RGBA32 → RGB24 conversion. Writes directly into a pre-allocated buffer.
     * Drops the alpha channel using auto-vectorized sequential writes.
     *
     * @param rgba    Source RGBA32 data (4 bytes per pixel)
     * @param rgbOut  Destination RGB24 buffer (must be pixelCount*3 bytes)
     * @param pixelCount Number of pixels
     */
    static void rgbaToRgbFast(uint8_t const* rgba, uint8_t* rgbOut, size_t pixelCount);
    
    /**
     * Convert RGB/RGBA data to PNG bytes in memory (no file I/O).
     */
    static bool rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);
    
    /**
     * Encode RGBA8888 buffer to PNG bytes in memory.
     */
    static bool rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);

    /**
     * Encode RGBA8888 buffer to WebP bytes in memory.
     * @param quality 0-100, default 75. Use 100 for lossless.
     */
    static bool rgbaToWebpBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality = 75.f);

    /**
     * Encode RGBA8888 buffer to JPEG XL bytes in memory.
     * @param quality 0-100, default 75.
     */
    static bool rgbaToJxlBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData, float quality = 75.f);

    /**
     * Encode RGBA8888 buffer to QOI bytes in memory (lossless, fast).
     */
    static bool rgbaToQoiBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outData);

    /**
     * Encode RGBA8888 buffer to PNG and write to file (Unicode-safe on Windows).
     */
    static bool saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath);

    /**
     * Encode RGBA8888 buffer to WebP and write to file (Unicode-safe on Windows).
     */
    static bool saveRGBAToWebP(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath, float quality = 75.f);

    static bool loadRgbFileToPng(std::string const& rgbFilePath, std::vector<uint8_t>& outPngData);
    static bool loadRgbFile(std::string const& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight);

private:
    struct RGBHeader {
        uint32_t width;
        uint32_t height;
    };
};

