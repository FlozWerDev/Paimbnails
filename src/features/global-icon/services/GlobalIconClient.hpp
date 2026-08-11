#pragma once

// HTTP transport for the Global Icon server endpoints; delegates to HttpClient
// (full URLs, X-API-Key on post()). Knows nothing about More Icons; just net + JSON.
//
// Metadata reads go through a TTL cache with in-flight coalescing: ProfilePage
// re-runs its render pass on every layout, so without this a single profile
// visit would fire several identical requests (and a 404 per visit for the
// majority of players, who share nothing).

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace paimon::globalicon {

// Parse a metadata JSON object into GlobalIconMeta.
GlobalIconMeta parseMetaJson(matjson::Value const& v);

class GlobalIconClient {
public:
    // success, found (false = 404/not sharing), meta
    using MetaCallback  = geode::CopyableFunction<void(bool success, bool found, GlobalIconMeta const& meta)>;
    using BatchCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, GlobalIconMeta> const& metas)>;
    using FileCallback  = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using SyncCallback  = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static GlobalIconClient& get();

    // Normalized base URL (no trailing slash).
    std::string baseUrl() const;

    // GET /api/icons/<accountID> (public). Served from cache when fresh.
    void getMetadata(int accountID, MetaCallback cb);
    // POST /api/icons/batch (public) — cap 64 ids. Fills the same cache.
    void getMetadataBatch(std::vector<int> const& accountIDs, BatchCallback cb);
    // GET a blob (png/plist) by full URL; SSRF-validated via HttpClient.
    void downloadFile(std::string const& url, FileCallback cb);
    // POST /api/icons/sync (X-API-Key) — takes a prebuilt JSON body.
    void syncIcons(std::string const& jsonBody, SyncCallback cb);
    // POST /api/icons/sync with icons:[] — clears/disables the account.
    void clearIcons(int accountID, std::string const& username, SyncCallback cb);

    // Drop a cached entry (call after the local player syncs or clears).
    void invalidate(int accountID);
    void invalidateAll();

private:
    GlobalIconClient() = default;

    struct CacheEntry {
        GlobalIconMeta meta;
        bool found = false;
        int64_t fetchedAt = 0;
    };

    // Cached metadata lifetime. Negative results expire sooner so a player who
    // just enabled sharing shows up without a game restart.
    static constexpr int64_t kPositiveTtlSeconds = 300;
    static constexpr int64_t kNegativeTtlSeconds = 120;
    static constexpr size_t  kMaxCacheEntries    = 256;

    bool lookup(int accountID, CacheEntry& out) const;
    void store(int accountID, GlobalIconMeta const& meta, bool found);

    // Main-thread only (Geode dispatches web callbacks there), so no locking.
    std::unordered_map<int, CacheEntry> m_cache;
    std::unordered_map<int, std::vector<MetaCallback>> m_inflight;
};

} // namespace paimon::globalicon
