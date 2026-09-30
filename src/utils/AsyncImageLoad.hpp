#pragma once

// off-thread decode, sprite delivered on main thread. static images only;
// gif/apng go through animatedgifsprite.

#include <Geode/Geode.hpp>
#include "ImageLoadHelper.hpp"
#include "ThreadPool.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <filesystem>
#include <memory>
#include <limits>

namespace paimon::asyncimg {

using SpriteCallback = geode::CopyableFunction<void(cocos2d::CCSprite*)>;

namespace detail {
// shared lifetime pool (heap, no atexit destructor); 2 threads.
inline paimon::ThreadPool& pool() {
    static auto* p = new paimon::ThreadPool(2, "PaimonAsyncImg");
    return *p;
}
} // namespace detail

// autoreleased ccsprite* on main thread (nullptr on failure).
// caller guards its own lifetime in the callback.
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

        auto fileData = ImageLoadHelper::readBinaryFile(path, maxSizeMB);
        if (fileData.empty() || fileData.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            fail();
            return;
        }

        // cpu-only decode; no gl calls.
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(fileData.data(), static_cast<int>(fileData.size()), &w, &h, &channels)
            || w <= 0 || h <= 0 || w > ImageLoadHelper::kMaxImageDim || h > ImageLoadHelper::kMaxImageDim) {
            fail();
            return;
        }
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> px(
            stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()), &w, &h, &channels, 4),
            &stbi_image_free);
        if (!px || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            fail();
            return;
        }

        auto rgba = std::shared_ptr<unsigned char>(px.release(), &stbi_image_free);

        // gl texture creation must run on main thread.
        geode::Loader::get()->queueInMainThread(
            [rgba, w, h, callback = std::move(callback)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                cocos2d::CCSprite* sprite = nullptr;
                auto loaded = ImageLoadHelper::createFromRGBA(rgba.get(), w, h, false);
                if (loaded.success && loaded.texture) {
                    sprite = cocos2d::CCSprite::createWithTexture(loaded.texture);
                    loaded.texture->release(); // the sprite retains it
                }
                if (callback) callback(sprite);
            });
    });
}

} // namespace paimon::asyncimg
