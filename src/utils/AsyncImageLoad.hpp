#pragma once

// off-thread decode, sprite delivered on main thread. static images only;
// gif/apng go through animatedgifsprite.

#include <Geode/Geode.hpp>
#include "ImageLoadHelper.hpp"
#include "ThreadPool.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>

namespace paimon::asyncimg {

using SpriteCallback = geode::CopyableFunction<void(cocos2d::CCSprite*)>;

namespace detail {
inline std::mutex& poolMutex() {
    static auto* m = new std::mutex();
    return *m;
}

inline std::atomic<paimon::ThreadPool*>& poolPtr() {
    static auto* p = new std::atomic<paimon::ThreadPool*>(nullptr);
    return *p;
}

// lazy heap pool (no atexit destructor); never allocates after shutdown begins.
inline paimon::ThreadPool* getOrCreatePool() {
    if (paimon::isRuntimeShuttingDown()) {
        return poolPtr().load(std::memory_order_acquire);
    }
    std::lock_guard<std::mutex> lock(poolMutex());
    if (paimon::isRuntimeShuttingDown()) {
        return poolPtr().load(std::memory_order_acquire);
    }
    auto* p = poolPtr().load(std::memory_order_relaxed);
    if (!p) {
        p = new paimon::ThreadPool(2, "PaimonAsyncImg");
        poolPtr().store(p, std::memory_order_release);
    }
    return p;
}
} // namespace detail

inline void requestShutdown() {
    if (auto* p = detail::poolPtr().load(std::memory_order_acquire)) {
        p->requestStop();
    }
}

inline void waitShutdown(std::chrono::steady_clock::time_point deadline) {
    if (auto* p = detail::poolPtr().load(std::memory_order_acquire)) {
        p->waitShutdown(deadline);
    }
}

// autoreleased ccsprite* on main thread (nullptr on failure).
// caller guards its own lifetime in the callback.
inline void loadStaticSprite(std::filesystem::path path, size_t maxSizeMB, SpriteCallback callback) {
    auto* p = detail::getOrCreatePool();
    if (paimon::isRuntimeShuttingDown() || !p || p->isStopped()) {
        if (callback) callback(nullptr);
        return;
    }

    p->enqueue([path, maxSizeMB, callback = std::move(callback)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;

        auto fail = [&callback]() {
            if (paimon::isRuntimeShuttingDown()) return;
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
        int w = 0, h = 0;
        auto px = ImageLoadHelper::decodeRGBAFromMemory(fileData.data(), fileData.size(), w, h);
        if (!px) {
            fail();
            return;
        }

        auto rgba = std::shared_ptr<unsigned char>(px.release(), &stbi_image_free);

        if (paimon::isRuntimeShuttingDown()) return;

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
