#pragma once

#include <Geode/utils/cocos.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace paimon::blur {

class BlurDiskCache {
public:
    static BlurDiskCache& get();
    static bool isInitialized();

    using ReadyCallback = std::function<void(cocos2d::CCTexture2D* texture)>;

    void init();
    void lookupAsync(std::string const& key, ReadyCallback onReady);
    bool hasEntry(std::string const& key) const;
    void storeFromTextureAsync(std::string const& key, cocos2d::CCTexture2D* tex, int width, int height);
    void clear();
    void requestShutdown();
    void waitShutdown(std::chrono::steady_clock::time_point deadline);
    void shutdown();

private:
    BlurDiskCache() = default;
    ~BlurDiskCache() = default;

    struct IndexEntry {
        int width = 0;
        int height = 0;
        std::int64_t mtimeEpoch = 0;
        std::int64_t byteSize = 0;
    };

    std::filesystem::path cacheDir() const;
    std::filesystem::path pathForKey(std::string const& key) const;

    cocos2d::CCTexture2D* uploadRawRGBA(std::vector<uint8_t> const& pixels, int w, int h);

    // validates a .pblur header; returns {width, height} or nullopt.
    static std::optional<std::pair<int, int>> parseHeaderDims(std::uint32_t const header[5]);

    // writes the finished rgba blob on the i/o pool and indexes it.
    void persistPixelsAsync(std::string key, std::shared_ptr<std::vector<uint8_t>> pixels, int w, int h);

    // requires unique_lock on m_mutex.
    void evictIndexIfNeededLocked();

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, IndexEntry> m_index;
    // running sum of IndexEntry::byteSize, kept in sync under m_mutex so
    // eviction never has to re-sum the whole index.
    std::int64_t m_totalBytes = 0;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shuttingDown{false};

    // persisted blur tiles are stored at 1x point resolution, not at the
    // game's (often 4x) content scale. a 569x320 tile is ~730kb on disk.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 64LL * 1024 * 1024;
#else
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 256LL * 1024 * 1024;
#endif

    // format: magic(4) + version(4) + width(4) + height(4) + reserved(4) + raw rgba8888.
    // v2 stores 1x point-space pixels; v1 files are discarded at startup.
    static constexpr std::uint32_t MAGIC = 0x504C4255u;
    static constexpr std::uint32_t VERSION = 2u;
    static constexpr std::size_t HEADER_SIZE = 20;
    static constexpr std::uint64_t MAX_PIXEL_COUNT = 16ull * 1024 * 1024;
};

} // namespace paimon::blur
