#pragma once

#include <Geode/Geode.hpp>
#include "../video/VideoPlayer.hpp"
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <deque>
#include <chrono>
#include <atomic>

/**
 * VideoThumbnailSprite — CCSprite wrapper around VideoPlayer.
 *
 * Autoplay, muted, infinite loop. Integrates with Cocos2d-x scene graph.
 * Uses VideoPlayer's pre-allocated texture (glTexSubImage2D, zero alloc).
 * Suitable for LevelCell, GJScoreCell, ProfilePage, LayerBackgroundManager, etc.
 */
class VideoThumbnailSprite : public cocos2d::CCSprite {
public:
    using FrameReadyCallback = std::function<void(VideoThumbnailSprite*)>;

    // Create from local file path
    static VideoThumbnailSprite* create(std::string const& filePath);

    // Create from raw MP4 data (writes to temp file, then plays)
    static VideoThumbnailSprite* createFromData(std::vector<uint8_t> const& data, std::string const& cacheKey);

    // Check if a cacheKey has a cached temp file on disk
    static bool isCached(std::string const& cacheKey);

    // Recreate a VideoThumbnailSprite from a previously cached temp file
    static VideoThumbnailSprite* createFromCache(std::string const& cacheKey);

    // Async create from URL — downloads mp4 then creates on main thread
    using AsyncCallback = std::function<void(VideoThumbnailSprite*)>;
    static void createAsync(std::string const& url, std::string const& cacheKey, AsyncCallback callback);

    void play();
    void pause();
    void stop();

    void setLoop(bool loop);
    void setVolume(float v);
    bool isPlaying() const;
    bool hasVisibleFrame() const;
    cocos2d::CCSize getVideoSize() const;
    void setOnFirstVisibleFrame(FrameReadyCallback callback);

    // Cleanup all cached temp files (call on shutdown)
    static void clearCache();

    // Remove cached video files for a specific level
    static void removeForLevel(int levelID);

    void onEnter() override;
    void onExit() override;

    std::string const& getCacheKey() const { return m_cacheKey; }

protected:
    virtual ~VideoThumbnailSprite();
    void update(float dt) override;

private:
    struct PendingCreateCallback {
        std::string cacheKey;
        AsyncCallback callback;
    };

    struct DownloadRequest {
        std::string key;
        std::string url;
        std::string localPath;
        std::vector<PendingCreateCallback> callbacks;
        bool started = false;
    };

    struct CreateJob {
        std::string requestKey;
        std::string cacheKey;
        std::string localPath;
        AsyncCallback callback;
    };

    bool initWithPlayer(std::unique_ptr<paimon::video::VideoPlayer> player);

    static std::string makeRequestKey(std::string const& url, std::string const& cacheKey);
    static std::string getCachedPathLocked(std::string const& key);
    static void registerCachedPathLocked(std::string const& key, std::string const& path);
    static void pruneRecentFailuresLocked(std::chrono::steady_clock::time_point now);
    static void pumpAsyncQueues();
    static void handleDownloadResponse(std::string requestKey, geode::utils::web::WebResponse&& response);
    static void handleCreateJob(CreateJob job);
    void dispatchFirstVisibleFrame();

    std::unique_ptr<paimon::video::VideoPlayer> m_player;
    std::string m_cacheKey;
    bool m_playing = false;
    bool m_firstFrame = false;
    bool m_firstFrameSavedToCache = false;
    FrameReadyCallback m_onFirstVisibleFrame;

    // First-frame disk cache: saves/loads the first decoded frame as raw RGBA
    // for instant display on game restart (avoids waiting for full decode).
    static std::string getFirstFrameCachePath(std::string const& videoPath);
    void saveFirstFrameToCache();
    bool loadFirstFrameFromCache(std::string const& videoPath);

    // Temp file cache for data-based creation
    static std::mutex s_cacheMutex;
    static std::unordered_map<std::string, std::string> s_tempFiles; // cacheKey -> filePath
    static std::unordered_map<std::string, std::shared_ptr<DownloadRequest>> s_downloadRequests;
    static std::deque<std::string> s_downloadQueue;
    static std::deque<CreateJob> s_createQueue;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_recentFailures;
    static std::atomic<int> s_activeDownloads;
    static std::atomic<int> s_activeCreates;
    static std::atomic<bool> s_asyncShutdown;
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_CONCURRENT_DOWNLOADS = 1;
    static constexpr int MAX_CONCURRENT_CREATES = 1;
    static constexpr int MAX_CACHED_PLAYERS = 1;  // 1 cached player on mobile
#else
    static constexpr int MAX_CONCURRENT_DOWNLOADS = 2;
    static constexpr int MAX_CONCURRENT_CREATES = 1;
    static constexpr int MAX_CACHED_PLAYERS = 1;  // cache last player to speed up layer transitions
#endif
    static constexpr auto FAILED_REQUEST_TTL = std::chrono::minutes(2);
    static std::string getTempPath(std::string const& cacheKey);
    
    // VideoPlayer cache to avoid re-decoding when switching layers
    struct CachedPlayer {
        std::unique_ptr<paimon::video::VideoPlayer> player;
        std::string cacheKey;
        std::chrono::steady_clock::time_point lastUsed;
    };
    static std::mutex s_playerCacheMutex;
    static std::deque<CachedPlayer> s_playerCache;
    
    static std::unique_ptr<paimon::video::VideoPlayer> getCachedPlayer(std::string const& cacheKey);
    static void returnPlayerToCache(std::string const& cacheKey, std::unique_ptr<paimon::video::VideoPlayer> player);
    static void clearPlayerCache();
};
