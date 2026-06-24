#pragma once

#include <Geode/Geode.hpp>
#include "../models/EmoteModels.hpp"
#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <vector>
#include <filesystem>
#include <chrono>
#include <atomic>

namespace paimon::emotes {

// Two-tier cache (RAM + disk) for emote image data.
// Static emotes are stored as CCTexture2D*, GIF emotes as raw bytes
// (AnimatedGIFSprite creates its own textures from data).
class EmoteCache {
public:
    static EmoteCache& get() {
        static EmoteCache instance;
        return instance;
    }

    using TextureCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData)>;

    // Load an emote texture/data. Checks RAM → disk → network.
    // Callback is expected to be delivered on the main thread and uses:
    //   - (tex, false, {}) for static emotes
    //   - (nullptr, true, gifData) for GIF emotes
    //   - (nullptr, false, {}) on failure
    void loadEmote(EmoteInfo const& info, TextureCallback callback);

    // Check if an emote's texture/data is in RAM cache.
    bool isInRamCache(std::string const& name) const;

    // Clear all caches (RAM + disk)
    void clearAll();

    // Clear only RAM cache (keeps disk cache intact)
    void clearRam();

    // Callback fired when preload finishes: (downloaded, skippedCached, totalEmotes)
    // The callback should be treated as a main-thread completion callback.
    using PreloadCallback = geode::CopyableFunction<void(size_t downloaded, size_t skipped, size_t total)>;

    // Per-step progress callback, fired each time an emote either finishes
    // downloading or is skipped (because it was already cached). Useful for
    // driving an X/Y label in the LoadingLayer (Globed-style).
    // Args: (completed = downloaded + skipped, total = catalog size).
    // Always dispatched on the main thread.
    using PreloadProgressCallback = geode::CopyableFunction<void(size_t completed, size_t total)>;

    // Preload all emote images to disk in background (progressive, async).
    // Only downloads emotes not already cached on disk.
    // Completion callback is intended to run on the main thread.
    //
    // If `progressCallback` is set, it is invoked once per emote (downloaded
    // OR skipped) with the running totals so the UI can update a progress
    // label without polling.
    void preloadAllToDisk(PreloadCallback callback = nullptr,
                          PreloadProgressCallback progressCallback = nullptr);

    // Cancel any in-progress preload.
    void cancelPreload();

    // Stop decode workers and preload during game exit (before GL teardown).
    void shutdown();

    // Stats
    size_t ramCacheBytes() const { return m_currentRamBytes; }
    size_t ramCacheCount() const;

private:
    EmoteCache() = default;
    ~EmoteCache() {
        // Stop the decode workers cleanly so the threads don't outlive us.
        shutdownDecodeWorker();

        // Detach textures without release() to avoid crashing when CCPoolManager is already dead.
        std::lock_guard lock(m_ramMutex);
        for (auto& [_, entry] : m_ramCache) {
            if (entry.texture) {
                (void)entry.texture.take();
            }
        }
    }
    EmoteCache(EmoteCache const&) = delete;
    EmoteCache& operator=(EmoteCache const&) = delete;

    // RAM cache entry
    struct RamEntry {
        geode::Ref<cocos2d::CCTexture2D> texture; // for static emotes
        std::vector<uint8_t> gifData;              // for gif emotes (raw bytes)
        EmoteType type = EmoteType::Static;
        size_t byteSize = 0;
        std::chrono::steady_clock::time_point cachedAt;
    };

    mutable std::mutex m_ramMutex;
    std::unordered_map<std::string, RamEntry> m_ramCache;
    std::list<std::string> m_lruOrder;
    std::unordered_map<std::string, std::list<std::string>::iterator> m_lruMap;
    size_t m_currentRamBytes = 0;

    static constexpr size_t MAX_RAM_ENTRIES = 100;
    static constexpr size_t MAX_RAM_BYTES = 32 * 1024 * 1024; // 32 MB

    void addToRam(std::string const& name, RamEntry entry);
    void touchLru(std::string const& name);
    void evictRamIfNeeded();

    // Disk cache
    std::filesystem::path getDiskCacheDir() const;
    std::filesystem::path getDiskPath(std::string const& filename) const;
    bool loadFromDisk(std::string const& filename, std::vector<uint8_t>& outData) const;
    void saveToDisk(std::string const& filename, std::vector<uint8_t> const& data);
    bool isDiskEntryValid(std::string const& filename) const;

    static constexpr int64_t DISK_TTL_SECONDS = 30 * 24 * 60 * 60; // 30 days

    // Background preload state
    std::atomic<bool> m_preloading{false};
    std::atomic<bool> m_preloadCancel{false};

    // Decode worker pool: decode off-thread, upload to GL on the main thread.
    struct DecodeTask {
        EmoteInfo info;
        std::vector<uint8_t> data;       // raw file bytes
        TextureCallback callback;
    };

    void initDecodeWorker();
    void shutdownDecodeWorker();
    void enqueueDecode(DecodeTask task);
    static void decodeWorkerLoop(EmoteCache* self);
    void finalizeStaticDecodeOnMainThread(EmoteInfo info,
                                          std::vector<uint8_t> rgba,
                                          int width, int height,
                                          size_t originalByteSize,
                                          TextureCallback callback);

    std::deque<DecodeTask> m_decodeQueue;
    std::mutex m_decodeMutex;
    std::condition_variable m_decodeCV;
    std::vector<std::thread> m_decodeWorkers;
    std::atomic<bool> m_decodeRunning{false};
    std::atomic<bool> m_decodeShutdown{false};
};

} // namespace paimon::emotes
