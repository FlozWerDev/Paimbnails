#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include "ThumbnailTypes.hpp"
#include "AccountVerifier.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <atomic>

class HttpClient {
public:
    // Geode v5: CopyableFunction replaces std::function (same copyable semantics,
    // but uses std23::function internally for better ABI compatibility).
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data, int width, int height)>;
    using CheckCallback = geode::CopyableFunction<void(bool exists)>;
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using GenericCallback = geode::CopyableFunction<void(bool success, std::string const& response)>;
    using BanListCallback = geode::CopyableFunction<void(bool success, std::string const& jsonData)>;
    using BanUserCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using ModeratorsListCallback = geode::CopyableFunction<void(bool success, std::vector<std::string> const& moderators)>;

    static HttpClient& get() {
        static HttpClient instance;
        return instance;
    }

    std::string getServerURL() const { return m_serverURL; }
    void setServerURL(std::string const& url);

    std::string getCDNBaseURL() const { return m_cdnBaseURL; }

    std::string getForumServerURL() const { return m_forumServerURL; }
    void setForumServerURL(std::string const& url);

    static std::string encodeQueryParam(std::string const& value);

    // mod code
    std::string getModCode() const { return m_modCode; }
    void setModCode(std::string const& code);

    // clear tasks; allowNewRequests=false at final game shutdown
    void cleanTasks(bool allowNewRequests = true);


    // upload thumb png
    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);

    // upload gif (mod/admin)
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);

    // upload mp4 video (mod/admin)
    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);

    // list thumbs
    void getThumbnails(int levelId, GenericCallback callback);

    // list thumbs (gallery) for many levels in one request.
    // Returns map id -> JSON-array-string (empty on failure); parsing happens
    // in the transport layer to reuse parseThumbnailResponse.
    using BatchListCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, std::string> const& itemsJson)>;
    void getThumbnailsBatch(std::vector<int> const& levelIds, BatchListCallback callback);

    // reorder thumbs (admin only)
    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, GenericCallback callback);

    // info thumb
    void getThumbnailInfo(int levelId, GenericCallback callback);

    // upload suggestion
    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // upload update
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // download suggestion
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // download update
    void downloadUpdate(int levelId, DownloadCallback callback);
    // download reported
    void downloadReported(int levelId, DownloadCallback callback);

    // upload profile img
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // upload profile gif (mod/admin/donator)
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // upload profile mp4 video (mod/admin)
    void uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);
    // download profile
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);
    // batch check: ask the server which accounts have a profile and return their configs
    void batchCheckProfiles(std::vector<int> const& accountIDs, GenericCallback callback);
    // download from url (validates image magic bytes)
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    // download from url without validating magic bytes (for audio, etc.)
    void downloadFromUrlRaw(std::string const& url, DownloadCallback callback);

    // validate a URL is safe to download (prevents SSRF)
    static bool isUrlSafe(std::string const& url);

    // upload profile picture (profileimg)
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    // upload profile gif (profileimg)
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // download profile picture (profileimg)
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    // upload profile config
    void uploadProfileConfig(int accountID, std::string const& jsonConfig, GenericCallback callback);
    // download profile config
    void downloadProfileConfig(int accountID, GenericCallback callback);

    // custom badge (emote used as a profile badge)
    void uploadCustomBadge(int accountID, std::string const& emoteName, GenericCallback callback);
    void downloadCustomBadge(int accountID, GenericCallback callback);
    void deleteCustomBadge(int accountID, GenericCallback callback);
    // batch badge: download badges for multiple accounts in 1 request
    void downloadCustomBadgeBatch(std::vector<int> const& accountIDs, GenericCallback callback);

    // download thumb (respects priority setting)
    void downloadThumbnail(int levelId, DownloadCallback callback);
    void downloadThumbnail(int levelId, bool isGif, DownloadCallback callback);

    // Batch downloads
    // Batch download result: id -> bytes (empty on failure).
    struct BatchItem {
        bool ok = false;
        std::string format;          // "webp" / "png" / "gif" / "mp4" / "jpg"
        std::vector<uint8_t> data;   // binary content (base64-decoded)
    };
    using BatchDownloadCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, BatchItem> const& items)>;

    // download level thumbnails in one request (cap 40 ids).
    void downloadThumbnailsBatch(std::vector<int> const& levelIds, BatchDownloadCallback callback);
    // download profile banners in one request (cap 40 accountIDs).
    void downloadProfileBackgroundsBatch(std::vector<int> const& accountIDs, BatchDownloadCallback callback);
    // download profile images (avatars) in one request (cap 40 accountIDs).
    void downloadProfileImgsBatch(std::vector<int> const& accountIDs, BatchDownloadCallback callback);
    
    // thumb exists?
    void checkThumbnailExists(int levelId, CheckCallback callback);

    // thumbnail confirmed missing on server (CDN + Worker both failed)
    bool isThumbnailNotFound(int levelId) const;
    void clearThumbnailNotFound(int levelId);
    
    // is moderator?
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // is moderator by accountid (safer)
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);

    // reports
    void submitReport(int levelId, std::string const& username, std::string const& note, GenericCallback callback);

    // ban list
    void getBanList(BanListCallback callback);

    // ban user
    void banUser(std::string const& username, std::string const& reason, BanUserCallback callback);
    // unban
    void unbanUser(std::string const& username, BanUserCallback callback);

    // list moderators
    void getModerators(ModeratorsListCallback callback);

    // top creators and top thumbnails
    void getTopCreators(GenericCallback callback);
    void getTopThumbnails(GenericCallback callback);
    void getUserUploads(std::string const& username, GenericCallback callback);
    
    // votes
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, GenericCallback callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, GenericCallback callback);

    // generic get/post
    void get(std::string const& endpoint, GenericCallback callback);
    void post(std::string const& endpoint, std::string const& data, GenericCallback callback);
    // authenticated post (includes X-Mod-Code for privileged operations)
    void postWithAuth(std::string const& endpoint, std::string const& data, GenericCallback callback);
    // post without X-Mod-Code (forces alternative backend validation)
    void postWithoutModCode(std::string const& endpoint, std::string const& data, GenericCallback callback);

    // whitelist
    void getWhitelist(std::string const& type, GenericCallback callback);
    void addToWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback);
    void removeFromWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback);

    // pet shop
    void getPetShopList(GenericCallback callback);
    void downloadPetShopItem(std::string const& itemId, std::string const& format,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback);
    void uploadPetShopItem(std::string const& name, std::string const& creator,
        std::vector<uint8_t> const& imageData, std::string const& format,
        UploadCallback callback);

    // profile stats — thumbnail upload count for any user
    void getProfileStats(int accountID, GenericCallback callback);

    // profile bundle — single request for mod status + badge + stats + music config
    void downloadProfileBundle(int accountID, std::string const& username, GenericCallback callback);

    // manifest cache — stores CDN URLs fetched from /api/manifest to bypass Worker
    struct ManifestEntry {
        std::string format;         // "webp", "png", "gif", etc.
        std::string cdnUrl;         // Bunny CDN Pull Zone URL (Paimbnails.b-cdn.net/...)
        std::string version;        // revision/version token
        std::string id;             // thumbnail id
        std::string revisionToken;  // server-computed token for staleness detection
        int64_t cachedAt = 0;       // epoch seconds when this entry was cached
    };

    // Batch init: single request at startup combining moderator + manifest + featured
    struct InitResult {
        bool isModerator = false;
        bool isAdmin = false;
        bool isVip = false;
        std::string newModCode;
        bool gdVerificationFailed = false;
        std::string dailyJson;
        std::string weeklyJson;
        std::string cdnBaseUrl;
        // manifest entries are applied directly to m_manifestCache
    };
    using InitCallback = geode::CopyableFunction<void(bool success, InitResult const& result)>;
    void fetchInit(std::string const& username, int accountID, std::vector<int> const& levelIds, InitCallback callback);

    // Batch ratings: get ratings for multiple levels in one request
    struct BatchRatingEntry {
        float average = 0.f;
        int count = 0;
        int userVote = 0;
    };
    using BatchRatingsCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, BatchRatingEntry> const& ratings)>;
    void fetchBatchRatings(std::vector<int> const& levelIds, std::string const& username,
        std::unordered_map<int, std::string> const& thumbnailIds, BatchRatingsCallback callback);

    // Discovery: combines top-creators + top-thumbnails + latest-uploads + featured
    using DiscoveryCallback = geode::CopyableFunction<void(bool success, std::string const& json)>;
    void fetchDiscovery(int creatorsLimit, int thumbnailsLimit, int uploadsLimit, DiscoveryCallback callback);

    // Queue summary: all queue categories counts + preview items in one request (moderators)
    using QueueSummaryCallback = geode::CopyableFunction<void(bool success, std::string const& json)>;
    void fetchQueueSummary(std::string const& username, int accountID, int previewCount, QueueSummaryCallback callback);

    // Batch profile bundle: fetch bundles for multiple accounts in one request
    using BatchBundleCallback = geode::CopyableFunction<void(bool success, std::string const& json)>;
    void fetchBatchProfileBundle(std::vector<std::pair<int, std::string>> const& accounts, BatchBundleCallback callback);

    // CDN Pull Zone base URL — public, no auth needed (e.g. "https://Paimbnails.b-cdn.net")
    std::string m_cdnBaseURL;

    // Worker exhaustion tracking — when CF Worker quota (100k/day) is hit,
    // fallback to CDN Pull Zone for all read requests.
    // Only marked exhausted after 3+ consecutive 503/429 failures so a transient
    // Worker cold-start doesn't disable the system. Auto-resets after 30s or on success.
    std::atomic<bool> m_workerExhausted{false};
    std::atomic<int64_t> m_exhaustedAt{0};
    std::atomic<int> m_consecutiveWorkerFailures{0};
    static constexpr int64_t EXHAUSTED_RECOVERY_SECONDS = 30; // retry Worker after 30s
    static constexpr int EXHAUSTION_THRESHOLD = 3; // consecutive failures before marking exhausted

    void fetchManifest(std::vector<int> const& levelIds, std::function<void(bool)> callback);
    std::optional<ManifestEntry> getManifestEntry(int levelId);
    void removeManifestEntry(int levelId);
    void removeExistsEntry(int levelId);
    std::vector<int> updateManifestFromJson(std::string const& json);

    // disk persistence for manifest cache
    void saveManifestToDisk();
    void loadManifestFromDisk();

private:
    HttpClient();
    ~HttpClient() = default;
    
    HttpClient(HttpClient const&) = delete;
    HttpClient& operator=(HttpClient const&) = delete;

    std::string m_serverURL;
    std::string m_forumServerURL;
    std::string m_apiKey;
    std::string m_modCode;
    
    // exists cache to avoid spamming
    struct ExistsCacheEntry {
        bool exists;
        time_t timestamp;
    };
    std::map<int, ExistsCacheEntry> m_existsCache;
    mutable std::mutex m_existsCacheMutex;
    static constexpr int EXISTS_CACHE_DURATION = 30; // 30 sec

    // manifest cache — CDN URLs indexed by levelId
    std::unordered_map<int, ManifestEntry> m_manifestCache;
    std::mutex m_manifestMutex;
    static constexpr size_t MAX_MANIFEST_ENTRIES = 5000;
    static constexpr int64_t MANIFEST_ENTRY_TTL = 48 * 60 * 60; // 48 hours in seconds
    std::shared_ptr<std::atomic<bool>> m_callbackGate;

    // Manifest fetch circuit breaker
    // Coalesces concurrent fetchManifest calls into a single request,
    // and backs off on 429 to avoid hammering the server.
    bool m_manifestFetchInFlight = false;
    std::vector<std::function<void(bool)>> m_manifestPendingCallbacks;
    std::mutex m_manifestFetchMutex;
    std::chrono::steady_clock::time_point m_manifestCooldownUntil{};
    static constexpr int MANIFEST_COOLDOWN_SECONDS = 30; // min backoff if server doesn't send retryAfter
    bool isManifestCooldownActive() const;
    void setManifestCooldown(int retryAfterSeconds);

    bool isWorkerExhausted();
    void markWorkerExhausted();

    // in-flight download dedup — coalesce concurrent downloadThumbnail calls
    // for the same levelId into a single network request
    std::unordered_map<int, std::vector<DownloadCallback>> m_inflightDownloads;
    std::mutex m_inflightMutex;
    void resolveInflight(int levelId, bool success, std::vector<uint8_t> const& data);

    // Thumbnails the server confirmed don't exist (CDN + Worker both failed).
    // Used in ThumbnailLoader to avoid infinite retries every 2s. Short TTL (5 min)
    // since we can't distinguish a real 404 from a transient network error here;
    // ThumbnailCache::markNotFound() handles real 404s persistently per session.
    mutable std::unordered_map<int, std::chrono::steady_clock::time_point> m_notFoundCache;
    mutable std::mutex m_notFoundMutex;
    static constexpr int NOT_FOUND_TTL_SECONDS = 5 * 60; // 5 min — quick recovery if it was transient
    void markThumbnailNotFound(int levelId) const;

    // in-flight moderator check dedup — coalesce concurrent checkModeratorAccount calls
    // for the same username into a single network request
    std::unordered_map<std::string, std::vector<ModeratorCallback>> m_inflightModChecks;
    std::mutex m_inflightModMutex;
    void resolveModCheckInflight(std::string const& key, bool isMod, bool isAdmin);

    // request async
    void performRequest(
        std::string const& url,
        std::string const& method,
        std::string const& postData,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::string const&)> callback,
        bool includeStoredModCode = true
    );
    
    // binary download (not to string)
    void performBinaryRequest(
        std::string const& url,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback,
        int timeoutSeconds = 15,
        bool includeModCode = false
    );

    // file upload
    void performUpload(
        std::string const& url,
        std::string const& fieldName,
        std::string const& filename,
        std::vector<uint8_t> const& data,
        std::vector<std::pair<std::string, std::string>> const& formFields,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::string const&)> callback,
        std::string const& contentType = "image/png"
    );
};
