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
        // lastAccess en microsegundos desde steady_clock epoch — atomic int64
        // garantiza lock-free en todas plataformas. Se actualiza en cada hit
        // sin necesidad de escalar el shared_lock a unique_lock.
        std::atomic<int64_t> lastAccessUs{0};
        std::chrono::steady_clock::time_point addedAt;  // when entry was first inserted (for purge grace)
        size_t byteSize = 0;
        int invalidationVersion = 0;
        int originalWidth = 0;   // dimensions before downsampling (0 = not downsampled)
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

        // unordered_map necesita move/copy; el atomic no es trivialmente copiable,
        // asi que definimos manualmente preservando solo el snapshot del valor.
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
    // Borra todas las URLs del cache cuyo path contenga el patron de un levelID
    // (ej. "/12345.", "/12345_"). Usado por ThumbnailLoader::invalidateLevel
    // para garantizar que las URLs viejas de gallery no persistan en RAM
    // tras un upload/reorder.
    void clearUrlsForLevel(int levelID);
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
    // Backoff escalonado: 15s → 30s → 60s → 300s (5 min max)
    // El primer paso ya no es 2s para evitar martillar el servidor cuando
    // muchos thumbnails fallan a la vez (rate-limit, timeout, etc.)
    static constexpr int FAILED_BACKOFF_STEPS[] = {15, 30, 60, 300};
    static constexpr int FAILED_BACKOFF_MAX_STEP = 3;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr size_t AGGREGATE_RAM_CAP = 80ull * 1024 * 1024;
#else
    static constexpr size_t AGGREGATE_RAM_CAP = 150ull * 1024 * 1024;
#endif

    static constexpr auto PURGE_INTERVAL = std::chrono::seconds(2);
    // Grace period: recently-added entries are immune to purge so that
    // pending callbacks have time to retain the texture before it is evicted.
    // Aumentado de 500ms a 2000ms para cubrir picos de stress: con
    // MAX_CALLBACKS_PER_FRAME=4 y 20+ celdas en cola, ademas de eventual
    // frame lag (que reduce el budget a 1 callback/frame), una textura
    // recien agregada al RAM cache puede tardar >500ms en aplicarse al
    // sprite. Con 500ms, el purge corria antes de que el callback drenara
    // y la textura se liberaba prematuramente. 2s da margen seguro sin
    // afectar la eviction LRU normal de entradas viejas.
    static constexpr auto PURGE_GRACE_PERIOD = std::chrono::milliseconds(2000);

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
    // Atomic time_point para evitar race en purgeUnusedTextures cuando
    // se llama desde el main thread y otros threads en paralelo.
    std::atomic<int64_t> m_lastPurgeUs{0};

    // ── Stats ──────────────────
    CacheStats m_stats;
};

} // namespace paimon::cache
