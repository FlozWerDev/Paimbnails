#pragma once

// ThumbnailCache.hpp — Sistema de cache unificado para thumbnails.
// Inspirado en https://github.com/cdc-sys/level-thumbs-mod
//
// Reemplaza: DiskManifest, m_textureCache/m_urlTextureCache embebidos
// en ThumbnailLoader, m_failedCache, m_diskCache legacy, LRU linked-lists.
//
// Diseño: shared_mutex para concurrencia read-heavy, mapas planos con
// timestamps para eviction (O(n) negligible para n<80), persistencia
// via Geode saved values ($on_mod(DataSaved)), orphan cleanup al arrancar.

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

    // ── RAM Cache: Level Thumbnails ─────────────────────────────

    struct RamEntry {
        geode::Ref<cocos2d::CCTexture2D> texture;
        std::chrono::steady_clock::time_point lastAccess;
        std::chrono::steady_clock::time_point addedAt;  // when entry was first inserted (for purge grace)
        size_t byteSize = 0;
        int invalidationVersion = 0;
        int originalWidth = 0;   // dimensions before downsampling (0 = not downsampled)
        int originalHeight = 0;
    };

    std::optional<geode::Ref<cocos2d::CCTexture2D>> getFromRam(int levelID, bool isGif);
    void addToRam(int levelID, bool isGif, cocos2d::CCTexture2D* texture, int version = -1, int origW = 0, int origH = 0);
    void removeFromRam(int levelID, bool isGif);
    void evictRamIfNeeded();
    // libera texturas con retainCount==1 (nadie las muestra)
    void purgeUnusedTextures();
    size_t ramBytes() const;
    size_t ramEntryCount() const;

    // ── RAM Cache: URL Gallery ──────────────────────────────────

    std::optional<geode::Ref<cocos2d::CCTexture2D>> getUrlFromRam(std::string const& url);
    void addUrlToRam(std::string const& url, cocos2d::CCTexture2D* texture);
    void removeUrlFromRam(std::string const& url);
    size_t urlRamBytes() const;
    size_t urlRamEntryCount() const;

    // ── Disk Index ──────────────────────────────────────────────

    // DiskEntry is now an alias for DiskManifestEntry (from CacheModels.hpp)
    // to maintain backward compatibility with existing ThumbnailLoader code
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

    // direct access to the underlying DiskManifest
    DiskManifest& diskManifest() { return m_manifest; }

    // ── Failed Cache (unified) ──────────────────────────────────

    bool isFailed(std::string const& key) const;
    void markFailed(std::string const& key);
    void clearFailed(std::string const& key);
    void clearAllFailed();
    // purga entradas expiradas del failed cache (llamar periodicamente)
    void purgeExpiredFailed();

    // ── Not-Found Cache (persistente por sesion) ───────────────
    // Thumbnails que confirmamos que NO existen en el servidor
    // (CDN + Worker ambos fallaron tras retry). No tiene TTL —
    // solo se limpia con clearNotFound() / invalidateLevel().
    bool isNotFound(std::string const& key) const;
    void markNotFound(std::string const& key);
    void clearNotFound(std::string const& key);
    void clearAllNotFound();

    // ── Invalidation ────────────────────────────────────────────

    int getInvalidationVersion(int levelID) const;
    void incrementInvalidation(int levelID);
    int getVersionForKey(int legacyKey) const;

    // ── Lifecycle ───────────────────────────────────────────────

    void clearRam();
    void clearDisk();   // remove files + clear index
    void clearAll();

    // safe destructor: take() textures without release() to avoid
    // crash during static destruction when Cocos2d is already dead
    void takeAllTextures();

    // ── Stats ───────────────────────────────────────────────────

    CacheStats& stats() { return m_stats; }
    CacheStats const& stats() const { return m_stats; }

    static constexpr size_t URL_CACHE_MAX_ENTRIES = 80;
    static constexpr size_t URL_CACHE_MAX_BYTES = 32ull * 1024 * 1024;

    static constexpr auto FAILED_CACHE_TTL = std::chrono::minutes(5);
    // Backoff escalonado: 2s → 4s → 8s → 300s (5 min max)
    // El ultimo paso es largo para evitar martillado tras errores persistentes,
    // pero no permanente: tras 5 min se permite reintento automatico.
    static constexpr int FAILED_BACKOFF_STEPS[] = {2, 4, 8, 300};
    static constexpr int FAILED_BACKOFF_MAX_STEP = 3;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr size_t AGGREGATE_RAM_CAP = 80ull * 1024 * 1024;
#else
    static constexpr size_t AGGREGATE_RAM_CAP = 150ull * 1024 * 1024;
#endif

    static constexpr auto PURGE_INTERVAL = std::chrono::seconds(2);
    // Grace period: recently-added entries are immune to purge so that
    // pending callbacks have time to retain the texture before it is evicted.
    static constexpr auto PURGE_GRACE_PERIOD = std::chrono::milliseconds(500);

    // Not-found entries persist for the entire session to avoid re-requesting
    // thumbnails that don't exist.  Manual refresh clears these entries.
    static constexpr auto NOT_FOUND_TTL = std::chrono::hours(24 * 365);

private:
    ThumbnailCache() = default;
    ~ThumbnailCache();

    static size_t estimateTextureBytes(cocos2d::CCTexture2D* tex);
    static std::string makeRamKey(int levelID, bool isGif);
    static std::string makeDiskKey(int levelID, bool isGif);
    static int64_t nowEpoch();

    // eviction helpers
    void evictRamLocked();
    void evictUrlRamLocked();

    // ── RAM level cache ────────
    std::unordered_map<std::string, RamEntry> m_ramCache;
    mutable std::shared_mutex m_ramMutex;
    size_t m_ramBytes = 0;

    // ── RAM URL cache ──────────
    std::unordered_map<std::string, RamEntry> m_urlRamCache;
    mutable std::shared_mutex m_urlMutex;
    size_t m_urlBytes = 0;

    // ── Disk index (backed by DiskManifest — standalone manifest.json) ──
    DiskManifest m_manifest;

    // ── Failed cache ───────────
    struct FailedEntry {
        std::chrono::steady_clock::time_point timestamp;
        int retryStep = 0; // indice en FAILED_BACKOFF_STEPS
    };
    mutable std::unordered_map<std::string, FailedEntry> m_failedCache;
    mutable std::mutex m_failedMutex;

    // ── Not-found cache (with TTL) ──────────────────────────
    mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_notFoundCache;
    mutable std::mutex m_notFoundMutex;

    // ── Invalidation ───────────
    std::unordered_map<int, int> m_invalidationVersions;
    mutable std::mutex m_invalidationMutex;

    // ── Purge throttle ─────────
    std::chrono::steady_clock::time_point m_lastPurge = std::chrono::steady_clock::time_point::min();

    // ── Stats ──────────────────
    CacheStats m_stats;
};

} // namespace paimon::cache
