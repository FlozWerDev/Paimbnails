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
    // Geode v5 uses CopyableFunction for ABI compatibility.
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data, int width, int height)>;
    using CheckCallback = geode::CopyableFunction<void(bool exists)>;
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using GenericCallback = geode::CopyableFunction<void(bool success, std::string const& response)>;
    using BanListCallback = geode::CopyableFunction<void(bool success, std::string const& jsonData)>;
    using BanUserCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using ModeratorsListCallback = geode::CopyableFunction<void(bool success, std::vector<std::string> const& moderators)>;

    // Full role set; newer flags default false for older servers.
    struct UserRoleFlags {
        bool isMod = false;
        bool isAdmin = false;
        bool isVip = false;
        bool isHelper = false;
        bool isIdea = false;
    };
    using UserRolesCallback = geode::CopyableFunction<void(UserRoleFlags const& flags, bool ok)>;

    static HttpClient& get() {
        static HttpClient instance;
        return instance;
    }

    std::string getServerURL() const { return m_serverURL; }
    void setServerURL(std::string const& url);
    std::string buildAssetURL(std::string const& path, std::string const& defaultFolder = "") const;
    std::string getApiKey() const { return m_apiKey; }

    std::string getCDNBaseURL() const { return m_cdnBaseURL; }

    std::string getForumServerURL() const { return m_forumServerURL; }
    void setForumServerURL(std::string const& url);

    static std::string encodeQueryParam(std::string const& value);

    std::string getModCode() const { return m_modCode; }
    void setModCode(std::string const& code);
    void startModCodeSetup(std::string const& username, int accountID, GenericCallback callback);
    void completeModCodeSetup(std::string const& challengeToken, GenericCallback callback);

    void cleanTasks(bool allowNewRequests = true);


    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta = "");

    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback, std::string const& levelMeta = "");

    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback, std::string const& levelMeta = "");

    void getThumbnails(int levelId, GenericCallback callback);

    using BatchListCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, std::string> const& itemsJson)>;
    void getThumbnailsBatch(std::vector<int> const& levelIds, BatchListCallback callback);

    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, GenericCallback callback);

    void getThumbnailInfo(int levelId, GenericCallback callback);

    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta = "");
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta = "");
    void downloadSuggestion(int levelId, DownloadCallback callback);
    void downloadUpdate(int levelId, DownloadCallback callback);
    void downloadReported(int levelId, DownloadCallback callback);

    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    void uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);
    void batchCheckProfiles(std::vector<int> const& accountIDs, GenericCallback callback);
    // Image download with signature validation.
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    // Binary download for non-image assets.
    void downloadFromUrlRaw(std::string const& url, DownloadCallback callback);

    // Reject unsafe download URLs.
    static bool isUrlSafe(std::string const& url);

    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    void uploadProfileConfig(int accountID, std::string const& jsonConfig, GenericCallback callback);
    void downloadProfileConfig(int accountID, GenericCallback callback);

    void uploadCustomBadge(int accountID, std::string const& emoteName, GenericCallback callback);
    void downloadCustomBadge(int accountID, GenericCallback callback);
    void deleteCustomBadge(int accountID, GenericCallback callback);
    void downloadCustomBadgeBatch(std::vector<int> const& accountIDs, GenericCallback callback);

    void downloadThumbnail(int levelId, DownloadCallback callback);
    void downloadThumbnail(int levelId, bool isGif, DownloadCallback callback);

    // Batch result keyed by level/account ID; data is empty on failure.
    struct BatchItem {
        bool ok = false;
        std::string format;
        std::vector<uint8_t> data;
    };
    using BatchDownloadCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, BatchItem> const& items)>;

    void downloadThumbnailsBatch(std::vector<int> const& levelIds, BatchDownloadCallback callback);
    void downloadProfileBackgroundsBatch(std::vector<int> const& accountIDs, BatchDownloadCallback callback);
    void downloadProfileImgsBatch(std::vector<int> const& accountIDs, BatchDownloadCallback callback);
    
    void checkThumbnailExists(int levelId, CheckCallback callback);

    bool isThumbnailNotFound(int levelId) const;
    void clearThumbnailNotFound(int levelId);
    
    void checkModerator(std::string const& username, ModeratorCallback callback);
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);
    void checkUserRoles(std::string const& username, int accountID, UserRolesCallback callback);

    void submitReport(int levelId, std::string const& username, std::string const& note, GenericCallback callback);

    void getBanList(BanListCallback callback);

    // Startup ban check for the current user.
    using BanCheckCallback = geode::CopyableFunction<void(bool ok, bool banned, std::string const& reason)>;
    void checkBanned(BanCheckCallback callback);

    void banUser(std::string const& username, std::string const& reason, BanUserCallback callback);
    void unbanUser(std::string const& username, BanUserCallback callback);

    void getModerators(ModeratorsListCallback callback);

    void addRoleMember(std::string const& role, std::string const& username, int accountID, GenericCallback callback);
    void removeRoleMember(std::string const& role, std::string const& username, int accountID, GenericCallback callback);
    void getRoleMembers(std::string const& role, GenericCallback callback);

    void getTopCreators(GenericCallback callback);
    void getTopThumbnails(GenericCallback callback);
    void getUserUploads(std::string const& username, GenericCallback callback);
    
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, GenericCallback callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, GenericCallback callback);

    void get(std::string const& endpoint, GenericCallback callback);
    void post(std::string const& endpoint, std::string const& data, GenericCallback callback);
    void postWithAuth(std::string const& endpoint, std::string const& data, GenericCallback callback);
    void postWithoutModCode(std::string const& endpoint, std::string const& data, GenericCallback callback);

    void getWhitelist(std::string const& type, GenericCallback callback);
    void addToWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback);
    void removeFromWhitelist(std::string const& targetUsername, std::string const& type, GenericCallback callback);

    void getPetShopList(GenericCallback callback);
    void downloadPetShopItem(std::string const& itemId, std::string const& format,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback);
    void uploadPetShopItem(std::string const& name, std::string const& creator,
        std::vector<uint8_t> const& imageData, std::string const& format,
        UploadCallback callback);

    void getProfileStats(int accountID, GenericCallback callback);

    void downloadProfileBundle(int accountID, std::string const& username, GenericCallback callback);

    // Cached CDN URLs from /api/manifest, used to bypass the Worker.
    struct ManifestEntry {
        std::string format;
        std::string cdnUrl;
        std::string version;
        std::string id;
        std::string revisionToken;
        int64_t cachedAt = 0;
    };

    struct InitResult {
        bool isModerator = false;
        bool isAdmin = false;
        bool isVip = false;
        std::string newModCode;
        bool gdVerificationFailed = false;
        std::string dailyJson;
        std::string weeklyJson;
        std::string cdnBaseUrl;
    };
    using InitCallback = geode::CopyableFunction<void(bool success, InitResult const& result)>;
    void fetchInit(std::string const& username, int accountID, std::vector<int> const& levelIds, InitCallback callback);

    struct BatchRatingEntry {
        float average = 0.f;
        int count = 0;
        int userVote = 0;
    };
    using BatchRatingsCallback = geode::CopyableFunction<void(bool success, std::unordered_map<int, BatchRatingEntry> const& ratings)>;
    void fetchBatchRatings(std::vector<int> const& levelIds, std::string const& username,
        std::unordered_map<int, std::string> const& thumbnailIds, BatchRatingsCallback callback);

    using DiscoveryCallback = geode::CopyableFunction<void(bool success, std::string const& json)>;
    void fetchDiscovery(int creatorsLimit, int thumbnailsLimit, int uploadsLimit, DiscoveryCallback callback);

    using QueueSummaryCallback = geode::CopyableFunction<void(bool success, std::string const& json)>;
    void fetchQueueSummary(std::string const& username, int accountID, int previewCount, QueueSummaryCallback callback);

    using BatchBundleCallback = geode::CopyableFunction<void(bool success, std::string const& json)>;
    void fetchBatchProfileBundle(std::vector<std::pair<int, std::string>> const& accounts, BatchBundleCallback callback);

    // Public CDN Pull Zone used when the Worker is exhausted.
    std::string m_cdnBaseURL;

    // After repeated 503/429s, route reads through the CDN for 30 seconds.
    std::atomic<bool> m_workerExhausted{false};
    std::atomic<int64_t> m_exhaustedAt{0};
    std::atomic<int> m_consecutiveWorkerFailures{0};
    static constexpr int64_t EXHAUSTED_RECOVERY_SECONDS = 30;
    static constexpr int EXHAUSTION_THRESHOLD = 3;

    void fetchManifest(std::vector<int> const& levelIds, std::function<void(bool)> callback);
    std::optional<ManifestEntry> getManifestEntry(int levelId);
    void removeManifestEntry(int levelId);
    void removeExistsEntry(int levelId);
    std::vector<int> updateManifestFromJson(std::string const& json);

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
    
    struct ExistsCacheEntry {
        bool exists;
        time_t timestamp;
    };
    std::map<int, ExistsCacheEntry> m_existsCache;
    mutable std::mutex m_existsCacheMutex;
    static constexpr int EXISTS_CACHE_DURATION = 30;

    // CDN manifest entries indexed by level ID.
    std::unordered_map<int, ManifestEntry> m_manifestCache;
    std::mutex m_manifestMutex;
    static constexpr size_t MAX_MANIFEST_ENTRIES = 5000;
    static constexpr int64_t MANIFEST_ENTRY_TTL = 48 * 60 * 60;
    std::shared_ptr<std::atomic<bool>> m_callbackGate;

    // Coalesces manifest fetches and backs off after 429.
    bool m_manifestFetchInFlight = false;
    std::vector<std::function<void(bool)>> m_manifestPendingCallbacks;
    std::mutex m_manifestFetchMutex;
    std::chrono::steady_clock::time_point m_manifestCooldownUntil{};
    static constexpr int MANIFEST_COOLDOWN_SECONDS = 30;
    bool isManifestCooldownActive() const;
    void setManifestCooldown(int retryAfterSeconds);

    bool isWorkerExhausted();
    void markWorkerExhausted();

    // Coalesce concurrent downloads for the same level ID.
    std::unordered_map<int, std::vector<DownloadCallback>> m_inflightDownloads;
    std::mutex m_inflightMutex;
    void resolveInflight(int levelId, bool success, std::vector<uint8_t> const& data);

    // Short-lived negative cache for downloads that failed through both endpoints;
    // avoids retry loops while allowing transient errors to recover.
    mutable std::unordered_map<int, std::chrono::steady_clock::time_point> m_notFoundCache;
    mutable std::mutex m_notFoundMutex;
    static constexpr int NOT_FOUND_TTL_SECONDS = 5 * 60;
    void markThumbnailNotFound(int levelId) const;

    // Coalesce concurrent moderator checks for the same username.
    std::unordered_map<std::string, std::vector<ModeratorCallback>> m_inflightModChecks;
    std::mutex m_inflightModMutex;
    void resolveModCheckInflight(std::string const& key, bool isMod, bool isAdmin);

    void performRequest(
        std::string const& url,
        std::string const& method,
        std::string const& postData,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::string const&)> callback,
        bool includeStoredModCode = true
    );
    
    void performBinaryRequest(
        std::string const& url,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback,
        int timeoutSeconds = 15,
        bool includeModCode = false
    );

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
