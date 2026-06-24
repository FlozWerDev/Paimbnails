#pragma once

// HTTP transport for the Global Icon server endpoints; delegates to HttpClient
// (full URLs, X-API-Key on post()). Knows nothing about More Icons; just net + JSON.

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace paimon::globalicon {

// Parse a metadata JSON object into GlobalIconMeta.
GlobalIconMeta parseMetaJson(matjson::Value const& v);

class GlobalIconClient {
public:
    // success, found (false = 404/not synced), meta
    using MetaCallback  = geode::CopyableFunction<void(bool success, bool found, GlobalIconMeta const& meta)>;
    using BatchCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, GlobalIconMeta> const& metas)>;
    using FileCallback  = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using SyncCallback  = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static GlobalIconClient& get();

    // Normalized base URL (no trailing slash).
    std::string baseUrl() const;

    // GET /api/icons/<accountID> (public)
    void getMetadata(int accountID, MetaCallback cb);
    // POST /api/icons/batch (public) — cap 64 ids
    void getMetadataBatch(std::vector<int> const& accountIDs, BatchCallback cb);
    // GET a blob (png/plist) by full URL; SSRF-validated via HttpClient.
    void downloadFile(std::string const& url, FileCallback cb);
    // POST /api/icons/sync (X-API-Key) — takes a prebuilt JSON body.
    void syncIcons(std::string const& jsonBody, SyncCallback cb);
    // POST /api/icons/sync with icons:[] — clears/disables the account.
    void clearIcons(int accountID, std::string const& username, SyncCallback cb);

private:
    GlobalIconClient() = default;
};

} // namespace paimon::globalicon
