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

class EmoteCache {
public:
    static EmoteCache& get() {
        static EmoteCache instance;
        return instance;
    }

    using TextureCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData)>;

    void loadEmote(EmoteInfo const& info, TextureCallback callback);

    bool isInRamCache(std::string const& name) const;

    void clearAll();

    void clearRam();

    using PreloadCallback = geode::CopyableFunction<void(size_t downloaded, size_t skipped, size_t total)>;

    using PreloadProgressCallback = geode::CopyableFunction<void(size_t completed, size_t total)>;

    void preloadAllToDisk(PreloadCallback callback = nullptr,
                          PreloadProgressCallback progressCallback = nullptr);

    void cancelPreload();

    void shutdown();

    size_t ramCacheBytes() const { return m_currentRamBytes; }
    size_t ramCacheCount() const;

private:
    EmoteCache() = default;
    ~EmoteCache() {
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

    struct RamEntry {
        geode::Ref<cocos2d::CCTexture2D> texture;
        std::vector<uint8_t> gifData;
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
    static constexpr size_t MAX_RAM_BYTES = 32 * 1024 * 1024;

    void addToRam(std::string const& name, RamEntry entry);
    void touchLru(std::string const& name);
    void evictRamIfNeeded();

    std::filesystem::path getDiskCacheDir() const;
    std::filesystem::path getDiskPath(std::string const& filename) const;
    bool loadFromDisk(std::string const& filename, std::vector<uint8_t>& outData) const;
    void saveToDisk(std::string const& filename, std::vector<uint8_t> const& data);
    bool isDiskEntryValid(std::string const& filename) const;

    static constexpr int64_t DISK_TTL_SECONDS = 30 * 24 * 60 * 60;

    std::atomic<bool> m_preloading{false};
    std::atomic<bool> m_preloadCancel{false};

    struct DecodeTask {
        EmoteInfo info;
        std::vector<uint8_t> data;
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
