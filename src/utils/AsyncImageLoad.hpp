#pragma once

// AsyncImageLoad — decodes static images off the main thread: heavy CPU work
// (read + decode to RGBA) runs on a ThreadPool, only the GL texture creation
// returns to the main thread. The callback gets an autoreleased CCSprite* or
// nullptr. Use AnimatedGIFSprite directly for GIF/APNG; this is for static
// images only (PNG/JPEG/BMP/TGA/WebP via stb).

#include <Geode/Geode.hpp>
#include "ImageLoadHelper.hpp"
#include "ThreadPool.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <filesystem>
#include <memory>
#include <vector>

namespace paimon::asyncimg {

using SpriteCallback = geode::CopyableFunction<void(cocos2d::CCSprite*)>;

namespace detail {
// Shared lifetime pool (heap, no atexit destructor); 2 threads decode previews
// without saturating I/O.
inline paimon::ThreadPool& pool() {
    static auto* p = new paimon::ThreadPool(2, "PaimonAsyncImg");
    return *p;
}
} // namespace detail

// Decode a static image off-thread and deliver an autoreleased CCSprite* on the
// main thread (nullptr on failure). The caller must guard its own lifetime in
// the callback (Ref/WeakRef/generation guard).
inline void loadStaticSprite(std::filesystem::path path, size_t maxSizeMB, SpriteCallback callback) {
    if (paimon::isRuntimeShuttingDown()) {
        if (callback) callback(nullptr);
        return;
    }

    detail::pool().enqueue([path, maxSizeMB, callback = std::move(callback)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;

        auto fail = [&callback]() {
            geode::Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                if (callback) callback(nullptr);
            });
        };

        // file size check
        if (maxSizeMB > 0) {
            std::error_code ec;
            auto fileSize = std::filesystem::file_size(path, ec);
            if (!ec && fileSize > maxSizeMB * 1024ull * 1024ull) { fail(); return; }
        }

        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) { fail(); return; }
        auto& fileData = readRes.unwrap();
        if (fileData.empty()) { fail(); return; }

// CPU-only decode; no GL calls.
        int w = 0, h = 0, channels = 0;
        unsigned char* px = stbi_load_from_memory(
            fileData.data(), static_cast<int>(fileData.size()), &w, &h, &channels, 4);
        if (!px || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            if (px) stbi_image_free(px);
            fail();
            return;
        }

        auto rgba = std::make_shared<std::vector<uint8_t>>(
            px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        stbi_image_free(px);

// GL texture creation on the main thread.
        geode::Loader::get()->queueInMainThread(
            [rgba, w, h, callback = std::move(callback)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                cocos2d::CCSprite* sprite = nullptr;
                auto loaded = ImageLoadHelper::createFromRGBA(rgba->data(), w, h, /*copyBuffer*/ false);
                if (loaded.success && loaded.texture) {
                    sprite = cocos2d::CCSprite::createWithTexture(loaded.texture);
                    loaded.texture->release(); // the sprite retains it
                }
                if (callback) callback(sprite);
            });
    });
}

inline void shutdownPool() {
    detail::pool().shutdown();
}

} // namespace paimon::asyncimg
