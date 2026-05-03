#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/function.hpp>
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
    // Geode v5: CopyableFunction reemplaza std::function — misma semantica copiable,
    // pero usa std23::function internamente para mejor compatibilidad ABI.
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

    std::string getForumServerURL() const { return m_forumServerURL; }
    void setForumServerURL(std::string const& url);

    static std::string encodeQueryParam(std::string const& value);

    // mod code
    std::string getModCode() const { return m_modCode; }
    void setModCode(std::string const& code);

    // limpia tasks
    void cleanTasks();


    // sube thumb png
    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);

    // sube gif (mod/admin)
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);

    // sube video mp4 (mod/admin)
    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);

    // lista thumbs
    void getThumbnails(int levelId, GenericCallback callback);

    // reordenar thumbs (solo admin)
    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, GenericCallback callback);

    // info thumb
    void getThumbnailInfo(int levelId, GenericCallback callback);

    // sube suggestion
    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // sube update
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // descarga suggestion
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // descarga update
    void downloadUpdate(int levelId, DownloadCallback callback);
    // descarga reportada
    void downloadReported(int levelId, DownloadCallback callback);

    // sube profile img
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // sube profile gif (mod/admin/donator)
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // sube profile video mp4 (mod/admin)
    void uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback);
    // descarga profile
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);
    // batch check: pregunta al servidor cuales cuentas tienen perfil y devuelve sus configs
    void batchCheckProfiles(std::vector<int> const& accountIDs, GenericCallback callback);
    // descarga desde url (valida magic bytes de imagen)
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    // descarga desde url sin validar magic bytes (para audio, etc.)
    void downloadFromUrlRaw(std::string const& url, DownloadCallback callback);

    // valida que una URL sea segura para descargar (previene SSRF)
    static bool isUrlSafe(std::string const& url);

    // sube imagen de perfil (profileimg)
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    // sube gif de perfil (profileimg)
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // descarga imagen de perfil (profileimg)
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    // sube config profile
    void uploadProfileConfig(int accountID, std::string const& jsonConfig, GenericCallback callback);
    // descarga config profile
    void downloadProfileConfig(int accountID, GenericCallback callback);

    // custom badge (emote usado como badge de perfil)
    void uploadCustomBadge(int accountID, std::string const& emoteName, GenericCallback callback);
    void downloadCustomBadge(int accountID, GenericCallback callback);
    void deleteCustomBadge(int accountID, GenericCallback callback);
    // batch badge: descarga badges de multiples cuentas en 1 request
    void downloadCustomBadgeBatch(std::vector<int> const& accountIDs, GenericCallback callback);

    // descarga thumb (respeta setting priority)
    void downloadThumbnail(int levelId, DownloadCallback callback);
    void downloadThumbnail(int levelId, bool isGif, DownloadCallback callback);
    
    // existe thumb?
    void checkThumbnailExists(int levelId, CheckCallback callback);
    
    // es mod?
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // es mod por accountid (mas seguro)
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);

    // reportes
    void submitReport(int levelId, std::string const& username, std::string const& note, GenericCallback callback);

    // lista baneados
    void getBanList(BanListCallback callback);

    // banear user
    void banUser(std::string const& username, std::string const& reason, BanUserCallback callback);
    // unban
    void unbanUser(std::string const& username, BanUserCallback callback);

    // lista mods
    void getModerators(ModeratorsListCallback callback);

    // top creators y top thumbnails
    void getTopCreators(GenericCallback callback);
    void getTopThumbnails(GenericCallback callback);
    
    // votos
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, GenericCallback callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, GenericCallback callback);

    // get/post generico
    void get(std::string const& endpoint, GenericCallback callback);
    void post(std::string const& endpoint, std::string const& data, GenericCallback callback);
    // post autenticado (incluye X-Mod-Code para operaciones privilegiadas)
    void postWithAuth(std::string const& endpoint, std::string const& data, GenericCallback callback);
    // post sin X-Mod-Code (fuerza validacion alternativa en backend)
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

    // CDN Pull Zone base URL — public, no auth needed (e.g. "https://Paimbnails.b-cdn.net")
    std::string m_cdnBaseURL;

    // Worker exhaustion tracking — when CF Worker quota (100k/day) is hit,
    // fallback to CDN Pull Zone for all read requests
    std::atomic<bool> m_workerExhausted{false};
    int64_t m_exhaustedAt = 0;
    static constexpr int64_t EXHAUSTED_RECOVERY_SECONDS = 3600; // retry Worker after 1h

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
    
    // cache exists pa no spamear
    struct ExistsCacheEntry {
        bool exists;
        time_t timestamp;
    };
    std::map<int, ExistsCacheEntry> m_existsCache;
    static constexpr int EXISTS_CACHE_DURATION = 30; // 30 sec

    // manifest cache — CDN URLs indexed by levelId
    std::unordered_map<int, ManifestEntry> m_manifestCache;
    std::mutex m_manifestMutex;
    static constexpr size_t MAX_MANIFEST_ENTRIES = 5000;
    static constexpr int64_t MANIFEST_ENTRY_TTL = 48 * 60 * 60; // 48 hours in seconds
    std::shared_ptr<std::atomic<bool>> m_callbackGate;

    // ── Manifest fetch circuit breaker ────────────────────────────────
    // Coalesces concurrent fetchManifest calls into a single request,
    // and backs off on 429 to avoid hammering the server.
    bool m_manifestFetchInFlight = false;
    std::vector<std::function<void(bool)>> m_manifestPendingCallbacks;
    std::mutex m_manifestFetchMutex;
    std::chrono::steady_clock::time_point m_manifestCooldownUntil{};
    static constexpr int MANIFEST_COOLDOWN_SECONDS = 30; // min backoff if server doesn't send retryAfter
    bool isManifestCooldownActive() const;
    void setManifestCooldown(int retryAfterSeconds);

    // Check if Worker quota is exhausted (auto-recovers after EXHAUSTED_RECOVERY_SECONDS)
    bool isWorkerExhausted();
    // Mark Worker as exhausted (called on HTTP 429 or CF quota errors)
    void markWorkerExhausted();

    // in-flight download dedup — coalesce concurrent downloadThumbnail calls
    // for the same levelId into a single network request
    std::unordered_map<int, std::vector<DownloadCallback>> m_inflightDownloads;
    std::mutex m_inflightMutex;
    void resolveInflight(int levelId, bool success, std::vector<uint8_t> const& data);

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
    
    // descarga binary (sin a string)
    void performBinaryRequest(
        std::string const& url,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback,
        int timeoutSeconds = 15,
        bool includeModCode = false
    );

    // sube archivo
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
