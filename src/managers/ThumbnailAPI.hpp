#pragma once

#include <Geode/Geode.hpp>
#include "../utils/HttpClient.hpp"
#include "../utils/ThumbnailTypes.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"

struct ProfileConfig;
#include "../features/thumbnails/services/ThumbnailSubmissionService.hpp"
#include "../features/moderation/services/ModerationService.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include <string>
#include <optional>
#include <chrono>

// Compatibility facade; delegates to the per-domain services. Prefer the services directly in new code.
class ThumbnailAPI {
public:
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using ExistsCallback = geode::CopyableFunction<void(bool exists)>;
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using QueueCallback = geode::CopyableFunction<void(bool success, std::vector<PendingItem> const& items)>;
    using ActionCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = geode::CopyableFunction<void(bool success, std::vector<ThumbnailInfo> const& thumbnails)>;

    static ThumbnailAPI& get() {
        static ThumbnailAPI instance;
        return instance;
    }

    void getThumbnails(int levelId, ThumbnailListCallback callback);

    void getThumbnailInfo(int levelId, ActionCallback callback);

    std::string getThumbnailURL(int levelId);

    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta = "");

    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback, std::string const& levelMeta = ""); // mod/admin only

    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback, std::string const& levelMeta = ""); // mod/admin only

    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta = ""); // non-moderator -> /suggestions
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback, std::string const& levelMeta = ""); // non-moderator -> /updates
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    void uploadProfileVideo(int accountID, std::vector<uint8_t> const& mp4Data, std::string const& username, UploadCallback callback); // mod/admin
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);
    // which accounts have a profile + configs
    using BatchCheckCallback = ProfileImageService::BatchCheckCallback;
    void batchCheckProfiles(std::vector<int> const& accountIDs, BatchCheckCallback callback);

    // profileimg = avatar, distinct from the profile background above
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    void downloadFromUrlData(std::string const& url, DownloadDataCallback callback); // raw bytes, not a texture


    
    void uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback);
    void downloadProfileConfig(int accountID, geode::CopyableFunction<void(bool success, ProfileConfig const& config)> callback);

    void downloadSuggestion(int levelId, DownloadCallback callback);
    void downloadSuggestionImage(std::string const& filename, DownloadCallback callback);
    void downloadUpdate(int levelId, DownloadCallback callback);
    // server's current (reported) thumbnail
    void downloadReported(int levelId, DownloadCallback callback);
    // pending profile background, for the moderator verification center
    void downloadPendingProfile(int accountID, DownloadCallback callback);


    // voting system
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, geode::CopyableFunction<void(bool success, float average, int count, int userVote)> callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, ActionCallback callback);

    void downloadThumbnail(int levelId, DownloadCallback callback, bool isGif = false);
    
    void checkExists(int levelId, ExistsCallback callback);
    
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // moderator check requiring accountID > 0
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);
    
    void checkUserStatus(std::string const& username, ModeratorCallback callback);

    void getThumbnail(int levelId, DownloadCallback callback);
    
    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    
    void claimQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& type = "");
    
    void acceptQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& targetFilename = "", std::string const& type = "", bool acceptAll = false);

    void rejectQueueItem(int levelId, PendingCategory category, std::string const& username, std::string const& reason, ActionCallback callback, std::string const& type = "", std::string const& targetFilename = "");
    
    void submitReport(int levelId, std::string const& username, std::string const& note, ActionCallback callback);
    
    void addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);
    
    void removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);

    void getTopCreators(ActionCallback callback);

    void getTopThumbnails(ActionCallback callback);
    
    void getUserUploads(std::string const& username, ActionCallback callback);
    
    void deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username, int accountID, ActionCallback callback);
    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, ActionCallback callback);
    
    void setServerEnabled(bool enabled);

    cocos2d::CCTexture2D* webpToTexture(std::vector<uint8_t> const& webpData);

private:
    ThumbnailAPI();
    ~ThumbnailAPI() = default;
    
    ThumbnailAPI(const ThumbnailAPI&) = delete;
    ThumbnailAPI& operator=(const ThumbnailAPI&) = delete;

    // residual state kept for compatibility; the real logic lives in the services
    bool m_serverEnabled = true;
    int m_uploadCount = 0;
};
