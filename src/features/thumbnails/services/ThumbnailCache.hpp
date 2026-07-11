#pragma once

#include <Geode/Geode.hpp>
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <optional>
#include <chrono>
#include <atomic>
#include <filesystem>
#include "CacheModels.hpp"
#include "DiskManifest.hpp"

namespace paimon::cache {

class ThumbnailCache {
public:
    static ThumbnailCache& get();
    static bool isAlive();

    struct RamEntry {
        geode::Ref<cocos2d::CCTexture2D> texture;
        std::atomic<int64_t> lastAccessUs{0};
        std::chrono::steady_clock::time_point addedAt;
        size_t byteSize = 0;
        int invalidationVersion = 0;
        int originalWidth = 0;
        int originalHeight = 0;

        static int64_t toUs(std::chrono::steady_clock::time_point tp) {
            return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
        }
        std::chrono::steady_clock::time_point lastAccess() const {
            return std::chrono::steady_clock::time_point(
                std::chrono::microseconds(lastAccessUs.load(std::memory_order_relaxed)));
        }
        void touchAccess(std::chrono::steady_clock::time_point tp) {
            lastAccessUs.store(toUs(tp), std::memory_order_relaxed);
        }

        RamEntry() = default;
        RamEntry(cocos2d::CCTexture2D* tex, std::chrono::steady_clock::time_point la,
                 std::chrono::steady_clock::time_point added, size_t bytes,
                 int ver, int oW = 0, int oH = 0)
            : texture(tex), lastAccessUs(toUs(la)), addedAt(added), byteSize(bytes),
              invalidationVersion(ver), originalWidth(oW), originalHeight(oH) {}

        // atomic no es trivialmente copiable: copy/move manuales preservan el snapshot del valor.
        RamEntry(RamEntry const& o)
            : texture(o.texture), lastAccessUs(o.lastAccessUs.load(std::memory_order_relaxed)),
              addedAt(o.addedAt), byteSize(o.byteSize),
              invalidationVersion(o.invalidationVersion),
              originalWidth(o.originalWidth), originalHeight(o.originalHeight) {}
        RamEntry& operator=(RamEntry const& o) {
            texture = o.texture;
            lastAccessUs.store(o.lastAccessUs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            addedAt = o.addedAt;
            byteSize = o.byteSize;
            invalidationVersion = o.invalidationVersion;
            originalWidth = o.originalWidth;
            originalHeight = o.originalHeight;
            return *this;
        }
        RamEntry(RamEntry&& o) noexcept
            : texture(std::move(o.texture)),
              lastAccessUs(o.lastAccessUs.load(std::memory_order_relaxed)),
              addedAt(o.addedAt), byteSize(o.byteSize),
              invalidationVersion(o.invalidationVersion),
              originalWidth(o.originalWidth), originalHeight(o.originalHeight) {}
        RamEntry& operator=(RamEntry&& o) noexcept {
            texture = std::move(o.texture);
            lastAccessUs.store(o.lastAccessUs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            addedAt = o.addedAt;
            byteSize = o.byteSize;
            invalidationVersion = o.invalidationVersion;
            originalWidth = o.originalWidth;
            originalHeight = o.originalHeight;
            return *this;
        }
    };

    std::optional<geode::Ref<cocos2d::CCTexture2D>> getFromRam(int levelID, bool isGif);
    // Fast presence check: level RAM map only (no URL fallback, no access touch).
    // Not equivalent to ThumbnailLoader::isLoaded — that also treats a default-URL
    // hit in the URL RAM cache as loaded for static thumbs.
    bool hasInRam(int levelID, bool isGif) const;
    bool isRamEntrySuitable(int levelID, bool isGif, int requestedMaxDim) const;
    void addToRam(int levelID, bool isGif, cocos2d::CCTexture2D* texture, int version = -1, int origW = 0, int origH = 0);
    void removeFromRam(int levelID, bool isGif);
    void evictRamIfNeeded();
    // libera texturas con retainCount==1 (nadie las muestra)
    void purgeUnusedTextures();
    size_t ramBytes() const;
    size_t ramEntryCount() const;

    std::optional<geode::Ref<cocos2d::CCTexture2D>> getUrlFromRam(std::string const& url);
    void addUrlToRam(std::string const& url, cocos2d::CCTexture2D* texture);
    void removeUrlFromRam(std::string const& url);
    void clearUrlsForLevel(int levelID);
    size_t urlRamBytes() const;
    size_t urlRamEntryCount() const;

    using DiskEntry = DiskManifestEntry;

    bool hasDiskEntry(int levelID, bool isGif) const;
    std::optional<DiskEntry> getDiskEntry(int levelID, bool isGif) const;
    void upsertDisk(DiskEntry entry);
    void removeDisk(int levelID, bool isGif);
    void touchDiskAccess(int levelID, bool isGif);
    void evictDiskIfNeeded(size_t maxBytes, std::chrono::hours maxAge);
    size_t diskTotalBytes() const;
    size_t diskEntryCount() const;

    void loadDiskIndex();
    void saveDiskIndex(bool allowDuringShutdown = false);

    DiskManifest& diskManifest() { return m_manifest; }

    bool isFailed(std::string const& key) const;
    void markFailed(std::string const& key);
    void clearFailed(std::string const& key);
    void clearAllFailed();
    void purgeExpiredFailed();

    // Thumbnails confirmados inexistentes en el servidor; sin TTL practico, solo se limpia con clearNotFound() / invalidateLevel().
    bool isNotFound(std::string const& key) const;
    void markNotFound(std::string const& key);
    void clearNotFound(std::string const& key);
    void clearAllNotFound();

    int getInvalidationVersion(int levelID) const;
    void incrementInvalidation(int levelID);
    int getVersionForKey(int legacyKey) const;

    void clearRam();
    void clearDisk();
    void clearAll();

    // safe destructor: take() texturas sin release() para evitar crash durante static destruction.
    void takeAllTextures();

    CacheStats& stats() { return m_stats; }
    CacheStats const& stats() const { return m_stats; }

    static constexpr size_t URL_CACHE_MAX_ENTRIES = 80;
    static constexpr size_t URL_CACHE_MAX_BYTES = 32ull * 1024 * 1024;

    static constexpr auto FAILED_CACHE_TTL = std::chrono::minutes(5);
    // Backoff escalonado: 15s → 30s → 60s → 300s. El primer paso no es 2s para no martillar el servidor en fallos masivos.
    static constexpr int FAILED_BACKOFF_STEPS[] = {15, 30, 60, 300};
    static constexpr int FAILED_BACKOFF_MAX_STEP = 3;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr size_t AGGREGATE_RAM_CAP = 80ull * 1024 * 1024;
#else
    static constexpr size_t AGGREGATE_RAM_CAP = 150ull * 1024 * 1024;
#endif

    static constexpr auto PURGE_INTERVAL = std::chrono::seconds(2);
    // Grace period: entradas recientes inmunes al purge para que callbacks pendientes retengan la textura. 2s cubre picos de stress con budget de callbacks reducido.
    static constexpr auto PURGE_GRACE_PERIOD = std::chrono::milliseconds(2000);

    static constexpr auto NOT_FOUND_TTL = std::chrono::hours(24 * 365);

private:
    ThumbnailCache() = default;
    ~ThumbnailCache();

    static size_t estimateTextureBytes(cocos2d::CCTexture2D* tex);
    // Integer RAM key: levelID for static, -levelID for GIF (no string alloc on hit path).
    static int makeRamKey(int levelID, bool isGif);
    static int64_t nowEpoch();

    void evictRamLocked();
    void evictUrlRamLocked();

    std::unordered_map<int, RamEntry> m_ramCache;
    mutable std::shared_mutex m_ramMutex;
    size_t m_ramBytes = 0;

    std::unordered_map<std::string, RamEntry> m_urlRamCache;
    mutable std::shared_mutex m_urlMutex;
    size_t m_urlBytes = 0;

    DiskManifest m_manifest;

    struct FailedEntry {
        std::chrono::steady_clock::time_point timestamp;
        int retryStep = 0;
    };
    mutable std::unordered_map<std::string, FailedEntry> m_failedCache;
    mutable std::mutex m_failedMutex;

    mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_notFoundCache;
    mutable std::mutex m_notFoundMutex;

    std::unordered_map<int, int> m_invalidationVersions;
    mutable std::mutex m_invalidationMutex;

    std::atomic<int64_t> m_lastPurgeUs{0};

    CacheStats m_stats;
};

} // namespace paimon::cache
