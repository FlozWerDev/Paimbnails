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

// CCSprite wrapper around VideoPlayer with muted autoplay and zero-allocation
// frame uploads.
class VideoThumbnailSprite : public cocos2d::CCSprite {
public:
    using FrameReadyCallback = std::function<void(VideoThumbnailSprite*)>;

    static VideoThumbnailSprite* create(std::string const& filePath);

    static VideoThumbnailSprite* createFromData(std::vector<uint8_t> const& data, std::string const& cacheKey);

    static bool isCached(std::string const& cacheKey);

    // Returns a cached MP4 path, or empty when missing. Thread-safe.
    static std::string getCachedPathForKey(std::string const& cacheKey);

    static VideoThumbnailSprite* createFromCache(std::string const& cacheKey);

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

    static void clearCache();

    // Release GL players before reload; keep temp files for lazy recreation.
    static void onGLContextReload();

    static void removeForLevel(int levelID);

    // Remove a cache when a profile switches away from video.
    static void removeForCacheKey(std::string const& cacheKey);

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
    // Opens the decoder off the main thread, then hands the player to finishCreateJob.
    static void handleCreateJob(CreateJob job);
    // Main thread: wraps the player in a sprite and settles the queue bookkeeping.
    static void finishCreateJob(CreateJob job, std::unique_ptr<paimon::video::VideoPlayer> player);
    void dispatchFirstVisibleFrame();

    std::unique_ptr<paimon::video::VideoPlayer> m_player;
    std::string m_cacheKey;
    bool m_playing = false;
    bool m_firstFrame = false;
    bool m_firstFrameSavedToCache = false;
    FrameReadyCallback m_onFirstVisibleFrame;

    // Raw RGBA first-frame cache for instant display after restart.
    static std::string getFirstFrameCachePath(std::string const& videoPath);
    void saveFirstFrameToCache();
    bool loadFirstFrameFromCache(std::string const& videoPath);

    static std::mutex s_cacheMutex;
    static std::unordered_map<std::string, std::string> s_tempFiles; // cacheKey → path
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
    static constexpr int MAX_CACHED_PLAYERS = 2;
    static constexpr int  MAX_TEMP_FILES = 30;
    static constexpr size_t MAX_TEMP_FILES_BYTES = 80ULL * 1024 * 1024;
    static constexpr size_t MAX_FIRST_FRAME_BYTES = 30ULL * 1024 * 1024;
#else
    static constexpr int MAX_CONCURRENT_DOWNLOADS = 2;
    // Disk hits are decoder opens, so allow two in parallel.
    static constexpr int MAX_CONCURRENT_CREATES = 2;
    static constexpr int MAX_CACHED_PLAYERS = 3;
    static constexpr int  MAX_TEMP_FILES = 80;
    static constexpr size_t MAX_TEMP_FILES_BYTES = 256ULL * 1024 * 1024;
    static constexpr size_t MAX_FIRST_FRAME_BYTES = 96ULL * 1024 * 1024;
#endif
    static constexpr auto FAILED_REQUEST_TTL = std::chrono::minutes(2);
    static std::string getTempPath(std::string const& cacheKey);

    // Enforce the MP4 count/size budget; caller holds s_cacheMutex.
    static void enforceTempFilesBudgetLocked();

    // Remove unreferenced runtime cache files once per session; do not hold locks.
    static void cleanupOrphanedDiskFiles();
    
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

    // Bound concurrent decoders in multi-sprite scenes; excess sprites pause
    // until an active slot is released.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_ACTIVE_SPRITES = 3;
#else
    static constexpr int MAX_ACTIVE_SPRITES = 6;
#endif

    static std::mutex s_activeSpritesMutex;
    static int s_activeSpriteCount;

    // Keep total decode frame budget bounded as visible sprite count grows.
    static int adaptiveSpriteFPS(int activeCount);

    // Claim one active-sprite slot, if available.
    bool tryAcquireActiveSlot();
    void releaseActiveSlot();

    bool m_holdsActiveSlot = false;

    // Pause decoding after this sprite stays off-screen long enough.
    float m_offscreenAccumulator = 0.0f;
    static constexpr float kOffscreenPauseThreshold = 0.5f; // seconds
};
