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
#include <cstdint>
#include <memory>

namespace paimon::blur {

class BlurDiskCache {
public:
    static BlurDiskCache& get();

    using ReadyCallback = std::function<void(cocos2d::CCTexture2D* texture)>;

    void init();
    void lookupAsync(std::string const& key, ReadyCallback onReady);
    bool hasEntry(std::string const& key) const;
    void storeAsync(std::string const& key, cocos2d::CCRenderTexture* rt);
    void storeFromTextureAsync(std::string const& key, cocos2d::CCTexture2D* tex, int width, int height);
    void invalidate(std::string const& key);
    void clear();
    std::size_t diskEntryCount() const;
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

    // Writes a finished RGBA blob to disk on the I/O pool and indexes it.
    void persistPixelsAsync(std::string key, std::shared_ptr<std::vector<uint8_t>> pixels, int w, int h);

    // Requires unique_lock on m_mutex.
    void evictIndexIfNeededLocked();

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, IndexEntry> m_index;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shuttingDown{false};

    // Raw 512x288 blurs are ~576KB each (256MB = ~450 entries).
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 64LL * 1024 * 1024;
#else
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 256LL * 1024 * 1024;
#endif

    // Format: magic(4) + version(4) + width(4) + height(4) + reserved(4) + raw RGBA8888.
    static constexpr std::uint32_t MAGIC = 0x504C4255u;
    static constexpr std::uint32_t VERSION = 1u;
    static constexpr std::size_t HEADER_SIZE = 20;
};

std::string makeKey(std::int64_t sourceID, int thumbIndex, char const* style,
                    int intensity, int width, int height);

} // namespace paimon::blur
