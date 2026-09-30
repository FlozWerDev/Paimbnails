#pragma once

// net + json only; knows nothing about more icons. coalescing matters because
// profilepage re-runs its render pass on every layout.

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace paimon::globalicon {

GlobalIconMeta parseMetaJson(matjson::Value const& v);

class GlobalIconClient {
public:
    // success, found (false = 404/not sharing), meta
    using MetaCallback  = geode::CopyableFunction<void(bool success, bool found, GlobalIconMeta const& meta)>;
    using FileCallback  = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using SyncCallback  = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static GlobalIconClient& get();

    // normalized base url (no trailing slash).
    std::string baseUrl() const;

    // get /api/icons/<accountid> (public). served from cache when fresh.
    void getMetadata(int accountID, MetaCallback cb);
    // get a blob (png/plist) by full url; ssrf-validated via httpclient.
    void downloadFile(std::string const& url, FileCallback cb);
    // post /api/icons/sync (x-api-key) — takes a prebuilt json body.
    void syncIcons(std::string const& jsonBody, SyncCallback cb);
    // post /api/icons/sync with icons:[] — clears/disables the account.
    void clearIcons(int accountID, std::string const& username, SyncCallback cb);

    // drop a cached entry (call after the local player syncs or clears).
    void invalidate(int accountID);

private:
    GlobalIconClient() = default;

    struct CacheEntry {
        GlobalIconMeta meta;
        bool found = false;
        int64_t fetchedAt = 0;
    };

    // cached metadata lifetime. negative results expire sooner so a player who
    // just enabled sharing shows up without a game restart.
    static constexpr int64_t kPositiveTtlSeconds = 300;
    static constexpr int64_t kNegativeTtlSeconds = 120;
    static constexpr size_t  kMaxCacheEntries    = 256;

    bool lookup(int accountID, CacheEntry& out) const;
    void store(int accountID, GlobalIconMeta const& meta, bool found);

    // main-thread only (geode dispatches web callbacks there), so no locking.
    std::unordered_map<int, CacheEntry> m_cache;
    std::unordered_map<int, std::vector<MetaCallback>> m_inflight;
};

} // namespace paimon::globalicon
