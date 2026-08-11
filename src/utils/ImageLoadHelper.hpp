#pragma once

#include <Geode/Geode.hpp>
#include "FormatDetect.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include "ImageConverter.hpp"
#include "LocalAssetStore.hpp"

// stb_image handles formats outside CCImage's usual PNG/JPEG path.
#include "stb_image.h"

using cocos2d::CCTexture2D;
using cocos2d::CCSize;
using cocos2d::CCImage;
using cocos2d::ccTexParams;
using cocos2d::kCCTexture2DPixelFormat_RGBA8888;

    // Decoded texture plus optional retained RGBA data.
    namespace ImageLoadHelper {

    struct LoadedImage {
        CCTexture2D* texture = nullptr;     // Retained; caller releases.
        std::shared_ptr<uint8_t> buffer;    // RGBA data.
        int width = 0;
        int height = 0;
        bool success = false;
        std::string error;
    };

    // Reject dimensions above 4096² (64 MB RGBA).
    static constexpr int kMaxImageDim = 4096;

    inline LoadedImage createFromRGBA(uint8_t const* rgba, int w, int h, bool copyBuffer = true) {
        LoadedImage result;

        if (w <= 0 || h <= 0 || w > kMaxImageDim || h > kMaxImageDim) {
            result.error = "invalid_dimensions";
            return result;
        }

        size_t rgbaSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;

        auto* tex = new (std::nothrow) CCTexture2D();
        if (!tex) {
            geode::log::error("[ImageLoadHelper] bad_alloc creating {}x{} texture ({} bytes)", w, h, rgbaSize);
            result.error = "out_of_memory";
            return result;
        }

        if (!tex->initWithData(rgba, kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(w, h))) {
            tex->release();
            result.error = "texture_error";
            return result;
        }

        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        tex->setTexParameters(&params);

        if (copyBuffer) {
            auto* rawBuffer = new (std::nothrow) uint8_t[rgbaSize];
            if (!rawBuffer) {
                tex->release();
                geode::log::error("[ImageLoadHelper] bad_alloc creating {}x{} texture buffer ({} bytes)", w, h, rgbaSize);
                result.error = "out_of_memory";
                return result;
            }

            auto buffer = std::shared_ptr<uint8_t>(rawBuffer, std::default_delete<uint8_t[]>());
            memcpy(buffer.get(), rgba, rgbaSize);
            result.buffer = buffer;
        }

        result.texture = tex;
        result.width = w;
        result.height = h;
        result.success = true;
        return result;
    }

    // Decode common and exotic image formats from memory via stb_image.
    inline LoadedImage loadWithSTBFromMemory(uint8_t const* fileData, size_t fileSize, bool copyBuffer = true) {
        LoadedImage result;

        if (!fileData || fileSize == 0 || fileSize > static_cast<size_t>(std::numeric_limits<int>::max())) {
            result.error = "invalid_image_data";
            return result;
        }

        int w = 0, h = 0, channels = 0;
        int const dataSize = static_cast<int>(fileSize);
        if (!stbi_info_from_memory(fileData, dataSize, &w, &h, &channels)
            || w <= 0 || h <= 0 || w > kMaxImageDim || h > kMaxImageDim) {
            result.error = "invalid_image_data";
            return result;
        }

        unsigned char* data = stbi_load_from_memory(fileData, dataSize, &w, &h, &channels, 4);
        if (!data) {
            result.error = "image_open_error";
            return result;
        }

        result = createFromRGBA(data, w, h, copyBuffer);
        stbi_image_free(data);

        if (!result.success) {
            result.error = "texture_error";
        } else {
            geode::log::debug("[ImageLoadHelper] Loaded via stb_image (memory): {}x{} (original {} channels)", w, h, channels);
        }
        return result;
    }

    // Decode a file via stb_image.
    inline LoadedImage loadWithSTB(std::filesystem::path const& path) {
        LoadedImage result;

        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) {
            result.error = "image_open_error";
            return result;
        }
        auto& fileData = readRes.unwrap();

        if (fileData.empty()) {
            result.error = "image_open_error";
            return result;
        }

        return loadWithSTBFromMemory(fileData.data(), fileData.size());
    }

    // Decode a static image, trying stb_image before CCImage. maxSizeMB=0 disables
    // the file-size limit.
    inline LoadedImage loadStaticImage(std::filesystem::path const& path, size_t maxSizeMB = 10) {
        LoadedImage result;

        if (maxSizeMB > 0) {
            std::error_code ec;
            auto fileSize = std::filesystem::file_size(path, ec);
            if (ec) {
                result.error = "image_open_error";
                return result;
            }
            if (fileSize > maxSizeMB * 1024 * 1024) {
                result.error = fmt::format("Image too large (max {}MB)", maxSizeMB);
                return result;
            }
        }

        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) {
            result.error = "image_open_error";
            return result;
        }
        auto& fileData = readRes.unwrap();

        if (fileData.empty()) {
            result.error = "image_open_error";
            return result;
        }

        {
            auto stbResult = loadWithSTBFromMemory(fileData.data(), fileData.size());
            if (stbResult.success) return stbResult;
            if (stbResult.error == "invalid_image_data") return stbResult;
        }

        {
            CCImage img;
            if (img.initWithImageData(const_cast<uint8_t*>(fileData.data()), fileData.size())) {
                int w = img.getWidth();
                int h = img.getHeight();
                auto raw = img.getData();
                if (raw && w > 0 && h > 0 && w <= kMaxImageDim && h <= kMaxImageDim) {
                    int bpp = img.hasAlpha() ? 4 : 3;

                    size_t rgbaSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
                    std::vector<uint8_t> rgba(rgbaSize);
                    unsigned char const* src = reinterpret_cast<unsigned char const*>(raw);

                    if (bpp == 4) {
                        memcpy(rgba.data(), src, rgbaSize);
                    } else {
                        ImageConverter::rgbToRgbaFast(src, rgba.data(), static_cast<size_t>(w) * h);
                    }

                    return createFromRGBA(rgba.data(), w, h);
                }
            }
        }

        result.error = "image_open_error";
        return result;
    }

    // Read a binary file, returning empty on failure or size overflow.
    inline std::vector<uint8_t> readBinaryFile(std::filesystem::path const& path, size_t maxSizeMB = 10) {
        if (maxSizeMB > 0) {
            std::error_code ec;
            auto fileSize = std::filesystem::file_size(path, ec);
            if (ec || fileSize > maxSizeMB * 1024 * 1024) return {};
        }

        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) return {};

        auto& data = readRes.unwrap();
        if (maxSizeMB > 0 && data.size() > maxSizeMB * 1024 * 1024) {
            return {};
        }

        return std::move(data);
    }

    // Detect GIF/APNG from the first 64 bytes, then fall back to .gif.
    inline bool isGIF(std::filesystem::path const& path) {
        {
            std::ifstream file(path, std::ios::binary);
            if (file) {
                char buf[64];
                file.read(buf, sizeof(buf));
                auto n = static_cast<size_t>(file.gcount());
                if (n >= 6) {
                    if (paimon::format::isGif(reinterpret_cast<uint8_t const*>(buf), n)) return true;
                }
            }
        }
        std::string ext = geode::utils::string::pathToString(path.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".gif";
    }

    inline bool isAnimatedImage(std::filesystem::path const& path) {
        return isGIF(path);
    }

    // Load animated images through the callback, falling back to static decode.
    inline cocos2d::CCSprite* loadAnimatedOrStatic(
        std::filesystem::path const& path,
        size_t maxSizeMB,
        std::function<cocos2d::CCSprite*(std::string const&)> const& createAnimated
    ) {
        if (isAnimatedImage(path)) {
            auto pathStr = geode::utils::string::pathToString(path);
            auto* anim = createAnimated(pathStr);
            if (anim) return anim;
        }

        auto img = loadStaticImage(path, maxSizeMB);
        if (img.success && img.texture) {
            auto spr = cocos2d::CCSprite::createWithTexture(img.texture);
            img.texture->release(); // sprite retains it
            return spr;
        }
        return nullptr;
    }

    inline LoadedImage loadStaticImage(std::string_view pathStr, size_t maxSizeMB = 10) {
        return loadStaticImage(paimon::assets::pathFromUtf8(pathStr), maxSizeMB);
    }
    inline LoadedImage loadStaticImage(std::string const& pathStr, size_t maxSizeMB = 10) {
        return loadStaticImage(paimon::assets::pathFromUtf8(pathStr), maxSizeMB);
    }
    inline LoadedImage loadStaticImage(char const* pathStr, size_t maxSizeMB = 10) {
        return loadStaticImage(paimon::assets::pathFromUtf8(pathStr), maxSizeMB);
    }

    inline std::vector<uint8_t> readBinaryFile(std::string_view pathStr, size_t maxSizeMB = 10) {
        return readBinaryFile(paimon::assets::pathFromUtf8(pathStr), maxSizeMB);
    }
    inline std::vector<uint8_t> readBinaryFile(std::string const& pathStr, size_t maxSizeMB = 10) {
        return readBinaryFile(paimon::assets::pathFromUtf8(pathStr), maxSizeMB);
    }
    inline std::vector<uint8_t> readBinaryFile(char const* pathStr, size_t maxSizeMB = 10) {
        return readBinaryFile(paimon::assets::pathFromUtf8(pathStr), maxSizeMB);
    }

    inline bool isGIF(std::string_view pathStr) {
        return isGIF(paimon::assets::pathFromUtf8(pathStr));
    }
    inline bool isGIF(std::string const& pathStr) {
        return isGIF(paimon::assets::pathFromUtf8(pathStr));
    }
    inline bool isGIF(char const* pathStr) {
        return isGIF(paimon::assets::pathFromUtf8(pathStr));
    }

    inline bool isAnimatedImage(std::string_view pathStr) {
        return isAnimatedImage(paimon::assets::pathFromUtf8(pathStr));
    }
    inline bool isAnimatedImage(std::string const& pathStr) {
        return isAnimatedImage(paimon::assets::pathFromUtf8(pathStr));
    }
    inline bool isAnimatedImage(char const* pathStr) {
        return isAnimatedImage(paimon::assets::pathFromUtf8(pathStr));
    }

    inline cocos2d::CCSprite* loadAnimatedOrStatic(
        std::string_view pathStr,
        size_t maxSizeMB,
        std::function<cocos2d::CCSprite*(std::string const&)> const& createAnimated
    ) {
        return loadAnimatedOrStatic(paimon::assets::pathFromUtf8(pathStr), maxSizeMB, createAnimated);
    }
    inline cocos2d::CCSprite* loadAnimatedOrStatic(
        std::string const& pathStr,
        size_t maxSizeMB,
        std::function<cocos2d::CCSprite*(std::string const&)> const& createAnimated
    ) {
        return loadAnimatedOrStatic(paimon::assets::pathFromUtf8(pathStr), maxSizeMB, createAnimated);
    }
    inline cocos2d::CCSprite* loadAnimatedOrStatic(
        char const* pathStr,
        size_t maxSizeMB,
        std::function<cocos2d::CCSprite*(std::string const&)> const& createAnimated
    ) {
        return loadAnimatedOrStatic(paimon::assets::pathFromUtf8(pathStr), maxSizeMB, createAnimated);
    }

    // CPU bilinear fallback for cache downsampling when GPU rendering is unavailable.
    struct DownsampleResult {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
    };

    inline DownsampleResult downsampleForCache(
        uint8_t const* pixels, int width, int height, int maxDim
    ) {
        DownsampleResult result;
        if (!pixels || width <= 0 || height <= 0 || maxDim <= 0) return result;
        if (width <= maxDim && height <= maxDim) {
            result.width = width;
            result.height = height;
            result.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
            return result;
        }

        float scale = static_cast<float>(maxDim) / std::max(width, height);
        int newW = std::max(1, static_cast<int>(width * scale));
        int newH = std::max(1, static_cast<int>(height * scale));
        newW = (newW / 2) * 2;
        newH = (newH / 2) * 2;
        if (newW < 2) newW = 2;
        if (newH < 2) newH = 2;

        size_t outSize = static_cast<size_t>(newW) * newH * 4;
        result.pixels.resize(outSize);
        result.width = newW;
        result.height = newH;

        float xRatio = static_cast<float>(width) / newW;
        float yRatio = static_cast<float>(height) / newH;

        for (int y = 0; y < newH; ++y) {
            float srcY = y * yRatio;
            int y0 = std::min(static_cast<int>(srcY), height - 1);
            int y1 = std::min(y0 + 1, height - 1);
            float fy = srcY - y0;

            for (int x = 0; x < newW; ++x) {
                float srcX = x * xRatio;
                int x0 = std::min(static_cast<int>(srcX), width - 1);
                int x1 = std::min(x0 + 1, width - 1);
                float fx = srcX - x0;

                auto sample = [&](int sx, int sy) -> uint8_t const* {
                    return pixels + (static_cast<size_t>(sy) * width + sx) * 4;
                };

                uint8_t const* p00 = sample(x0, y0);
                uint8_t const* p10 = sample(x1, y0);
                uint8_t const* p01 = sample(x0, y1);
                uint8_t const* p11 = sample(x1, y1);

                size_t outIdx = (static_cast<size_t>(y) * newW + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    float top = p00[c] * (1.f - fx) + p10[c] * fx;
                    float bot = p01[c] * (1.f - fx) + p11[c] * fx;
                    result.pixels[outIdx + c] = static_cast<uint8_t>(top * (1.f - fy) + bot * fy + 0.5f);
                }
            }
        }

        return result;
    }
}
