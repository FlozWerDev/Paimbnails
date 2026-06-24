#pragma once
// Persistent on-disk cache for pre-computed blur textures (raw RGBA8888).

#include <Geode/utils/cocos.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace paimon::blur {

class BlurDiskCache {
public:
    static BlurDiskCache& get();

    using ReadyCallback = std::function<void(cocos2d::CCTexture2D* texture)>;

    /// Initialize the cache (creates the dir, reads the index). Safe to call repeatedly.
    void init();

    /// Look up an entry; onReady(nullptr) on miss.
    void lookupAsync(std::string const& key, ReadyCallback onReady);

    /// Synchronous index-only check (no I/O).
    bool hasEntry(std::string const& key) const;

    /// Persist a computed blur to disk.
    void storeAsync(std::string const& key, cocos2d::CCRenderTexture* rt);

    /// Store from a CCTexture2D (reads pixels via glReadPixels).
    void storeFromTextureAsync(std::string const& key, cocos2d::CCTexture2D* tex, int width, int height);

    /// Invalidate an entry.
    void invalidate(std::string const& key);

    /// Clear the entire blur cache.
    void clear();

    /// Stats.
    std::size_t diskEntryCount() const;
    std::size_t ramEntryCount() const;

    /// Ordered shutdown.
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

    bool loadIndex();
    bool writeIndex();

    cocos2d::CCTexture2D* uploadRawRGBA(std::vector<uint8_t> const& pixels, int w, int h);

    // Requires unique_lock on m_mutex. Evicts LRU entries when over the cap.
    void evictIndexIfNeededLocked();

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, IndexEntry> m_index;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<std::size_t> m_ramHits{0};
    std::atomic<std::size_t> m_diskHits{0};
    std::atomic<std::size_t> m_misses{0};
    std::atomic<std::size_t> m_stores{0};

    // Max on-disk cache size; oldest entries evicted when exceeded. Raw 512x288 blurs
    // are ~576KB each (256MB = ~450 entries).
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 64LL * 1024 * 1024;  // 64 MB
#else
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 256LL * 1024 * 1024; // 256 MB
#endif

    // Format: magic(4) + version(4) + width(4) + height(4) + reserved(4) + raw RGBA8888.
    static constexpr std::uint32_t MAGIC = 0x504C4255u; // 'PLBU' in LE = 'UBLP'
    static constexpr std::uint32_t VERSION = 1u;
    static constexpr std::size_t HEADER_SIZE = 20;
};

/// Build a canonical key for a blur parameter combination.
std::string makeKey(std::int64_t sourceID, int thumbIndex, char const* style,
                    int intensity, int width, int height);

} // namespace paimon::blur
