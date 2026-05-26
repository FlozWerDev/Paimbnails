#include "ThumbnailCache.hpp"
#include "ThumbnailTransportClient.hpp"
#include "ThumbnailLoader.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../core/MainLevels.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/Debug.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <fstream>
#include <algorithm>
#include <ranges>
#include <atomic>
#include <cctype>

using namespace geode::prelude;

namespace paimon::cache {

static std::atomic<bool> s_cacheInstanceAlive{false};

// ── Helpers ─────────────────────────────────────────────────────────

ThumbnailCache& ThumbnailCache::get() {
    static ThumbnailCache instance;
    s_cacheInstanceAlive = true;
    return instance;
}

bool ThumbnailCache::isAlive() {
    return s_cacheInstanceAlive;
}

ThumbnailCache::~ThumbnailCache() {
    // Defensive: detach any remaining textures without calling release()
    // to avoid crash when CCPoolManager is already dead during static destruction.
    takeAllTextures();
    s_cacheInstanceAlive = false;
}

size_t ThumbnailCache::estimateTextureBytes(cocos2d::CCTexture2D* tex) {
    if (!tex) return 0;
    return static_cast<size_t>(tex->getPixelsWide()) * static_cast<size_t>(tex->getPixelsHigh()) * 4;
}

std::string ThumbnailCache::makeRamKey(int levelID, bool isGif) {
    return isGif ? ("-" + std::to_string(levelID)) : std::to_string(levelID);
}

std::string ThumbnailCache::makeDiskKey(int levelID, bool isGif) {
    return isGif ? ("-" + std::to_string(levelID)) : std::to_string(levelID);
}

int64_t ThumbnailCache::nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// ── RAM Cache: Level Thumbnails ─────────────────────────────────────

std::optional<geode::Ref<cocos2d::CCTexture2D>> ThumbnailCache::getFromRam(int levelID, bool isGif) {
    auto key = makeRamKey(levelID, isGif);
    
    {
        // Shared lock for read — allows concurrent lookups without serialization.
        // lastAccess es atomic, asi que se puede tocar sin escalar a unique_lock.
        std::shared_lock slock(m_ramMutex);
        auto it = m_ramCache.find(key);
        if (it != m_ramCache.end()) {
            it->second.touchAccess(std::chrono::steady_clock::now());
            return it->second.texture;
        }
    } // slock se libera automáticamente aquí
    
    // Puente de caché (Cache-bridging): buscar textura en caché de URLs como fallback
    if (!isGif) {
        std::string defaultUrl = ThumbnailTransportClient::get().getThumbnailURL(levelID);
        if (!defaultUrl.empty()) {
            std::string cacheKey = ThumbnailLoader::normalizeUrlKey(defaultUrl);
            auto urlTex = getUrlFromRam(cacheKey);
            if (urlTex.has_value()) {
                // Poblar caché principal para agilizar futuros accesos
                addToRam(levelID, isGif, urlTex.value().data());
                return urlTex.value();
            }
        }
    }
    
    return std::nullopt;
}

void ThumbnailCache::addToRam(int levelID, bool isGif, cocos2d::CCTexture2D* texture, int version, int origW, int origH) {
    if (!texture) return;
    auto key = makeRamKey(levelID, isGif);
    size_t incomingBytes = estimateTextureBytes(texture);

    std::unique_lock lock(m_ramMutex);

    // subtract old entry bytes if replacing
    if (auto oldIt = m_ramCache.find(key); oldIt != m_ramCache.end()) {
        if (m_ramBytes >= oldIt->second.byteSize) m_ramBytes -= oldIt->second.byteSize;
        else m_ramBytes = 0;
    }

    int ver = version;
    if (ver < 0) {
        ver = getVersionForKey(isGif ? -levelID : levelID);
    }

    auto now = std::chrono::steady_clock::now();
    m_ramCache[key] = RamEntry{texture, now, now, incomingBytes, ver, origW, origH};
    m_ramBytes += incomingBytes;

    evictRamLocked();
}

void ThumbnailCache::removeFromRam(int levelID, bool isGif) {
    auto key = makeRamKey(levelID, isGif);
    std::unique_lock lock(m_ramMutex);
    auto it = m_ramCache.find(key);
    if (it != m_ramCache.end()) {
        if (m_ramBytes >= it->second.byteSize) m_ramBytes -= it->second.byteSize;
        else m_ramBytes = 0;
        m_ramCache.erase(it);
    }
}

void ThumbnailCache::evictRamIfNeeded() {
    std::unique_lock lock(m_ramMutex);
    evictRamLocked();
}

void ThumbnailCache::evictRamLocked() {
    size_t maxEntries = paimon::settings::quality::ramCacheEntries();
    size_t maxBytes   = paimon::settings::quality::ramCacheBytes();

    // Early-exit: si estamos dentro de los limites, no hacer nada
    if (m_ramCache.size() <= maxEntries && m_ramBytes <= maxBytes) {
        return;
    }

    // aggregate cap: thumbnails + URL + GIF caches
    size_t gifCacheBytes = AnimatedGIFSprite::currentCacheBytes();
    size_t urlBytes = 0;
    {
        std::shared_lock ulock(m_urlMutex);
        urlBytes = m_urlBytes;
    }
    size_t aggregateBytes = m_ramBytes + urlBytes + gifCacheBytes;
    size_t effectiveMaxBytes = maxBytes;
    if (aggregateBytes > AGGREGATE_RAM_CAP && maxBytes > m_ramBytes) {
        size_t otherBytes = gifCacheBytes + urlBytes;
        effectiveMaxBytes = (AGGREGATE_RAM_CAP > otherBytes)
            ? std::min(maxBytes, AGGREGATE_RAM_CAP - otherBytes)
            : maxBytes / 2;
    }

    // Eviction LRU: evitar el bucle O(k·n) que hacia min_element por cada
    // eviction. Recolectamos candidatos y usamos partial_sort para ordenar
    // solo los primeros k por lastAccess (mas viejos primero) y borramos.
    if (m_ramCache.size() <= maxEntries && m_ramBytes <= effectiveMaxBytes) return;

    struct EvictCandidate { std::string key; int64_t accessUs; size_t bytes; };
    std::vector<EvictCandidate> candidates;
    candidates.reserve(m_ramCache.size());
    for (auto const& [k, e] : m_ramCache) {
        candidates.push_back({k, e.lastAccessUs.load(std::memory_order_relaxed), e.byteSize});
    }
    // Estimar cuantas entradas hay que evictar. Usamos partial_sort para
    // ordenar solo lo necesario en O(n log k) en vez de O(n log n).
    size_t toEvictCount = std::max<size_t>(
        m_ramCache.size() > maxEntries ? m_ramCache.size() - maxEntries : 0,
        1
    );
    // Limite de seguridad por si los bytes piden mucho mas que entries
    toEvictCount = std::min(toEvictCount + 8, candidates.size());

    if (toEvictCount < candidates.size()) {
        std::partial_sort(candidates.begin(), candidates.begin() + toEvictCount, candidates.end(),
            [](EvictCandidate const& a, EvictCandidate const& b) {
                return a.accessUs < b.accessUs;
            });
    } else {
        std::sort(candidates.begin(), candidates.end(),
            [](EvictCandidate const& a, EvictCandidate const& b) {
                return a.accessUs < b.accessUs;
            });
    }

    for (auto const& c : candidates) {
        if (m_ramCache.size() <= maxEntries && m_ramBytes <= effectiveMaxBytes) break;
        auto it = m_ramCache.find(c.key);
        if (it == m_ramCache.end()) continue;
        if (m_ramBytes >= it->second.byteSize) m_ramBytes -= it->second.byteSize;
        else m_ramBytes = 0;
        m_stats.ramEvictions.fetch_add(1, std::memory_order_relaxed);
        m_ramCache.erase(it);
    }
}

void ThumbnailCache::purgeUnusedTextures() {
    auto now = std::chrono::steady_clock::now();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    int64_t lastUs = m_lastPurgeUs.load(std::memory_order_relaxed);
    constexpr int64_t intervalUs = 2'000'000; // 2s
    if (lastUs != 0 && nowUs - lastUs < intervalUs) {
        return;
    }
    if (!m_lastPurgeUs.compare_exchange_strong(lastUs, nowUs,
                                                std::memory_order_relaxed)) {
        // otro hilo ya gano la carrera; no hacer purge duplicado
        return;
    }

    // purge level RAM: scan with shared lock, erase with exclusive lock
    {
        std::vector<std::string> toPurge;
        {
            std::shared_lock slock(m_ramMutex);
            toPurge.reserve(m_ramCache.size() / 4);
            for (auto const& [key, entry] : m_ramCache) {
                if (now - entry.addedAt < PURGE_GRACE_PERIOD) continue;
                if (entry.texture && entry.texture->retainCount() <= 1) {
                    toPurge.push_back(key);
                }
            }
        }
        if (!toPurge.empty()) {
            std::unique_lock ulock(m_ramMutex);
            for (auto const& key : toPurge) {
                auto it = m_ramCache.find(key);
                if (it != m_ramCache.end()) {
                    // Re-check conditions under exclusive lock (entry may have changed)
                    if (now - it->second.addedAt >= PURGE_GRACE_PERIOD &&
                        it->second.texture && it->second.texture->retainCount() <= 1) {
                        if (m_ramBytes >= it->second.byteSize) m_ramBytes -= it->second.byteSize;
                        else m_ramBytes = 0;
                        m_stats.ramEvictions.fetch_add(1, std::memory_order_relaxed);
                        m_ramCache.erase(it);
                    }
                }
            }
        }
    }

    // purge URL RAM: same pattern
    {
        std::vector<std::string> toPurge;
        {
            std::shared_lock slock(m_urlMutex);
            toPurge.reserve(m_urlRamCache.size() / 4);
            for (auto const& [key, entry] : m_urlRamCache) {
                if (now - entry.addedAt < PURGE_GRACE_PERIOD) continue;
                if (entry.texture && entry.texture->retainCount() <= 1) {
                    toPurge.push_back(key);
                }
            }
        }
        if (!toPurge.empty()) {
            std::unique_lock ulock(m_urlMutex);
            for (auto const& key : toPurge) {
                auto it = m_urlRamCache.find(key);
                if (it != m_urlRamCache.end()) {
                    if (now - it->second.addedAt >= PURGE_GRACE_PERIOD &&
                        it->second.texture && it->second.texture->retainCount() <= 1) {
                        if (m_urlBytes >= it->second.byteSize) m_urlBytes -= it->second.byteSize;
                        else m_urlBytes = 0;
                        m_stats.ramEvictions.fetch_add(1, std::memory_order_relaxed);
                        m_urlRamCache.erase(it);
                    }
                }
            }
        }
    }
}

size_t ThumbnailCache::ramBytes() const {
    std::shared_lock lock(m_ramMutex);
    return m_ramBytes;
}

size_t ThumbnailCache::ramEntryCount() const {
    std::shared_lock lock(m_ramMutex);
    return m_ramCache.size();
}

// ── RAM Cache: URL Gallery ──────────────────────────────────────────

std::optional<geode::Ref<cocos2d::CCTexture2D>> ThumbnailCache::getUrlFromRam(std::string const& url) {
    // Shared lock for read — allows concurrent lookups without serialization.
    // lastAccess es atomic, asi que se puede tocar sin escalar a unique_lock.
    std::shared_lock slock(m_urlMutex);
    auto it = m_urlRamCache.find(url);
    if (it == m_urlRamCache.end()) return std::nullopt;
    it->second.touchAccess(std::chrono::steady_clock::now());
    return it->second.texture;
}

void ThumbnailCache::addUrlToRam(std::string const& url, cocos2d::CCTexture2D* texture) {
    if (!texture || url.empty()) return;
    size_t incomingBytes = estimateTextureBytes(texture);

    std::unique_lock lock(m_urlMutex);

    if (auto oldIt = m_urlRamCache.find(url); oldIt != m_urlRamCache.end()) {
        if (m_urlBytes >= oldIt->second.byteSize) m_urlBytes -= oldIt->second.byteSize;
        else m_urlBytes = 0;
    }

    auto now = std::chrono::steady_clock::now();
    m_urlRamCache[url] = RamEntry{texture, now, now, incomingBytes, 0};
    m_urlBytes += incomingBytes;

    evictUrlRamLocked();
}

void ThumbnailCache::removeUrlFromRam(std::string const& url) {
    std::unique_lock lock(m_urlMutex);
    auto it = m_urlRamCache.find(url);
    if (it != m_urlRamCache.end()) {
        if (m_urlBytes >= it->second.byteSize) m_urlBytes -= it->second.byteSize;
        else m_urlBytes = 0;
        m_urlRamCache.erase(it);
    }
}

void ThumbnailCache::evictUrlRamLocked() {
    if (m_urlRamCache.size() <= URL_CACHE_MAX_ENTRIES && m_urlBytes <= URL_CACHE_MAX_BYTES) return;

    struct EvictCandidate { std::string key; int64_t accessUs; size_t bytes; };
    std::vector<EvictCandidate> candidates;
    candidates.reserve(m_urlRamCache.size());
    for (auto const& [k, e] : m_urlRamCache) {
        candidates.push_back({k, e.lastAccessUs.load(std::memory_order_relaxed), e.byteSize});
    }
    size_t toEvictCount = std::max<size_t>(
        m_urlRamCache.size() > URL_CACHE_MAX_ENTRIES ? m_urlRamCache.size() - URL_CACHE_MAX_ENTRIES : 0,
        1
    );
    toEvictCount = std::min(toEvictCount + 8, candidates.size());

    if (toEvictCount < candidates.size()) {
        std::partial_sort(candidates.begin(), candidates.begin() + toEvictCount, candidates.end(),
            [](EvictCandidate const& a, EvictCandidate const& b) {
                return a.accessUs < b.accessUs;
            });
    } else {
        std::sort(candidates.begin(), candidates.end(),
            [](EvictCandidate const& a, EvictCandidate const& b) {
                return a.accessUs < b.accessUs;
            });
    }

    for (auto const& c : candidates) {
        if (m_urlRamCache.size() <= URL_CACHE_MAX_ENTRIES && m_urlBytes <= URL_CACHE_MAX_BYTES) break;
        auto it = m_urlRamCache.find(c.key);
        if (it == m_urlRamCache.end()) continue;
        if (m_urlBytes >= it->second.byteSize) m_urlBytes -= it->second.byteSize;
        else m_urlBytes = 0;
        m_stats.ramEvictions.fetch_add(1, std::memory_order_relaxed);
        m_urlRamCache.erase(it);
    }
}

size_t ThumbnailCache::urlRamBytes() const {
    std::shared_lock lock(m_urlMutex);
    return m_urlBytes;
}

size_t ThumbnailCache::urlRamEntryCount() const {
    std::shared_lock lock(m_urlMutex);
    return m_urlRamCache.size();
}

void ThumbnailCache::clearUrlsForLevel(int levelID) {
    if (levelID <= 0) return;
    // Buscamos URLs cuyo path contenga el levelID seguido de un separador
    // tipico ('.', '_', '/'). Esto cubre patrones como:
    //   - /t/12345.png
    //   - /t/12345_2.png
    //   - /api/thumbnails/12345/list
    // Sin matchear falsos positivos como /12345abc.png o /12345000.png.
    std::string idStr = std::to_string(levelID);
    std::string n1 = "/" + idStr + ".";
    std::string n2 = "/" + idStr + "_";
    std::string n3 = "/" + idStr + "/";
    std::string n4 = "=" + idStr + "&";   // parametros tipo ?levelId=12345&...
    std::string n5 = "=" + idStr;          // al final del query string

    std::unique_lock lock(m_urlMutex);
    size_t freed = 0;
    size_t removedCount = 0;
    for (auto it = m_urlRamCache.begin(); it != m_urlRamCache.end();) {
        bool matches =
            it->first.find(n1) != std::string::npos ||
            it->first.find(n2) != std::string::npos ||
            it->first.find(n3) != std::string::npos ||
            it->first.find(n4) != std::string::npos ||
            // Para n5 verificamos que termine exactamente con el patron
            // (evita match parcial con "=12345000" cuando buscamos "=12345").
            (it->first.size() >= n5.size() &&
             it->first.compare(it->first.size() - n5.size(), n5.size(), n5) == 0 &&
             // siguiente char debe ser fin de string o no-digito
             (it->first.size() == n5.size() ||
              !std::isdigit(static_cast<unsigned char>(it->first[n5.size()]))));

        if (matches) {
            freed += it->second.byteSize;
            ++removedCount;
            it = m_urlRamCache.erase(it);
        } else {
            ++it;
        }
    }
    if (m_urlBytes >= freed) m_urlBytes -= freed;
    else m_urlBytes = 0;

    if (removedCount > 0) {
        log::debug("[ThumbnailCache] clearUrlsForLevel({}): {} URLs removed, {} bytes freed",
                   levelID, removedCount, freed);
    }
}

// ── Disk Index (delegates to DiskManifest) ──────────────────────────

bool ThumbnailCache::hasDiskEntry(int levelID, bool isGif) const {
    return m_manifest.contains(levelID, isGif);
}

std::optional<ThumbnailCache::DiskEntry> ThumbnailCache::getDiskEntry(int levelID, bool isGif) const {
    auto const* entry = m_manifest.getEntry(levelID, isGif);
    if (entry) return *entry;
    return std::nullopt;
}

void ThumbnailCache::upsertDisk(DiskEntry entry) {
    bool isGif = entry.isGif;
    int levelID = entry.levelID;
    // ensure filename is set for the manifest
    if (entry.filename.empty()) {
        entry.filename = paimon::quality::thumbFilename(levelID, isGif);
    }
    std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
    m_manifest.upsert(levelID, isGif, std::move(entry));
}

void ThumbnailCache::removeDisk(int levelID, bool isGif) {
    std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
    m_manifest.remove(levelID, isGif);
}

void ThumbnailCache::touchDiskAccess(int levelID, bool isGif) {
    std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
    m_manifest.touchAccess(levelID, isGif);
}

void ThumbnailCache::evictDiskIfNeeded(size_t maxBytes, std::chrono::hours maxAge) {
    std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);

    size_t currentBytes = m_manifest.totalBytesLocked();
    if (currentBytes <= maxBytes) return;

    auto pruneResult = m_manifest.computePrune(maxBytes, maxAge);
    if (pruneResult.filesToDelete.empty()) return;

    m_manifest.applyPrune(pruneResult);
    m_stats.diskEvictions.fetch_add(pruneResult.filesToDelete.size(), std::memory_order_relaxed);

    log::info("[ThumbnailCache] disk prune: {} files removed, {} bytes freed",
        pruneResult.filesToDelete.size(), pruneResult.freedBytes);
}

size_t ThumbnailCache::diskTotalBytes() const {
    return m_manifest.totalBytes();
}

size_t ThumbnailCache::diskEntryCount() const {
    return m_manifest.entryCount();
}

// ── Disk Index Persistence (DiskManifest — standalone manifest.json) ──

void ThumbnailCache::loadDiskIndex() {
    auto cacheDir = paimon::quality::cacheDir();

    // ensure cache dir exists
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);

    // delegate loading to DiskManifest (handles migration from dir scan,
    // legacy manifest.json, orphan cleanup, etc.)
    {
        std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
        m_manifest.load(cacheDir);
    }

    // also try migrating from Geode SavedValues if DiskManifest came up empty
    // (one-time migration from the old persistence layer)
    if (m_manifest.entryCount() == 0) {
        auto savedCache = Mod::get()->getSavedValue<matjson::Value>("thumbnail-disk-cache");
        if (savedCache.isObject()) {
            int migrated = 0;
            std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
            for (auto const& [key, val] : savedCache) {
                if (!val.isObject()) continue;

                DiskManifestEntry entry;
                entry.levelID = val["levelID"].asInt().unwrapOr(0);
                entry.format = val["format"].asString().unwrapOr("");
                entry.revisionToken = val["revisionToken"].asString().unwrapOr("");
                entry.byteSize = static_cast<size_t>(val["byteSize"].asInt().unwrapOr(0));
                entry.width = val["width"].asInt().unwrapOr(0);
                entry.height = val["height"].asInt().unwrapOr(0);
                entry.lastAccessEpoch = val["lastAccess"].asInt().unwrapOr(0);
                entry.lastValidatedEpoch = val["lastValidated"].asInt().unwrapOr(0);
                entry.isGif = val["isGif"].asBool().unwrapOr(false);
                entry.filename = paimon::quality::thumbFilename(entry.levelID, entry.isGif);

                // validate file exists
                if (!std::filesystem::exists(cacheDir / entry.filename, ec)) {
                    continue;
                }

                m_manifest.upsert(entry.levelID, entry.isGif, std::move(entry));
                migrated++;
            }
            if (migrated > 0) {
                log::info("[ThumbnailCache] migrated {} entries from Geode SavedValues to DiskManifest", migrated);
                // flush immediately so the manifest.json file is created
                m_manifest.flush();
                // clear the old saved value to avoid re-migration
                Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::makeObject({}));
                (void)Mod::get()->saveData();
            }
        }
    }

    // persist immediately if dirty (dir scan, SavedValues migration)
    // so the manifest.json exists even if the game crashes later
    m_manifest.flush();

    log::info("[ThumbnailCache] disk index ready: {} entries", m_manifest.entryCount());
}

void ThumbnailCache::saveDiskIndex(bool allowDuringShutdown) {
    if (!allowDuringShutdown && paimon::isRuntimeShuttingDown()) return;
    m_manifest.flush();
}

// auto-persist on DataSaved hook
$on_mod(DataSaved) {
    // allowDuringShutdown=true: DataSaved se dispara incluso durante el
    // shutdown de Geode. Sin este flag, saveDiskIndex retornaria temprano
    // y perderiamos las entradas pendientes de flush (debounced 10s tras
    // cada descarga exitosa). Pasarlo en true garantiza que el manifest
    // siempre se persista al cerrar el juego.
    ThumbnailCache::get().saveDiskIndex(true);
}

// ── Failed Cache ────────────────────────────────────────────────────

bool ThumbnailCache::isFailed(std::string const& key) const {
    std::lock_guard lock(m_failedMutex);
    auto it = m_failedCache.find(key);
    if (it == m_failedCache.end()) return false;

    int step = std::min(it->second.retryStep, FAILED_BACKOFF_MAX_STEP);

    // backoff exponencial: el TTL crece con cada retry.
    // El paso maximo (index FAILED_BACKOFF_MAX_STEP) tiene TTL de 5 min;
    // tras eso se permite reintento — no es permanente.
    auto ttl = std::chrono::seconds(FAILED_BACKOFF_STEPS[step]);
    if (std::chrono::steady_clock::now() - it->second.timestamp >= ttl) {
        // TTL expirado — permitir reintento pero NO borrar la entrada.
        // Asi markFailed() puede incrementar retryStep y el backoff escala.
        return false;
    }
    return true;
}

void ThumbnailCache::markFailed(std::string const& key) {
    bool shouldMarkNotFound = false;
    {
        std::lock_guard lock(m_failedMutex);
        auto it = m_failedCache.find(key);
        if (it != m_failedCache.end()) {
            // Incrementar paso de backoff. Tras el SEGUNDO fallo (retryStep=1),
            // promovemos a notFound permanente: el usuario reporto que retries
            // infinitos consumian demasiados requests al servidor. Ahora tras
            // 2 fallos paramos hasta que el usuario presione "actualizar"
            // (que llama a invalidateLevel, el cual limpia ambas tablas).
            it->second.retryStep = std::min(it->second.retryStep + 1, FAILED_BACKOFF_MAX_STEP);
            it->second.timestamp = std::chrono::steady_clock::now();
            if (it->second.retryStep >= 1) {
                shouldMarkNotFound = true;
            }
        } else {
            // Primer fallo
            m_failedCache[key] = {std::chrono::steady_clock::now(), 0};
        }
    }
    // markNotFound toma su propio mutex; evitar nested locks.
    if (shouldMarkNotFound) {
        markNotFound(key);
    }
}

void ThumbnailCache::clearFailed(std::string const& key) {
    std::lock_guard lock(m_failedMutex);
    m_failedCache.erase(key);
}

void ThumbnailCache::clearAllFailed() {
    std::lock_guard lock(m_failedMutex);
    m_failedCache.clear();
}

void ThumbnailCache::purgeExpiredFailed() {
    std::lock_guard lock(m_failedMutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_failedCache.begin(); it != m_failedCache.end();) {
        int step = std::min(it->second.retryStep, FAILED_BACKOFF_MAX_STEP);
        // Ningun fallo es permanente; tras el TTL del ultimo paso se purga.
        auto ttl = std::chrono::seconds(FAILED_BACKOFF_STEPS[step]);
        if (now - it->second.timestamp >= ttl) {
            it = m_failedCache.erase(it);
        } else {
            ++it;
        }
    }
}

// ── Not-Found Cache (with TTL) ───────────────────────────────────

bool ThumbnailCache::isNotFound(std::string const& key) const {
    std::lock_guard lock(m_notFoundMutex);
    auto it = m_notFoundCache.find(key);
    if (it == m_notFoundCache.end()) return false;
    if (std::chrono::steady_clock::now() - it->second >= NOT_FOUND_TTL) {
        m_notFoundCache.erase(it);
        return false;
    }
    return true;
}

void ThumbnailCache::markNotFound(std::string const& key) {
    std::lock_guard lock(m_notFoundMutex);
    m_notFoundCache[key] = std::chrono::steady_clock::now();
}

void ThumbnailCache::clearNotFound(std::string const& key) {
    std::lock_guard lock(m_notFoundMutex);
    m_notFoundCache.erase(key);
}

void ThumbnailCache::clearAllNotFound() {
    std::lock_guard lock(m_notFoundMutex);
    m_notFoundCache.clear();
}

// ── Invalidation ────────────────────────────────────────────────────

int ThumbnailCache::getInvalidationVersion(int levelID) const {
    std::lock_guard lock(m_invalidationMutex);
    auto it = m_invalidationVersions.find(levelID);
    return it != m_invalidationVersions.end() ? it->second : 0;
}

void ThumbnailCache::incrementInvalidation(int levelID) {
    std::lock_guard lock(m_invalidationMutex);
    m_invalidationVersions[levelID]++;
}

int ThumbnailCache::getVersionForKey(int legacyKey) const {
    int realID = (legacyKey < 0) ? -legacyKey : legacyKey;
    return getInvalidationVersion(realID);
}

// ── Lifecycle ───────────────────────────────────────────────────────

void ThumbnailCache::clearRam() {
    {
        std::unique_lock lock(m_ramMutex);
        m_ramCache.clear();
        m_ramBytes = 0;
    }
    {
        std::unique_lock lock(m_urlMutex);
        m_urlRamCache.clear();
        m_urlBytes = 0;
    }
    {
        std::lock_guard lock(m_failedMutex);
        m_failedCache.clear();
    }
    {
        std::lock_guard lock(m_notFoundMutex);
        m_notFoundCache.clear();
    }
}

void ThumbnailCache::clearDisk() {
    auto dir = paimon::quality::cacheDir();
    // Preservar miniaturas de main levels (1-22) y la carpeta gifs/.
    auto [preserved, removed] = paimon::clearCachePreservingMainLevels(dir, {"gifs"});
    log::info("[ThumbnailCache] clearDisk: preserved {} entries, removed {}",
        preserved, removed);
    {
        std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
        m_manifest.clearPreservingMainLevels();
    }
    // recreate dir si por algun caso quedo vacio
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}

void ThumbnailCache::clearAll() {
    clearRam();
    clearDisk();
}

void ThumbnailCache::takeAllTextures() {
    // take() Ref pointers without calling release() to avoid crash
    // when Cocos2d is already dead during static destruction
    size_t levelCount = 0;
    {
        std::unique_lock lock(m_ramMutex);
        levelCount = m_ramCache.size();
        for (auto& [_, entry] : m_ramCache) {
            if (entry.texture) {
                (void)entry.texture.take();
            }
        }
        m_ramCache.clear();
        m_ramBytes = 0;
    }
    size_t urlCount = 0;
    {
        std::unique_lock lock(m_urlMutex);
        urlCount = m_urlRamCache.size();
        for (auto& [_, entry] : m_urlRamCache) {
            if (entry.texture) {
                (void)entry.texture.take();
            }
        }
        m_urlRamCache.clear();
        m_urlBytes = 0;
    }
    log::info("[ThumbnailCache] takeAllTextures: detached {} level textures and {} URL textures", levelCount, urlCount);
}

} // namespace paimon::cache
