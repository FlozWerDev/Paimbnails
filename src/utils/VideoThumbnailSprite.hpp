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

    // Returns the on-disk path for a cached video by key, or empty string if
    // the cache is missing.  Used by features that need to read the source
    // .mp4 directly (e.g. extracting audio for "Audio Video" profile mode).
    // Thread-safe: takes the same internal mutex used by isCached().
    static std::string getCachedPathForKey(std::string const& cacheKey);

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

    // Remove cached video file for a specific cacheKey. Used when the user
    // explicitly switches a profile away from video to gradient/none so the
    // local cache no longer overrides the server-side config.
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
    // Disk cache hard caps (mobile is tighter on space).
    static constexpr int  MAX_TEMP_FILES = 30;
    static constexpr size_t MAX_TEMP_FILES_BYTES = 80ULL * 1024 * 1024;   // 80 MB
    static constexpr size_t MAX_FIRST_FRAME_BYTES = 30ULL * 1024 * 1024;  // 30 MB
#else
    static constexpr int MAX_CONCURRENT_DOWNLOADS = 2;
    static constexpr int MAX_CONCURRENT_CREATES = 1;
    static constexpr int MAX_CACHED_PLAYERS = 1;  // cache last player to speed up layer transitions
    // Disk cache hard caps for desktop.
    static constexpr int  MAX_TEMP_FILES = 80;
    static constexpr size_t MAX_TEMP_FILES_BYTES = 256ULL * 1024 * 1024;  // 256 MB
    static constexpr size_t MAX_FIRST_FRAME_BYTES = 96ULL * 1024 * 1024;  // 96 MB
#endif
    static constexpr auto FAILED_REQUEST_TTL = std::chrono::minutes(2);
    static std::string getTempPath(std::string const& cacheKey);

    // Enforce MAX_TEMP_FILES / MAX_TEMP_FILES_BYTES on the on-disk MP4 cache.
    // Caller must hold s_cacheMutex.
    static void enforceTempFilesBudgetLocked();

    // Best-effort cleanup of orphaned ff_*.raw and video_*.mp4 files in the
    // runtime directory that aren't referenced by s_tempFiles. Runs at most
    // once per session to avoid I/O thrash. Caller must NOT hold mutexes.
    static void cleanupOrphanedDiskFiles();
    
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

    // Active-sprite budget (multi-sprite scenes like LevelCell lists)
    //
    // Hard cap on how many VideoThumbnailSprites may decode simultaneously.
    // When a sprite enters update() and would push the count over this cap,
    // it pauses its decoder until enough older sprites release the slot.
    // This avoids 20+ concurrent decoders in a full level list.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_ACTIVE_SPRITES = 3;
#else
    static constexpr int MAX_ACTIVE_SPRITES = 6;
#endif

    static std::mutex s_activeSpritesMutex;
    static int s_activeSpriteCount;

    // Returns the per-sprite target FPS given how many sprites are currently
    // decoding. Mirrors the LayerBackgroundManager adaptive-FPS scheme so
    // total frame budget stays bounded as more sprites become visible.
    static int adaptiveSpriteFPS(int activeCount);

    // Try to claim one slot of the active-sprite budget. Returns true on
    // success; false if the budget is full.
    bool tryAcquireActiveSlot();
    void releaseActiveSlot();

    // Whether this sprite currently holds an active slot.
    bool m_holdsActiveSlot = false;

    // Tracks how long the sprite has been off-screen so we can pause its
    // decoder rather than just skipping the frame upload.
    float m_offscreenAccumulator = 0.0f;
    static constexpr float kOffscreenPauseThreshold = 0.5f; // seconds
};
