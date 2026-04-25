#pragma once

#include <Geode/Geode.hpp>
#include <prevter.imageplus/include/events.hpp>
#include <Geode/utils/file.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <functional>
#include <new>
#include "ImageConverter.hpp"

// stb_image: fallback para formatos exoticos (BMP, TGA, PSD, HDR)
#include "stb_image.h"

// Targeted type imports to avoid namespace pollution in headers
using cocos2d::CCTexture2D;
using cocos2d::CCSize;
using cocos2d::CCImage;
using cocos2d::ccTexParams;
using cocos2d::kCCTexture2DPixelFormat_RGBA8888;

/**
 * helper pa cargar imagen desde disco y prepararla pa CapturePreviewPopup.
 * elimina codigo duplicado entre processProfileImage y processProfileImg.
 */
namespace ImageLoadHelper {

    struct LoadedImage {
        CCTexture2D* texture = nullptr;     // textura cocos (retained, caller debe release)
        std::shared_ptr<uint8_t> buffer;    // buffer RGBA
        int width = 0;
        int height = 0;
        bool success = false;
        std::string error;
    };

    /**
     * Crea textura + buffer desde datos RGBA crudos.
     */
    // Max dimensions: 4096x4096 = 64MB RGBA. Anything larger risks OOM.
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

    /**
     * Fallback con stb_image desde memoria: decodifica JPEG, PNG, BMP, TGA, PSD, GIF, HDR, PIC.
     * Soporta mas formatos y perfiles de color que CCImage de cocos2d-x.
     * Intenta ImagePlus primero (PNG, WebP, GIF, QOI, JPEG XL), luego stb_image como fallback.
     */
    inline LoadedImage loadWithSTBFromMemory(uint8_t const* fileData, size_t fileSize, bool copyBuffer = true) {
        LoadedImage result;

        // === ImagePlus primero ===
        if (imgp::isAvailable()) {
            auto decResult = imgp::tryDecode(fileData, fileSize);
            if (decResult.isOk()) {
                auto& decoded = decResult.unwrap();
                if (auto* img = std::get_if<imgp::DecodedImage>(&decoded)) {
                    if (*img && img->width > 0 && img->height > 0
                        && img->width <= kMaxImageDim && img->height <= kMaxImageDim) {
                        result = createFromRGBA(img->data.get(), img->width, img->height, copyBuffer);
                        if (result.success) {
                            geode::log::info("[ImageLoadHelper] Loaded via ImagePlus (memory): {}x{}", img->width, img->height);
                            return result;
                        }
                    }
                }
                if (auto* anim = std::get_if<imgp::DecodedAnimation>(&decoded)) {
                    if (!anim->frames.empty() && anim->width > 0 && anim->height > 0
                        && anim->width <= kMaxImageDim && anim->height <= kMaxImageDim) {
                        result = createFromRGBA(anim->frames[0].data.get(), anim->width, anim->height, copyBuffer);
                        if (result.success) {
                            geode::log::info("[ImageLoadHelper] Loaded via ImagePlus animation first frame (memory): {}x{}", anim->width, anim->height);
                            return result;
                        }
                    }
                }
            }
        }

        // === Fallback: stb_image (BMP, TGA, PSD, HDR, JPEG especiales) ===
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load_from_memory(fileData, static_cast<int>(fileSize), &w, &h, &channels, 4);
        if (!data) {
            result.error = "image_open_error";
            return result;
        }

        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            stbi_image_free(data);
            result.error = "invalid_image_data";
            return result;
        }

        result = createFromRGBA(data, w, h, copyBuffer);
        stbi_image_free(data);

        if (!result.success) {
            result.error = "texture_error";
        } else {
            geode::log::info("[ImageLoadHelper] Loaded via stb_image (memory): {}x{} (original {} channels)", w, h, channels);
        }
        return result;
    }

    /**
     * Fallback con stb_image: decodifica JPEG, PNG, BMP, TGA, PSD, GIF, HDR, PIC.
     * Soporta mas formatos y perfiles de color que CCImage de cocos2d-x.
     * Intenta ImagePlus primero, luego stb_image como fallback.
     */
    inline LoadedImage loadWithSTB(std::filesystem::path const& path) {
        LoadedImage result;

        // leer archivo a memoria con Geode file utils
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

        // delegar a loadWithSTBFromMemory que ya tiene ImagePlus + stb fallback
        return loadWithSTBFromMemory(fileData.data(), fileData.size());
    }

    /**
     * carga imagen estatica (png/jpg/bmp/tga/psd/etc) desde path.
     * devuelve textura + buffer RGBA listo pa CapturePreviewPopup.
     *
     * Intenta en orden:
     * 1. ImagePlus (PNG, WebP, GIF first frame, QOI, JPEG XL)
     * 2. CCImage::initWithImageData (PNG, JPEG estandar)
     * 3. stb_image fallback (soporta BMP, TGA, PSD, JPEG CMYK, etc)
     *
     * @param path ruta al archivo
     * @param maxSizeMB tamano maximo en MB (0 = sin limite)
     * @return LoadedImage con los datos o error
     */
    inline LoadedImage loadStaticImage(std::filesystem::path const& path, size_t maxSizeMB = 10) {
        LoadedImage result;

        // verificar tamano
        if (maxSizeMB > 0) {
            std::error_code ec;
            auto fileSize = std::filesystem::file_size(path, ec);
            if (!ec && fileSize > maxSizeMB * 1024 * 1024) {
                result.error = fmt::format("Image too large (max {}MB)", maxSizeMB);
                return result;
            }
        }

        // leer archivo una sola vez a memoria
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

        // === Intento 1: ImagePlus (PNG, WebP, GIF, QOI, JPEG XL) ===
        if (imgp::isAvailable()) {
            auto decResult = imgp::tryDecode(fileData.data(), fileData.size());
            if (decResult.isOk()) {
                auto& decoded = decResult.unwrap();
                if (auto* img = std::get_if<imgp::DecodedImage>(&decoded)) {
                    if (*img && img->width > 0 && img->height > 0) {
                        auto loaded = createFromRGBA(img->data.get(), img->width, img->height);
                        if (loaded.success) return loaded;
                    }
                }
                if (auto* anim = std::get_if<imgp::DecodedAnimation>(&decoded)) {
                    if (!anim->frames.empty() && anim->width > 0 && anim->height > 0) {
                        auto loaded = createFromRGBA(anim->frames[0].data.get(), anim->width, anim->height);
                        if (loaded.success) return loaded;
                    }
                }
            }
        }

        // === Intento 2: CCImage::initWithImageData (PNG, JPEG estandar) ===
        {
            CCImage img;
            if (img.initWithImageData(const_cast<uint8_t*>(fileData.data()), fileData.size())) {
                int w = img.getWidth();
                int h = img.getHeight();
                auto raw = img.getData();
                if (raw && w > 0 && h > 0) {
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

        // === Intento 3: stb_image desde memoria (BMP, TGA, PSD, JPEG especiales, etc) ===
        {
            auto stbResult = loadWithSTBFromMemory(fileData.data(), fileData.size());
            if (stbResult.success) return stbResult;
        }

        result.error = "image_open_error";
        return result;
    }

    /**
     * lee archivo en binario (gif, png, etc).
     *
     * @param path ruta al archivo
     * @param maxSizeMB tamano maximo en MB
     * @return vector con datos o vacio si falla
     */
    inline std::vector<uint8_t> readBinaryFile(std::filesystem::path const& path, size_t maxSizeMB = 10) {
        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) return {};

        auto& data = readRes.unwrap();
        if (maxSizeMB > 0 && data.size() > maxSizeMB * 1024 * 1024) {
            return {};
        }

        return std::move(data);
    }

    /**
     * verifica si el archivo es GIF o APNG (deteccion por contenido, magic bytes)
     * fallback a extension si no se puede leer el archivo
     * Optimizado: lee solo los primeros 64 bytes en vez del archivo entero
     */
    inline bool isGIF(std::filesystem::path const& path) {
        // deteccion por contenido: leer solo los primeros 64 bytes
        {
            std::ifstream file(path, std::ios::binary);
            if (file) {
                char buf[64];
                file.read(buf, sizeof(buf));
                auto n = static_cast<size_t>(file.gcount());
                if (n >= 6) {
                    if (imgp::formats::isGif(buf, n)) return true;
                    if (imgp::formats::isAPng(buf, n)) return true;
                }
            }
        }
        // fallback por extension
        std::string ext = geode::utils::string::pathToString(path.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".gif";
    }

    /**
     * verifica si el archivo es una imagen animada (GIF, APNG).
     * Usa deteccion por contenido (magic bytes) para mayor precision.
     */
    inline bool isAnimatedImage(std::filesystem::path const& path) {
        return isGIF(path);
    }

    /**
     * Carga una imagen desde path, detectando automaticamente si es animada (GIF/APNG)
     * o estatica (PNG, JPEG, WebP, BMP, TGA, PSD, TIFF, QOI, JXL, etc).
     *
     * Usa un callback para crear el sprite animado, evitando dependencia circular
     * entre ImageLoadHelper y AnimatedGIFSprite.
     *
     * - Si es animada: llama a createAnimated(pathStr) y devuelve el resultado
     * - Si falla la carga animada o es estatica: usa loadStaticImage()
     *
     * Ejemplo de uso:
     *   auto* spr = ImageLoadHelper::loadAnimatedOrStatic(path, 10,
     *       [](std::string const& p) -> CCSprite* {
     *           return AnimatedGIFSprite::create(p);
     *       });
     *
     * @param path ruta al archivo
     * @param maxSizeMB tamano maximo en MB (0 = sin limite)
     * @param createAnimated callback que recibe el path string y devuelve CCSprite* (o nullptr)
     * @return CCSprite* o nullptr
     */
    inline cocos2d::CCSprite* loadAnimatedOrStatic(
        std::filesystem::path const& path,
        size_t maxSizeMB,
        std::function<cocos2d::CCSprite*(std::string const&)> const& createAnimated
    ) {
        // intentar carga como animacion (GIF/APNG)
        if (isAnimatedImage(path)) {
            auto pathStr = geode::utils::string::pathToString(path);
            auto* anim = createAnimated(pathStr);
            if (anim) return anim;
            // si falla la carga animada, caer al fallback estatico
        }

        // carga estatica
        auto img = loadStaticImage(path, maxSizeMB);
        if (img.success && img.texture) {
            auto spr = cocos2d::CCSprite::createWithTexture(img.texture);
            img.texture->release(); // sprite retains it
            return spr;
        }
        return nullptr;
    }

    /**
     * Downsample RGBA pixels using bilinear interpolation for cache storage.
     * NOTE: GPU-based downsampling via CCRenderTexture is preferred (faster, uses GPU linear filtering).
     * This CPU fallback is kept for contexts where GPU rendering is not available.
     *
     * @param pixels  RGBA8888 pixel data
     * @param width   original width
     * @param height  original height
     * @param maxDim  maximum dimension (width or height) for the output
     * @return        pair of {downsampled_pixels, {new_width, new_height}}
     */
    struct DownsampleResult {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
    };

    inline DownsampleResult downsampleForCache(
        uint8_t const* pixels, int width, int height, int maxDim
    ) {
        DownsampleResult result;
        // Skip if already small enough or invalid
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
        // Round to even dimensions for cleaner GPU alignment
        newW = (newW / 2) * 2;
        newH = (newH / 2) * 2;
        if (newW < 2) newW = 2;
        if (newH < 2) newH = 2;

        size_t outSize = static_cast<size_t>(newW) * newH * 4;
        result.pixels.resize(outSize);
        result.width = newW;
        result.height = newH;

        // Bilinear interpolation
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
