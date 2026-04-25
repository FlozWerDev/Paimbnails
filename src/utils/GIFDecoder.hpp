#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>

/**
 * Simple GIF decoder for extracting frames.
 * Supports basic animated GIFs without complex LZW compression.
 */
class GIFDecoder {
public:
    struct Frame {
        int left;
        int top;
        int width;
        int height;
        int delayMs; // Frame duration in milliseconds
        std::vector<uint8_t> pixels; // RGBA data
    };

    struct GIFData {
        std::vector<Frame> frames;
        int width;
        int height;
        bool isAnimated;
    };

    /**
     * Decodes a GIF from in-memory data.
     * @param data GIF data
     * @param size Data size in bytes
     * @return GIFData with all frames, or an empty structure on failure
     */
    static GIFData decode(uint8_t const* data, size_t size);

    /**
     * Verifies whether the data is a valid GIF.
     */
    static bool isGIF(uint8_t const* data, size_t size);

    /**
     * Obtiene las dimensiones del GIF sin decodificarlo completo
     */
    static bool getDimensions(uint8_t const* data, size_t size, int& width, int& height);

private:
    // Internal decoding helpers
    static bool parseHeader(uint8_t const*& ptr, uint8_t const* end, int& width, int& height);
    static bool parseColorTable(uint8_t const*& ptr, uint8_t const* end, std::vector<uint8_t>& palette, int size);
    struct RawFrame {
        std::vector<uint8_t> pixels;
        int width, height, left, top;
    };
    
    static bool parseFrame(uint8_t const*& ptr, uint8_t const* end, RawFrame& frame, std::vector<uint8_t> const& globalPalette, int transparentIndex, bool hasTransparency);
};

