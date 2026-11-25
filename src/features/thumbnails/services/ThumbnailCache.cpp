#include "ThumbnailCache.hpp"
#include "ThumbnailTransportClient.hpp"
#include "ThumbnailLoader.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../core/MainLevels.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/Debug.hpp"
#include "../../../utils/UrlKeyNormalize.hpp"
#include "../../../utils/MainThread.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "CacheMemoryBudget.hpp"
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

ThumbnailCache& ThumbnailCache::get() {
    // intentional leak: post-geode-shutdown dtor crashes (bunny reports).
    static auto* instance = new ThumbnailCache();
    s_cacheInstanceAlive = true;
    return *instance;
}

bool ThumbnailCache::isAlive() {
    return s_cacheInstanceAlive;
}

ThumbnailCache::~ThumbnailCache() {
    // detach without release() if ccpoolmanager already died in teardown.
    takeAllTextures();
    s_cacheInstanceAlive = false;
}

size_t ThumbnailCache::estimateTextureBytes(cocos2d::CCTexture2D* tex) {
    if (!tex) return 0;
    return static_cast<size_t>(tex->getPixelsWide()) * static_cast<size_t>(tex->getPixelsHigh()) * 4;
}

int ThumbnailCache::makeRamKey(int levelID, bool isGif) {
    return paimon::cache::makeLevelRamKey(levelID, isGif);
}

int64_t ThumbnailCache::nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::optional<geode::Ref<cocos2d::CCTexture2D>> ThumbnailCache::getFromRam(int levelID, bool isGif) {
    int const key = makeRamKey(levelID, isGif);
    
    {
        std::shared_lock slock(m_ramMutex);
        auto it = m_ramCache.find(key);
        if (it != m_ramCache.end()) {
            it->second.touchAccess(std::chrono::steady_clock::now());
            return it->second.texture;
        }
    }
    
    // probe url cache only when it can match.
    if (!isGif) {
        bool urlCacheEmpty = false;
        {
            std::shared_lock ulock(m_urlMutex);
            urlCacheEmpty = m_urlRamCache.empty();
        }
        if (!urlCacheEmpty) {
            std::string defaultUrl = ThumbnailTransportClient::get().getThumbnailURL(levelID);
            if (!defaultUrl.empty()) {
                std::string cacheKey = paimon::cache::normalizeUrlKey(defaultUrl);
                auto urlTex = getUrlFromRam(cacheKey);
                if (urlTex.has_value()) {
                    addToRam(levelID, isGif, urlTex.value().data());
                    return urlTex.value();
                }
            }
        }
    }
    
    return std::nullopt;
}

bool ThumbnailCache::hasInRam(int levelID, bool isGif) const {
    int const key = makeRamKey(levelID, isGif);
    std::shared_lock slock(m_ramMutex);
    return m_ramCache.find(key) != m_ramCache.end();
}

bool ThumbnailCache::isRamEntrySuitable(int levelID, bool isGif, int requestedMaxDim) const {
    if (requestedMaxDim <= 0) return true;

    int const key = makeRamKey(levelID, isGif);
    std::shared_lock slock(m_ramMutex);
    auto it = m_ramCache.find(key);
    if (it == m_ramCache.end() || !it->second.texture) return false;

    auto* tex = it->second.texture.data();
    int texLong = std::max(tex->getPixelsWide(), tex->getPixelsHigh());
    int originalLong = std::max(it->second.originalWidth, it->second.originalHeight);

    // unknown source dims stay usable; no disk/network churn to upgrade them.
    if (originalLong <= 0) return true;

    int targetLong = std::min(requestedMaxDim, originalLong);
    return texLong + 8 >= targetLong;
}

void ThumbnailCache::addToRam(int levelID, bool isGif, cocos2d::CCTexture2D* texture, int version, int origW, int origH) {
    if (!texture) return;
    int const key = makeRamKey(levelID, isGif);
    size_t incomingBytes = estimateTextureBytes(texture);

    std::unique_lock lock(m_ramMutex);

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

    lock.unlock();
    evictRamIfNeeded();
}

void ThumbnailCache::removeFromRam(int levelID, bool isGif) {
    int const key = makeRamKey(levelID, isGif);
    std::unique_lock lock(m_ramMutex);
    auto it = m_ramCache.find(key);
    if (it != m_ramCache.end()) {
        if (m_ramBytes >= it->second.byteSize) m_ramBytes -= it->second.byteSize;
        else m_ramBytes = 0;
        m_ramCache.erase(it);
    }
}

void ThumbnailCache::evictRamIfNeeded() {
    auto const gifBytes = AnimatedGIFSprite::currentCacheBytes();
    size_t gifLimit = 0;
    {
        // always acquire level before URL; never hold either while trimming GIFs.
        std::unique_lock levelLock(m_ramMutex);
        std::unique_lock urlLock(m_urlMutex);
        evictUrlRamLocked(URL_CACHE_MAX_BYTES);
        evictRamLocked(cacheByteLimit(paimon::settings::quality::ramCacheBytes(),
            AGGREGATE_RAM_CAP, gifBytes, m_urlBytes));
        evictUrlRamLocked(cacheByteLimit(URL_CACHE_MAX_BYTES,
            AGGREGATE_RAM_CAP, gifBytes, m_ramBytes));
        gifLimit = cacheByteLimit(AGGREGATE_RAM_CAP, AGGREGATE_RAM_CAP, m_ramBytes, m_urlBytes);
    }
    AnimatedGIFSprite::trimCacheToBudget(gifLimit);
}

// LRU eviction shared by the level and URL RAM caches: collect non-skipped
// candidates oldest-first (partial sort), then erase until both limits hold.
template <typename Map, typename SkipFn>
static void evictLruLocked(Map& map, size_t& bytes, size_t maxEntries, size_t byteLimit,
                           SkipFn&& skipKey, CacheStats& stats) {
    if (map.size() <= maxEntries && bytes <= byteLimit) return;

    using Key = typename Map::key_type;
    struct EvictCandidate { Key key; int64_t accessUs; };
    std::vector<EvictCandidate> candidates;
    candidates.reserve(map.size());
    for (auto const& [k, e] : map) {
        if (skipKey(k)) continue;
        candidates.push_back({k, e.lastAccessUs.load(std::memory_order_relaxed)});
    }
    size_t toEvictCount = std::max<size_t>(
        map.size() > maxEntries ? map.size() - maxEntries : 0,
        1
    );
    toEvictCount = std::min(toEvictCount + 8, candidates.size());

    auto byAccess = [](EvictCandidate const& a, EvictCandidate const& b) {
        return a.accessUs < b.accessUs;
    };
    if (toEvictCount < candidates.size()) {
        std::partial_sort(candidates.begin(), candidates.begin() + toEvictCount, candidates.end(), byAccess);
    } else {
        std::sort(candidates.begin(), candidates.end(), byAccess);
    }

    for (auto const& c : candidates) {
        if (map.size() <= maxEntries && bytes <= byteLimit) break;
        auto it = map.find(c.key);
        if (it == map.end()) continue;
        if (bytes >= it->second.byteSize) bytes -= it->second.byteSize;
        else bytes = 0;
        stats.ramEvictions.fetch_add(1, std::memory_order_relaxed);
        map.erase(it);
    }
}

void ThumbnailCache::evictRamLocked(size_t effectiveMaxBytes) {
    // main levels 1-22 pinned for instant levelselect backgrounds.
    evictLruLocked(m_ramCache, m_ramBytes, paimon::settings::quality::ramCacheEntries(),
        effectiveMaxBytes,
        [](int key) { return paimon::isMainLevelID(paimon::cache::levelIdFromRamKey(key)); },
        m_stats);
}

void ThumbnailCache::purgeUnusedTextures() {
    auto now = std::chrono::steady_clock::now();
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    int64_t lastUs = m_lastPurgeUs.load(std::memory_order_relaxed);
constexpr int64_t intervalUs = 2'000'000; // 2 s.
    if (lastUs != 0 && nowUs - lastUs < intervalUs) {
        return;
    }
    if (!m_lastPurgeUs.compare_exchange_strong(lastUs, nowUs,
                                                std::memory_order_relaxed)) {
        return;
    }

    constexpr size_t kMaxPurgeCandidates = 64;

    // two-phase purge: collect candidates under a shared lock, then erase
    // under the exclusive lock only after rechecking each entry.
    auto purgeMap = [&](auto& map, auto& mutex, size_t& bytes, auto&& skipKey) {
        using Key = typename std::decay_t<decltype(map)>::key_type;
        std::vector<Key> toPurge;
        {
            std::shared_lock slock(mutex);
            toPurge.reserve(std::min(map.size() / 4, kMaxPurgeCandidates));
            for (auto const& [key, entry] : map) {
                if (toPurge.size() >= kMaxPurgeCandidates) break;
                if (skipKey(key)) continue;
                if (now - entry.addedAt < PURGE_GRACE_PERIOD) continue;
                if (entry.texture && entry.texture->retainCount() <= 1) {
                    toPurge.push_back(key);
                }
            }
        }
        if (toPurge.empty()) return;
        std::unique_lock ulock(mutex);
        for (auto const& key : toPurge) {
            auto it = map.find(key);
            if (it == map.end()) continue;
            // recheck under exclusive lock; entry may have changed.
            if (now - it->second.addedAt >= PURGE_GRACE_PERIOD &&
                it->second.texture && it->second.texture->retainCount() <= 1) {
                if (bytes >= it->second.byteSize) bytes -= it->second.byteSize;
                else bytes = 0;
                m_stats.ramEvictions.fetch_add(1, std::memory_order_relaxed);
                map.erase(it);
            }
        }
    };

    // main levels 1-22 never purge after preload.
    purgeMap(m_ramCache, m_ramMutex, m_ramBytes, [](int key) {
        return paimon::isMainLevelID(paimon::cache::levelIdFromRamKey(key));
    });
    purgeMap(m_urlRamCache, m_urlMutex, m_urlBytes, [](std::string const&) { return false; });
}

size_t ThumbnailCache::ramEntryCount() const {
    std::shared_lock lock(m_ramMutex);
    return m_ramCache.size();
}

std::optional<geode::Ref<cocos2d::CCTexture2D>> ThumbnailCache::getUrlFromRam(std::string const& url) {
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

    lock.unlock();
    evictRamIfNeeded();
}

void ThumbnailCache::evictUrlRamLocked(size_t byteLimit) {
    evictLruLocked(m_urlRamCache, m_urlBytes, URL_CACHE_MAX_ENTRIES, byteLimit,
        [](std::string const&) { return false; }, m_stats);
}

void ThumbnailCache::clearUrlsForLevel(int levelID) {
    if (levelID <= 0) return;
    // match the id only at url separator/query boundaries.
    std::string idStr = std::to_string(levelID);
    std::string n1 = "/" + idStr + ".";
    std::string n2 = "/" + idStr + "_";
    std::string n3 = "/" + idStr + "/";
    std::string n4 = "=" + idStr + "&";
    std::string n5 = "=" + idStr;

    std::unique_lock lock(m_urlMutex);
    size_t freed = 0;
    size_t removedCount = 0;
    for (auto it = m_urlRamCache.begin(); it != m_urlRamCache.end();) {
        bool matches =
            it->first.find(n1) != std::string::npos ||
            it->first.find(n2) != std::string::npos ||
            it->first.find(n3) != std::string::npos ||
            it->first.find(n4) != std::string::npos ||
            // trailing "=<id>" suffix is already boundary-safe.
            (it->first.size() >= n5.size() &&
             it->first.compare(it->first.size() - n5.size(), n5.size(), n5) == 0);

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

bool ThumbnailCache::hasDiskEntry(int levelID, bool isGif) const {
    return m_manifest.contains(levelID, isGif);
}

std::optional<ThumbnailCache::DiskEntry> ThumbnailCache::getDiskEntry(int levelID, bool isGif) const {
    return m_manifest.getEntry(levelID, isGif);
}

void ThumbnailCache::upsertDisk(DiskEntry entry) {
    bool isGif = entry.isGif;
    int levelID = entry.levelID;
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
    // computePrune and applyPrune each take the manifest lock internally.
    // applyPrune only renames files under the lock and deletes them afterwards,
    // so UI queries never wait for file deletion.
    auto pruneResult = m_manifest.computePrune(maxBytes, maxAge);
    if (pruneResult.items.empty()) return;

    auto const removed = m_manifest.applyPrune(pruneResult);
    if (removed == 0) return;

    m_stats.diskEvictions.fetch_add(removed, std::memory_order_relaxed);

    log::info("[ThumbnailCache] disk prune: {} files removed, {} bytes freed",
        removed, pruneResult.freedBytes);
}

size_t ThumbnailCache::diskTotalBytes() const {
    return m_manifest.totalBytes();
}

size_t ThumbnailCache::diskEntryCount() const {
    return m_manifest.entryCount();
}

void ThumbnailCache::loadDiskIndex() {
    auto cacheDir = paimon::quality::cacheDir();

    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);

    m_manifest.load(cacheDir);

    // one-time savedvalues migration while the manifest is empty.
    if (m_manifest.entryCount() == 0) {
        auto savedCache = Mod::get()->getSavedValue<matjson::Value>("thumbnail-disk-cache");
        if (savedCache.isObject()) {
            int migrated = 0;
            for (auto const& [key, val] : savedCache) {
                if (!val.isObject()) continue;

                DiskManifestEntry entry;
                entry.levelID = val["levelID"].asInt().unwrapOr(0);
                entry.format = val["format"].asString().unwrapOr("");
                entry.revisionToken = val["revisionToken"].asString().unwrapOr("");
                {
                    int rawBytes = val["byteSize"].asInt().unwrapOr(0);
                    entry.byteSize = rawBytes > 0 ? static_cast<size_t>(rawBytes) : 0;
                }
                entry.width = val["width"].asInt().unwrapOr(0);
                entry.height = val["height"].asInt().unwrapOr(0);
                entry.lastAccessEpoch = val["lastAccess"].asInt().unwrapOr(0);
                entry.lastValidatedEpoch = val["lastValidated"].asInt().unwrapOr(0);
                entry.isGif = val["isGif"].asBool().unwrapOr(false);
                entry.filename = paimon::quality::thumbFilename(entry.levelID, entry.isGif);

                if (!std::filesystem::exists(cacheDir / entry.filename, ec)) {
                    continue;
                }

                auto const levelID = entry.levelID;
                auto const isGif = entry.isGif;
                m_manifest.upsert(levelID, isGif, std::move(entry));
                migrated++;
            }
            if (migrated > 0) {
                log::info("[ThumbnailCache] migrated {} entries from Geode SavedValues to DiskManifest", migrated);
                m_manifest.flush();
                Mod::get()->setSavedValue("thumbnail-disk-cache", matjson::makeObject({}));
                (void)Mod::get()->saveData();
            }
        }
    }

    m_manifest.flush();

    log::info("[ThumbnailCache] disk index ready: {} entries", m_manifest.entryCount());
}

void ThumbnailCache::saveDiskIndex(bool allowDuringShutdown) {
    if (!allowDuringShutdown && paimon::isRuntimeShuttingDown()) return;
    if (paimon::isMainThread() && !paimon::isRuntimeShuttingDown()) {
        // DataSaved is delivered on the UI thread too. A worker alone is not enough:
        // DiskManifest::flush must also release its reader mutex before serialization/I/O.
        static std::atomic<bool> pending{false};
        if (pending.exchange(true, std::memory_order_acq_rel)) return;
        if (!paimon::ThreadTracker::get().spawn([this]() {
            struct ResetPending {
                ~ResetPending() { pending.store(false, std::memory_order_release); }
            } reset;
            m_manifest.flush();
        })) pending.store(false, std::memory_order_release);
        return;
    }
    m_manifest.flush();
}

$on_mod(DataSaved) {
    // allowduringshutdown keeps the pending flush from being lost.
    ThumbnailCache::get().saveDiskIndex(true);
}

bool ThumbnailCache::isFailed(std::string const& key) const {
    std::lock_guard lock(m_failedMutex);
    auto it = m_failedCache.find(key);
    if (it == m_failedCache.end()) return false;

    int step = std::min(it->second.retryStep, FAILED_BACKOFF_MAX_STEP);

    // backoff grows the ttl; the 5-minute step stays temporary.
    auto ttl = std::chrono::seconds(FAILED_BACKOFF_STEPS[step]);
    if (std::chrono::steady_clock::now() - it->second.timestamp >= ttl) {
        // expired entries retry unerased so markfailed() can grow backoff.
        return false;
    }
    return true;
}

void ThumbnailCache::markFailed(std::string const& key) {
    std::lock_guard lock(m_failedMutex);
    auto it = m_failedCache.find(key);
    if (it != m_failedCache.end()) {
        it->second.retryStep = std::min(it->second.retryStep + 1, FAILED_BACKOFF_MAX_STEP);
        it->second.timestamp = std::chrono::steady_clock::now();
    } else {
        m_failedCache[key] = {std::chrono::steady_clock::now(), 0};
    }
    while (m_failedCache.size() > FAILED_CACHE_MAX_ENTRIES) {
        auto oldest = std::min_element(m_failedCache.begin(), m_failedCache.end(),
            [](auto const& a, auto const& b) { return a.second.timestamp < b.second.timestamp; });
        if (oldest == m_failedCache.end()) break;
        m_failedCache.erase(oldest);
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
        auto ttl = std::chrono::seconds(FAILED_BACKOFF_STEPS[step]);
        if (now - it->second.timestamp >= ttl) {
            it = m_failedCache.erase(it);
        } else {
            ++it;
        }
    }
}

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
    while (m_notFoundCache.size() > NOT_FOUND_CACHE_MAX_ENTRIES) {
        auto oldest = std::min_element(m_notFoundCache.begin(), m_notFoundCache.end(),
            [](auto const& a, auto const& b) { return a.second < b.second; });
        if (oldest == m_notFoundCache.end()) break;
        m_notFoundCache.erase(oldest);
    }
}

void ThumbnailCache::clearNotFound(std::string const& key) {
    std::lock_guard lock(m_notFoundMutex);
    m_notFoundCache.erase(key);
}

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
    // main levels 1-22 and gifs/ survive.
    auto [preserved, removed] = paimon::clearCachePreservingMainLevels(dir, {"gifs"});
    log::info("[ThumbnailCache] clearDisk: preserved {} entries, removed {}",
        preserved, removed);
    {
        std::lock_guard<std::recursive_mutex> lock(m_manifest.mutex);
        m_manifest.clearPreservingMainLevels();
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}

void ThumbnailCache::clearAll() {
    clearRam();
    clearDisk();
}

void ThumbnailCache::takeAllTextures() {
    // take() without release(): cocos may already be dead in static destruction.
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

}
