#pragma once

#include <Geode/Geode.hpp>
#include <arc/future/Future.hpp>
#include "../../../utils/ThumbnailTypes.hpp"
#include "LocalThumbs.hpp"
#include <string>
#include <chrono>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <vector>

class ThumbnailTransportClient {
public:
    using UploadCallback      = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback    = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback= geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using ExistsCallback      = geode::CopyableFunction<void(bool exists)>;
    using ActionCallback      = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using RatingCallback      = geode::CopyableFunction<void(bool success, float average, int count, int userVote)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = geode::CopyableFunction<void(bool success, std::vector<ThumbnailInfo> const& thumbnails)>;

    static ThumbnailTransportClient& get() {
        static ThumbnailTransportClient instance;
        return instance;
    }

    void setServerEnabled(bool enabled) { m_serverEnabled = enabled; }

    void getThumbnails(int levelId, ThumbnailListCallback callback, bool forceRefresh = false);
    arc::Future<ThumbnailGalleryResult> fetchThumbnailsFuture(int levelId, bool forceRefresh = false);
    void getThumbnailInfo(int levelId, ActionCallback callback);
    std::string getThumbnailURL(int levelId);
    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData,
                         std::string const& username, UploadCallback callback,
                         std::string const& levelMeta = "");
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData,
                   std::string const& username, UploadCallback callback,
                   std::string const& levelMeta = "");
    void uploadVideo(int levelId, std::vector<uint8_t> const& mp4Data,
                     std::string const& username, UploadCallback callback,
                     std::string const& levelMeta = "");
    void downloadThumbnail(int levelId, DownloadCallback callback, bool isGif = false);
    void checkExists(int levelId, ExistsCallback callback);
    void deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username, int accountID,
                         ActionCallback callback);
    void reorderThumbnails(int levelId, std::vector<std::string> const& thumbnailIds, ActionCallback callback);
    void getRating(int levelId, std::string const& username,
                   std::string const& thumbnailId,
                   RatingCallback callback);
    void submitVote(int levelId, int stars, std::string const& username,
                    std::string const& thumbnailId, ActionCallback callback);
    void getThumbnail(int levelId, DownloadCallback callback);
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    void downloadFromUrlData(std::string const& url, DownloadDataCallback callback);
    arc::Future<ThumbnailTextureResult> downloadFromUrlFuture(std::string url);
    arc::Future<ThumbnailDataResult> downloadFromUrlDataFuture(std::string url);
    arc::Future<ThumbnailTextureResult> downloadThumbnailFuture(int levelId, bool isGif = false);
    arc::Future<ThumbnailTextureResult> getThumbnailFuture(int levelId);
    arc::Future<bool> checkExistsFuture(int levelId);
    void getTopCreators(ActionCallback callback);
    void getTopThumbnails(ActionCallback callback);
    void getUserUploads(std::string const& username, ActionCallback callback);
    void invalidateGalleryMetadata(int levelId);

    bool hasGalleryMetadataCached(int levelId);

    // Soporta PNG, JPG, WebP, GIF, QOI, JPEG XL via ImagePlus; JPEG fallback via CCImage
    static cocos2d::CCTexture2D* webpToTexture(std::vector<uint8_t> const& data);
    static cocos2d::CCTexture2D* bytesToTexture(std::vector<uint8_t> const& data);
    static bool isGIFData(std::vector<uint8_t> const& data);

private:
    struct GalleryMetadataEntry {
        std::vector<ThumbnailInfo> thumbnails;
        std::string revisionToken;
        std::chrono::steady_clock::time_point fetchedAt{};
    };

    struct RatingCacheEntry {
        float average = 0.f;
        int count = 0;
        int userVote = 0;
        std::chrono::steady_clock::time_point fetchedAt{};
    };

    ThumbnailTransportClient() = default;
    ThumbnailTransportClient(ThumbnailTransportClient const&) = delete;
    ThumbnailTransportClient& operator=(ThumbnailTransportClient const&) = delete;

    bool m_serverEnabled = true;
    int  m_uploadCount   = 0;
    std::unordered_map<int, GalleryMetadataEntry> m_galleryCache;
    std::unordered_map<int, std::vector<ThumbnailListCallback>> m_galleryInFlight;
    std::unordered_map<int, uint64_t> m_galleryGenerations;
    std::mutex m_galleryMutex;

    static constexpr auto UPLOAD_GUARD_TIMEOUT = std::chrono::minutes(2);
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_uploadsInFlight;
    std::mutex m_uploadMutex;
    bool beginUpload(int levelId);
    void finishUpload(int levelId);

    // Batch coalescing of getThumbnails into a single /api/thumbnails/list-batch
    // over a short window to avoid one request per level.
    static constexpr int BATCH_LIST_FLUSH_DELAY_MS = 50;
    static constexpr size_t BATCH_LIST_MAX_IDS = 40;
    std::vector<int> m_batchListPending;
    std::mutex m_batchListMutex;
    std::atomic<bool> m_batchListFlushScheduled{false};
    void scheduleBatchListFlush();
    void flushBatchList();

    std::unordered_map<std::string, RatingCacheEntry> m_ratingCache;
    std::unordered_map<std::string, std::vector<RatingCallback>> m_ratingInFlight;
    std::unordered_map<std::string, uint64_t> m_ratingGenerations;
    std::mutex m_ratingMutex;
    static constexpr int RATING_CACHE_TTL_SECONDS = 15;

    void invalidateRatingCache(int levelId, std::string const& thumbnailId = "");

    cocos2d::CCTexture2D* loadFromLocal(int levelId);
};
