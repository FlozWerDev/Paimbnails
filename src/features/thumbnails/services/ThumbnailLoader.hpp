#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/function.hpp>
#include <string>
#include <deque>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include "../../../utils/FormatDetect.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../utils/ThreadPool.hpp"
#include "CacheModels.hpp"
#include "ThumbnailCache.hpp"

class ThumbnailLoader {
public:
    using LoadCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* texture, bool success)>;
    using InvalidationCallback = geode::CopyableFunction<void(int levelID)>;

    static constexpr int PriorityBootstrap = 1;
    static constexpr int PriorityPredictivePrefetch = 2;
    static constexpr int PriorityVisiblePrefetch = 3;
    static constexpr int PriorityHero = 15;
    static constexpr int PriorityVisibleCell = 10;

    static ThumbnailLoader& get();

    enum class Quality {
        Small,
        High
    };

    void requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority = 0, bool isGif = false, Quality quality = Quality::Small);
    void prefetchLevelAssets(int levelID, int priority = 0);
    void prefetchLevels(std::vector<int> const& levelIDs, int priority = 0);
    
    void requestUrlLoad(std::string const& url, LoadCallback callback, int priority = 0);
    void requestUrlBatchLoad(std::vector<std::string> const& urls, LoadCallback perUrlCallback, int priority = 0);
    bool isUrlLoaded(std::string const& url) const;
    void cancelUrlLoad(std::string const& url);

    void cancelLoad(int levelID, bool isGif = false);
    
    bool isLoaded(int levelID, bool isGif = false) const;
    bool isPending(int levelID, bool isGif = false) const;
    bool isFailed(int levelID, bool isGif = false) const;
    bool isNotFound(int levelID, bool isGif = false) const;

    // Synchronous RAM-only fast path; no disk or callback queue on miss.
    cocos2d::CCTexture2D* tryGetCachedTexture(int levelID, bool isGif = false);

    void clearCache();
    void clearFailedCache();
    // Release RAM textures and pending old-context callbacks before GL reload;
    // disk and worker pools remain intact.
    void onGLContextReload();
    void invalidateLevel(int levelID, bool isGif = false);

    void updateRemoteRevision(int levelID, std::string const& revisionToken);

    int getInvalidationVersion(int levelID) const;
    int addInvalidationListener(InvalidationCallback callback);
    void removeInvalidationListener(int listenerId);

    void setMaxConcurrentTasks(int max);
    /// Do not call from ThumbnailLoader's constructor (reentry into get()).
    void applyConcurrentDownloadsSetting();
    void setBatchMode(bool enabled) { m_batchMode = enabled; }

    int getActiveTaskCount() const { return m_activeTaskCount; }
    int getMaxConcurrentTasks() const { return m_maxConcurrentTasks; }

    static bool isTextureSane(cocos2d::CCTexture2D* tex);
    std::filesystem::path getCachePath(int levelID, bool isGif = false);

    // Strip cache-busting params for a stable key.
    static std::string normalizeUrlKey(std::string const& url);
    
    void updateSessionCache(int levelID, cocos2d::CCTexture2D* texture);
    bool hasGIFData(int levelID) const;
    void cleanup();
    void clearDiskCache();
    void clearPendingQueue();

    void flushManifest();

    paimon::cache::CacheStats& stats() { return paimon::cache::ThumbnailCache::get().stats(); }
    paimon::cache::CacheStats const& stats() const { return paimon::cache::ThumbnailCache::get().stats(); }



private:
    ThumbnailLoader();
    ~ThumbnailLoader();

    struct Task {
        int levelID;
        std::string fileName;
        std::string url;
        std::string urlCacheKey;
        int priority;
        std::vector<LoadCallback> callbacks;
        bool running = false;
        bool cancelled = false;
        bool isUrlTask = false;
        bool wasNotFound = false;
        Quality quality = Quality::Small;
        std::chrono::steady_clock::time_point startedAt{};
    };

    std::unordered_map<int, std::shared_ptr<Task>> m_tasks;
    std::unordered_map<std::string, std::shared_ptr<Task>> m_urlTasks;
    std::multimap<int, int, std::greater<int>> m_priorityQueue;
    std::atomic<int> m_activeTaskCount{0};
    std::atomic<int> m_activeUrlTaskCount{0};
    int m_maxConcurrentTasks = 8;
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    int m_maxConcurrentUrlTasks = 4;
#else
    int m_maxConcurrentUrlTasks = 8;
#endif
    mutable std::shared_mutex m_queueMutex;

    std::unordered_set<int> m_gifLevels;
    mutable std::shared_mutex m_gifLevelsMutex;

    std::unordered_map<int, std::string> m_remoteRevisions;

    std::unordered_map<int, InvalidationCallback> m_invalidationListeners;
    int m_nextInvalidationListenerId = 1;

    std::unordered_set<int> m_revisionCheckedThisSession;
    void triggerBackgroundRevisionCheck(int levelID);

    // TTL for manifest requests, including levels absent from the manifest.
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_manifestRequestedAt;

    bool m_batchMode = false;

    // Global cooldown after a burst of failures.
    std::atomic<int> m_recentFailureCount{0};
    std::chrono::steady_clock::time_point m_failureWindowStart{};
    std::chrono::steady_clock::time_point m_globalCooldownUntil{};
    std::mutex m_cooldownMutex;
    static constexpr int FAILURE_THRESHOLD = 8;
    static constexpr int FAILURE_WINDOW_SECONDS = 6;
    static constexpr int COOLDOWN_SECONDS = 3;
    void recordDownloadFailure();
    bool isGlobalCooldownActive() const;

    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_cleanupStarted{false};
    std::atomic<bool> m_cleanupFinished{false};

    struct PendingCallback {
        LoadCallback callback;
        geode::Ref<cocos2d::CCTexture2D> texture;
        bool success;
        int levelID = 0;
        int capturedVersion = 0;
    };
    std::vector<PendingCallback> m_pendingCallbacks;
    std::mutex m_pendingMutex;
    std::atomic<bool> m_drainScheduled{false};
    // Timestamp used to re-arm a drain stalled over 100 ms.
    std::atomic<int64_t> m_drainScheduledAtUs{0};
    // Per-drain cap; the frame budget remains the real limiter.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_CALLBACKS_PER_FRAME = 8;
#else
    static constexpr int MAX_CALLBACKS_PER_FRAME = 16;
#endif
    void enqueuePendingCallback(LoadCallback cb, cocos2d::CCTexture2D* tex, bool success, int levelID = 0);
    void drainPendingCallbacks();
    void scheduleDrain();

    // Pending upload uses CCImage or raw RGBA pixels.
    struct PendingUpload {
        std::shared_ptr<Task> task;
        cocos2d::CCImage* image = nullptr;
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        int realID = 0;
        bool fallbackToDownload = false;
        int originalWidth = 0;
        int originalHeight = 0;
    };
    std::vector<PendingUpload> m_pendingUploads;
    std::mutex m_uploadMutex;
    std::atomic<bool> m_uploadDrainScheduled{false};
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_UPLOADS_PER_FRAME = 12;
    static constexpr int64_t UPLOAD_FRAME_BUDGET_US = 3500;
#else
    static constexpr int MAX_UPLOADS_PER_FRAME = 8;
    static constexpr int64_t UPLOAD_FRAME_BUDGET_US = 2500;
#endif
    // Maximum dimensions for RAM-cached thumbnails.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int RAM_CACHE_SMALL_MAX_DIM = 512;
    static constexpr int RAM_CACHE_HIGH_MAX_DIM = 1024;
    static constexpr int URL_CACHE_MAX_DIM = 512;
#else
    static constexpr int RAM_CACHE_SMALL_MAX_DIM = 768;
    static constexpr int RAM_CACHE_HIGH_MAX_DIM = 1920;
    static constexpr int URL_CACHE_MAX_DIM = 1280;
#endif
    int decodeMaxDimForQuality(Quality quality) const;
    void enqueuePendingUpload(PendingUpload upload);
    void drainPendingUploads();
    void scheduleUploadDrain();

    bool beginCleanup(char const* reason);
    void logShutdownSnapshot(char const* reason);
    void processQueue();
    void startTask(std::shared_ptr<Task> task);
    void finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success, int origW = 0, int origH = 0);
    
    void initDiskCache();
    
    void workerLoadFromDisk(std::shared_ptr<Task> task);
    void workerDownload(std::shared_ptr<Task> task);
    void processDownloadedData(std::shared_ptr<Task> task, std::vector<uint8_t> data, int realID);
    void workerUrlDownload(std::shared_ptr<Task> task);

    struct BatchPending {
        std::shared_ptr<Task> task;
        std::shared_ptr<std::atomic<int>> retryCount;
    };
    std::vector<BatchPending> m_batchPendingDownloads;
    std::mutex m_batchPendingMutex;
    std::atomic<bool> m_batchFlushScheduled{false};
    static constexpr int BATCH_FLUSH_THRESHOLD = 40;
    static constexpr int BATCH_FLUSH_DELAY_MS = 50;
    void scheduleBatchFlush();
    void flushBatchDownloads();
    void enqueueBatchDownload(std::shared_ptr<Task> task, std::shared_ptr<std::atomic<int>> retryCount);

    void processUrlQueue();
    void spawnDisk(std::function<void()> job);
    void spawnCpu(std::function<void()> job);
    void waitBackgroundWorkers();

    struct DecodeResult {
        std::vector<uint8_t> pixels;        // Preferred raw RGBA.
        cocos2d::CCImage* image = nullptr;  // Fallback; caller owns it.
        int width = 0;
        int height = 0;
        int originalWidth = 0;
        int originalHeight = 0;
        bool isGif = false;
        bool success = false;
        int64_t decodeTimeUs = 0;
    };
    DecodeResult decodeImageData(std::vector<uint8_t> const& data, int realID, int maxDim = 0);

    std::unique_ptr<paimon::ThreadPool> m_diskPool;
    std::unique_ptr<paimon::ThreadPool> m_cpuPool;

    static constexpr auto MAX_DISK_CACHE_AGE = std::chrono::hours(24 * 21);
    static constexpr auto FAILED_CACHE_TTL = std::chrono::minutes(10);
};
