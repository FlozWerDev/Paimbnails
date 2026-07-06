#include "ThumbnailLoader.hpp"
#include "ThumbnailTransportClient.hpp"
#include "LocalThumbs.hpp"
#include "LevelColors.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../core/MainLevels.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/Constants.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/DominantColors.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/Debug.hpp"
#include "../../../utils/FormatDetect.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/FrameBudget.hpp"
#include "../../../utils/stb_image.h"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <cmath>
#include <algorithm>
#include <tuple>
#include <chrono>
#include <string_view>

using namespace geode::prelude;

static bool isVolatileParam(std::string_view key) {
    return key == "_pv" || key == "_cb" || key == "ts" || key == "v" || key == "t";
}

std::string ThumbnailLoader::normalizeUrlKey(std::string const& url) {
    size_t q = url.find('?');
    if (q == std::string::npos) return url;

    std::string_view base(url.data(), q);
    std::string_view query(url.data() + q + 1, url.size() - q - 1);

    std::string out;
    out.reserve(url.size());
    out.append(base);

    bool first = true;
    size_t start = 0;
    while (start < query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string_view::npos) end = query.size();

        std::string_view pair(query.data() + start, end - start);
        size_t eq = pair.find('=');
        std::string_view key = (eq == std::string_view::npos) ? pair : std::string_view(pair.data(), eq);
        if (!isVolatileParam(key)) {
            if (first) { out.push_back('?'); first = false; }
            else { out.push_back('&'); }
            out.append(pair);
        }
        start = end + 1;
    }
    return out;
}

ThumbnailLoader& ThumbnailLoader::get() {
    // Singleton intentionally leaked to avoid atexit races with late worker callbacks.
    static auto* instance = new ThumbnailLoader();
    return *instance;
}

ThumbnailLoader::ThumbnailLoader() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_maxConcurrentTasks = 4;
    m_diskPool = std::make_unique<paimon::ThreadPool>(2, "PaimonDiskIO");
    m_cpuPool  = std::make_unique<paimon::ThreadPool>(2, "PaimonCPU");
#else
    unsigned hw = std::thread::hardware_concurrency();
    int cpuThreads  = static_cast<int>(std::clamp<unsigned>(hw ? (hw + 3u) / 4u : 2u, 2u, 6u));
    int diskThreads = static_cast<int>(std::clamp<unsigned>(hw ? hw / 6u : 2u, 1u, 3u));
    m_maxConcurrentTasks = std::min(4, std::max(2, cpuThreads / 3));
    m_diskPool = std::make_unique<paimon::ThreadPool>(diskThreads, "PaimonDiskIO");
    m_cpuPool  = std::make_unique<paimon::ThreadPool>(cpuThreads,  "PaimonCPU");
#endif
    log::info("[ThumbnailLoader] constructor: maxConcurrent={}", m_maxConcurrentTasks);
    initDiskCache();
}

ThumbnailLoader::~ThumbnailLoader() {
    log::info("[ThumbnailLoader] destructor: shutting down");
    if (m_cleanupFinished.load(std::memory_order_acquire)) {
        log::info("[ThumbnailLoader] cleanup already completed during runtime shutdown");
        return;
    }
    m_shuttingDown = true;
    waitBackgroundWorkers();

    if (paimon::cache::ThumbnailCache::isAlive()) {
        paimon::cache::ThumbnailCache::get().takeAllTextures();
    }

    // Clear task callbacks before member destruction: they hold WeakRef<PaimonLevelCell>
    // whose destructor touches CCPoolManager, which may already be dead.
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.clear();
    }
    for (auto& [id, task] : m_tasks) {
        if (task) task->callbacks.clear();
    }
    m_tasks.clear();
    for (auto& [url, task] : m_urlTasks) {
        if (task) task->callbacks.clear();
    }
    m_urlTasks.clear();
    m_invalidationListeners.clear();
}

bool ThumbnailLoader::beginCleanup(char const* reason) {
    bool expected = false;
    if (!m_cleanupStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        log::info("[ThumbnailLoader] beginCleanup: already started ({})", reason);
        return false;
    }
    return true;
}

void ThumbnailLoader::logShutdownSnapshot(char const* reason) {
    auto& cache = paimon::cache::ThumbnailCache::get();
    log::info("[ThumbnailLoader] shutdown snapshot ({}): ram={} disk={} tasks={} urlTasks={} active={}",
        reason, cache.ramEntryCount(), cache.diskEntryCount(),
        m_tasks.size(), m_urlTasks.size(), m_activeTaskCount.load());
}

void ThumbnailLoader::initDiskCache() {
    spawnDisk([this]() {
        paimon::quality::migrateLegacyCache();

        auto path = paimon::quality::cacheDir();
        PaimonDebug::log("[ThumbnailLoader] iniciando cache de disco en: {}", geode::utils::string::pathToString(path));
        
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            std::filesystem::create_directories(path, ec);
            PaimonDebug::log("[ThumbnailLoader] carpeta de cache creada");
        }

        if (m_shuttingDown.load(std::memory_order_acquire)) {
            PaimonDebug::log("[ThumbnailLoader] shutdown durante init cache, abortando");
            return;
        }

        auto& cache = paimon::cache::ThumbnailCache::get();
        cache.loadDiskIndex();

        PaimonDebug::log("[ThumbnailLoader] cache de disco lista. entradas: {}",
            cache.diskEntryCount());
    });
}

void ThumbnailLoader::enqueuePendingCallback(LoadCallback cb, cocos2d::CCTexture2D* tex, bool success, int levelID) {
    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return;
    }
    // Drop null callbacks: empty std23::function in the drain caused 0xFFFF... reads (crash).
    if (!cb) {
        return;
    }
    int version = 0;
    if (levelID > 0) {
        version = paimon::cache::ThumbnailCache::get().getInvalidationVersion(levelID);
    }
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.emplace_back(PendingCallback{std::move(cb), tex, success, levelID, version});
    }
    scheduleDrain();
}

void ThumbnailLoader::scheduleDrain() {
    int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    bool expected = false;
    if (!m_drainScheduled.compare_exchange_strong(expected, true)) {
        // Detect stale drains: if scheduled >100ms ago and never ran, re-arm or callbacks hang forever.
        int64_t scheduledAt = m_drainScheduledAtUs.load(std::memory_order_relaxed);
        if (scheduledAt > 0 && (nowUs - scheduledAt) > 100'000) {
            bool stillScheduled = true;
            if (m_drainScheduled.compare_exchange_strong(stillScheduled, false)) {
                log::warn("[ThumbnailLoader] drain stall detectado tras {}us — re-armando", nowUs - scheduledAt);
                scheduleDrain();
            }
        }
        return;
    }

    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        m_drainScheduled.store(false, std::memory_order_release);
        m_drainScheduledAtUs.store(0, std::memory_order_relaxed);
        return;
    }

    m_drainScheduledAtUs.store(nowUs, std::memory_order_relaxed);
    // WARNING: captures `this`. ThumbnailLoader is intentionally leaked (never destroyed)
    // so this lambda is always safe. Do not copy this pattern for non-singletons.
    Loader::get()->queueInMainThread([this]() {
        drainPendingCallbacks();
    });
}

void ThumbnailLoader::drainPendingCallbacks() {
    m_drainScheduled.store(false, std::memory_order_release);
    m_drainScheduledAtUs.store(0, std::memory_order_relaxed);
    if (m_shuttingDown.load(std::memory_order_acquire)) return;

    auto* director = cocos2d::CCDirector::get();
    if (!director) return;
    float dt = director->getDeltaTime();
    bool isFrameLag = dt > 0.016f;
    int frameLagSkip = isFrameLag ? 2 : 0;

    std::vector<PendingCallback> batch;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        
        // Do NOT coalesce callbacks by levelID: multiple cells can show the same level
        // and each needs its own callback (coalescing left some cells without a thumbnail).
        
        size_t pendingSize = m_pendingCallbacks.size();
        if (pendingSize == 0) {
            return;
        }

        size_t count = static_cast<size_t>(std::min<int>(MAX_CALLBACKS_PER_FRAME,
                                                          static_cast<int>(pendingSize)));
        if (isFrameLag && count > 0) {
            size_t reduced = count > static_cast<size_t>(frameLagSkip)
                                ? count - static_cast<size_t>(frameLagSkip)
                                : 1;
            count = std::max<size_t>(1, reduced);
        }
        if (count > pendingSize) count = pendingSize;
        if (count == 0) return;

        // Move first count elements in one pass: avoids assign+erase double-destroy
        // that can leave 0xFFFF... pointers with std23::function/geode::Ref.
        batch.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(std::move(m_pendingCallbacks[i]));
        }
        m_pendingCallbacks.erase(
            m_pendingCallbacks.begin(),
            m_pendingCallbacks.begin() + static_cast<std::ptrdiff_t>(count));
    }

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    constexpr int64_t CALLBACK_FRAME_BUDGET_US = 2000;
    constexpr int64_t FRAME_TARGET_US = 16666;
#else
    constexpr int64_t CALLBACK_FRAME_BUDGET_US = 2000;
    constexpr int64_t FRAME_TARGET_US = 16666;
#endif
    
    int64_t effectiveBudgetUs = isFrameLag ? 1500 : CALLBACK_FRAME_BUDGET_US;
    effectiveBudgetUs = std::min<int64_t>(effectiveBudgetUs,
        std::max<int64_t>(1, paimon::framebudget::remainingUs()));

    auto frameStart = std::chrono::steady_clock::now();
    std::vector<PendingCallback> deferred;

    for (size_t idx = 0; idx < batch.size(); ++idx) {
        auto& pc = batch[idx];
        // On stale invalidation version, fire callback with failure=false so the cell
        // resets and retries (not `continue`, which left ghost thumbnails until a refresh).
        bool versionStale = pc.levelID > 0 &&
            pc.capturedVersion != paimon::cache::ThumbnailCache::get().getInvalidationVersion(pc.levelID);

        if (idx > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameStart).count();
            if (elapsed >= effectiveBudgetUs) {
                for (size_t j = idx; j < batch.size(); ++j) {
                    deferred.push_back(std::move(batch[j]));
                }
                break;
            }
        }

        if (pc.callback) {
            // Move callback locally and wrap in try/catch: isolates exceptions and
            // avoids double-free of an already-invoked function.
            LoadCallback localCb = std::move(pc.callback);
            geode::Ref<cocos2d::CCTexture2D> localTex = pc.texture;
            bool localSuccess = pc.success;
            try {
                if (versionStale) {
                    localCb(nullptr, false);
                } else {
                    localCb(localTex.data(), localSuccess);
                }
            } catch (std::exception const& e) {
                log::warn("[ThumbnailLoader] callback throwed: {}", e.what());
            } catch (...) {
                log::warn("[ThumbnailLoader] callback throwed unknown exception");
            }
        }
    }

    paimon::framebudget::consume(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - frameStart).count());

    if (!deferred.empty()) {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.insert(m_pendingCallbacks.begin(),
            std::make_move_iterator(deferred.begin()),
            std::make_move_iterator(deferred.end()));
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pendingCallbacks.empty()) {
            scheduleDrain();
        }
    }

    static std::atomic<int64_t> s_lastPurgeTickUs{0};
    size_t pendingLeft = 0;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        pendingLeft = m_pendingCallbacks.size();
    }
    if (pendingLeft <= static_cast<size_t>(MAX_CALLBACKS_PER_FRAME * 2)) {
        auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t lastTick = s_lastPurgeTickUs.load(std::memory_order_relaxed);
        if (nowUs - lastTick > 2'000'000 &&
            s_lastPurgeTickUs.compare_exchange_strong(lastTick, nowUs,
                                                       std::memory_order_relaxed)) {
            auto& cache = paimon::cache::ThumbnailCache::get();
            cache.purgeUnusedTextures();
            cache.purgeExpiredFailed();
        }
    }
}

void ThumbnailLoader::enqueuePendingUpload(PendingUpload upload) {
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.push_back(std::move(upload));
    }
    scheduleUploadDrain();
}

void ThumbnailLoader::scheduleUploadDrain() {
    bool expected = false;
    if (!m_uploadDrainScheduled.compare_exchange_strong(expected, true)) return;
    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        m_uploadDrainScheduled.store(false, std::memory_order_release);
        return;
    }
    // WARNING: captures `this`. ThumbnailLoader is intentionally leaked (never destroyed)
    // so this lambda is always safe. Do not copy this pattern for non-singletons.
    Loader::get()->queueInMainThread([this]() {
        drainPendingUploads();
    });
}

void ThumbnailLoader::drainPendingUploads() {
    m_uploadDrainScheduled.store(false, std::memory_order_release);
    if (m_shuttingDown.load(std::memory_order_acquire)) return;

    std::vector<PendingUpload> batch;
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        // Prioritize visible-cell uploads via partial_sort over the first
        // MAX_UPLOADS_PER_FRAME elements: O(n log k) instead of O(n log n).
        int count = std::min(MAX_UPLOADS_PER_FRAME, static_cast<int>(m_pendingUploads.size()));
        if (count > 0 && static_cast<int>(m_pendingUploads.size()) > count) {
            std::partial_sort(
                m_pendingUploads.begin(),
                m_pendingUploads.begin() + count,
                m_pendingUploads.end(),
                [](PendingUpload const& a, PendingUpload const& b) {
                    int pa = a.task ? a.task->priority : 0;
                    int pb = b.task ? b.task->priority : 0;
                    return pa > pb;
                });
        } else if (count > 1) {
            std::sort(m_pendingUploads.begin(), m_pendingUploads.begin() + count,
                [](PendingUpload const& a, PendingUpload const& b) {
                    int pa = a.task ? a.task->priority : 0;
                    int pb = b.task ? b.task->priority : 0;
                    return pa > pb;
                });
        }
        batch.assign(
            std::make_move_iterator(m_pendingUploads.begin()),
            std::make_move_iterator(m_pendingUploads.begin() + count)
        );
        m_pendingUploads.erase(m_pendingUploads.begin(), m_pendingUploads.begin() + count);
    }

    auto frameStart = std::chrono::steady_clock::now();
    int uploaded = 0;
    std::vector<PendingUpload> deferred;

    int64_t uploadBudgetUs = std::min<int64_t>(UPLOAD_FRAME_BUDGET_US,
        std::max<int64_t>(1, paimon::framebudget::remainingUs()));

    for (auto& pu : batch) {
        if (!pu.task || pu.task->cancelled) {
            if (pu.image) {
                pu.image->release();
                pu.image = nullptr;
            }
            finishTask(pu.task, nullptr, false);
            continue;
        }
        if (!pu.image && pu.pixels.empty()) {
            if (pu.fallbackToDownload) {
                workerDownload(pu.task);
            } else {
                finishTask(pu.task, nullptr, false);
            }
            continue;
        }

        bool isVisibleCell = pu.task && pu.task->priority >= PriorityVisibleCell;
        if (uploaded > 0 && !isVisibleCell) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameStart).count();
            if (elapsed >= uploadBudgetUs) {
                deferred.push_back(std::move(pu));
                continue;
            }
        }

        auto tex = new CCTexture2D();
        bool ok = false;

        if (pu.image) {
            ok = tex->initWithImage(pu.image);
            // CCImage is a CCObject: release() (not delete) to respect refcount.
            pu.image->release();
            pu.image = nullptr;
        } else if (!pu.pixels.empty() && pu.width > 0 && pu.height > 0) {
            ok = tex->initWithData(pu.pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
                                   pu.width, pu.height, CCSize((float)pu.width, (float)pu.height));
        }

        if (ok) {
            tex->autorelease();
            finishTask(pu.task, tex, true, pu.originalWidth, pu.originalHeight);
            uploaded++;
        } else {
            tex->release();
            if (pu.fallbackToDownload) {
                workerDownload(pu.task);
            } else {
                finishTask(pu.task, nullptr, false);
            }
        }
    }

    paimon::framebudget::consume(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - frameStart).count());

    if (!deferred.empty()) {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.insert(m_pendingUploads.begin(),
            std::make_move_iterator(deferred.begin()),
            std::make_move_iterator(deferred.end()));
    }

    if (uploaded > 0) {
        auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frameStart).count();
        PaimonDebug::log("[ThumbnailLoader] GPU upload: {} textures in {}us (budget={}us, deferred={})",
            uploaded, totalUs, UPLOAD_FRAME_BUDGET_US, deferred.size());
    }

    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        if (!m_pendingUploads.empty()) {
            scheduleUploadDrain();
        }
    }
}

void ThumbnailLoader::recordDownloadFailure() {
    std::lock_guard<std::mutex> lock(m_cooldownMutex);
    auto now = std::chrono::steady_clock::now();

    auto windowElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_failureWindowStart).count();
    if (windowElapsed >= FAILURE_WINDOW_SECONDS) {
        m_recentFailureCount.store(0, std::memory_order_relaxed);
        m_failureWindowStart = now;
    }

    int count = m_recentFailureCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count >= FAILURE_THRESHOLD) {
        m_globalCooldownUntil = now + std::chrono::seconds(COOLDOWN_SECONDS);
        m_recentFailureCount.store(0, std::memory_order_relaxed);
        m_failureWindowStart = now;
        log::warn("[ThumbnailLoader] {} fallos en {}s — cooldown global de {}s activado",
            count, FAILURE_WINDOW_SECONDS, COOLDOWN_SECONDS);
    }
}

bool ThumbnailLoader::isGlobalCooldownActive() const {
    auto now = std::chrono::steady_clock::now();
    return now < m_globalCooldownUntil;
}

void ThumbnailLoader::triggerBackgroundRevisionCheck(int levelID) {
    // Deprecated no-op: staleness now handled by manifest revisionToken comparison.
    (void)levelID;
}

void ThumbnailLoader::setMaxConcurrentTasks(int max) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_maxConcurrentTasks = std::max(1, std::min(24, max));
#else
    m_maxConcurrentTasks = std::max(1, std::min(64, max));
#endif
}

void ThumbnailLoader::applyConcurrentDownloadsSetting() {
    static bool applied = false;
    if (applied) return;
    applied = true;

    auto* mod = Mod::get();
    if (!mod) {
        log::warn("[ThumbnailLoader] applyConcurrentDownloadsSetting: Mod::get() null, keeping default");
        return;
    }

    int const maxDownloads = static_cast<int>(
        mod->getSettingValue<int64_t>("thumbnail-concurrent-downloads")
    );
    setMaxConcurrentTasks(maxDownloads);
    log::info(
        "[ThumbnailLoader] concurrent downloads from settings: {}",
        m_maxConcurrentTasks
    );
}

bool ThumbnailLoader::isLoaded(int levelID, bool isGif) const {
    return paimon::cache::ThumbnailCache::get().getFromRam(levelID, isGif).has_value();
}

cocos2d::CCTexture2D* ThumbnailLoader::tryGetCachedTexture(int levelID, bool isGif) {
    // Returns a raw RAM pointer without enqueuing; caller must Ref<> it to keep it alive past this frame.
    auto ramTex = paimon::cache::ThumbnailCache::get().getFromRam(levelID, isGif);
    if (!ramTex.has_value()) return nullptr;
    paimon::cache::ThumbnailCache::get().stats().ramHits.fetch_add(
        1, std::memory_order_relaxed);
    return ramTex.value();
}

bool ThumbnailLoader::isPending(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::shared_lock<std::shared_mutex> lock(m_queueMutex);
    return m_tasks.find(key) != m_tasks.end();
}

bool ThumbnailLoader::isFailed(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::string keyStr = paimon::cache::CacheKey::fromLegacy(key).toString();
    return paimon::cache::ThumbnailCache::get().isFailed(keyStr);
}

bool ThumbnailLoader::isNotFound(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::string keyStr = paimon::cache::CacheKey::fromLegacy(key).toString();
    return paimon::cache::ThumbnailCache::get().isNotFound(keyStr);
}

bool ThumbnailLoader::hasGIFData(int levelID) const {
    std::shared_lock<std::shared_mutex> lock(m_gifLevelsMutex);
    return m_gifLevels.find(levelID) != m_gifLevels.end();
}

int ThumbnailLoader::decodeMaxDimForQuality(Quality quality) const {
    return quality == Quality::High ? RAM_CACHE_HIGH_MAX_DIM : RAM_CACHE_SMALL_MAX_DIM;
}

std::filesystem::path ThumbnailLoader::getCachePath(int levelID, bool isGif) {
    return paimon::quality::thumbCachePath(levelID, isGif);
}

void ThumbnailLoader::requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority, bool isGif, Quality quality) {
    applyConcurrentDownloadsSetting();

    int key = isGif ? -levelID : levelID;
    PaimonDebug::log("[ThumbnailLoader] requestLoad: levelID={} key={} priority={} isGif={}", levelID, key, priority, isGif);

    auto& cache = paimon::cache::ThumbnailCache::get();
    int requestedMaxDim = decodeMaxDimForQuality(quality);

    auto ramTex = cache.getFromRam(levelID, isGif);
    if (ramTex.has_value() && cache.isRamEntrySuitable(levelID, isGif, requestedMaxDim)) {
        cache.stats().ramHits.fetch_add(1, std::memory_order_relaxed);

        // Throttle disk LRU touch to once/min per level: avoids m_manifest.mutex
        // contention with the background flush.
        // thread_local safe: requestLoad is only called from the main thread.
        static thread_local std::unordered_map<int, int64_t> s_lastTouchUs;
        int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (s_lastTouchUs.size() > 1024) s_lastTouchUs.clear();
        auto& lastTouch = s_lastTouchUs[levelID];
        if (nowUs - lastTouch > 60'000'000) {
            lastTouch = nowUs;
            cache.touchDiskAccess(std::abs(key), isGif);
        }

        PaimonDebug::log("[ThumbnailLoader] requestLoad: RAM cache hit for key={}", key);
        auto tex = ramTex.value();
        if (callback) {
            enqueuePendingCallback(std::move(callback), tex.data(), true, std::abs(key));
        }
        return;
    }
    cache.stats().ramMisses.fetch_add(1, std::memory_order_relaxed);

    std::string keyStr = paimon::cache::CacheKey::fromLegacy(key).toString();
    if (cache.isNotFound(keyStr)) {
        PaimonDebug::log("[ThumbnailLoader] requestLoad: not-found cache hit for key={}", key);
        if (callback) enqueuePendingCallback(callback, nullptr, false, std::abs(key));
        return;
    }

    if (cache.isFailed(keyStr)) {
        PaimonDebug::log("[ThumbnailLoader] requestLoad: failed cache hit for key={}", key);
        if (callback) enqueuePendingCallback(callback, nullptr, false, std::abs(key));
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_queueMutex);

    auto taskIt = m_tasks.find(key);
    if (taskIt != m_tasks.end()) {
        if (taskIt->second->cancelled) {
            taskIt->second->cancelled = false;
            PaimonDebug::log("[ThumbnailLoader] requestLoad: un-cancelling task for key={}", key);
            if (!taskIt->second->running) {
                m_priorityQueue.emplace(priority, key);
            }
        }
        PaimonDebug::log("[ThumbnailLoader] requestLoad: task already queued for key={}, appending callback", key);
        if (callback) taskIt->second->callbacks.push_back(callback);
        if (priority > taskIt->second->priority) {
            taskIt->second->priority = priority;
            if (!taskIt->second->running) {
                m_priorityQueue.emplace(priority, key);
            }
        }
        if (quality == Quality::High) {
            taskIt->second->quality = Quality::High;
        }
        return;
    }

    if (isGlobalCooldownActive() && priority < PriorityVisiblePrefetch) {
        PaimonDebug::log("[ThumbnailLoader] requestLoad: global cooldown active, deferring key={}", key);
        if (callback) enqueuePendingCallback(callback, nullptr, false, std::abs(key));
        return;
    }

    constexpr int MAX_PENDING_LOW_PRIORITY = 80;
    int pendingCount = static_cast<int>(m_tasks.size()) - m_activeTaskCount.load(std::memory_order_relaxed);
    if (priority <= PriorityVisiblePrefetch && pendingCount >= MAX_PENDING_LOW_PRIORITY) {
        PaimonDebug::log("[ThumbnailLoader] requestLoad: queue saturated ({} pending), dropping low-priority key={}", pendingCount, key);
        if (callback) enqueuePendingCallback(callback, nullptr, false, std::abs(key));
        return;
    }

    PaimonDebug::log("[ThumbnailLoader] requestLoad: creating new task for key={} priority={}", key, priority);
    auto task = std::make_shared<Task>();
    task->levelID = key;
    task->fileName = fileName;
    task->priority = priority;
    task->quality = quality;
    if (callback) task->callbacks.push_back(callback);

    m_tasks[key] = task;
    m_priorityQueue.emplace(priority, key);
    
    processQueue();
}

void ThumbnailLoader::prefetchLevelAssets(int levelID, int priority) {
    if (levelID <= 0) {
        return;
    }

    if (isLoaded(levelID) || isPending(levelID) || isFailed(levelID) || isNotFound(levelID)) {
        return;
    }

    this->requestLoad(levelID, fmt::format("{}.png", levelID), nullptr, priority);
}

void ThumbnailLoader::prefetchLevels(std::vector<int> const& levelIDs, int priority) {
    if (levelIDs.empty()) {
        return;
    }

    int const concurrent = std::max(2, m_maxConcurrentTasks);
    int maxPrefetch = (priority >= PriorityVisiblePrefetch)
        ? std::min(concurrent, 12)
        : std::min(std::max(2, concurrent / 2), 6);

    std::unordered_set<int> seen;
    seen.reserve(levelIDs.size());
    std::vector<int> manifestIds;
    manifestIds.reserve(std::min(levelIDs.size(), static_cast<size_t>(maxPrefetch)));
    int queued = 0;

    for (int levelID : levelIDs) {
        if (queued >= maxPrefetch) break;
        if (levelID <= 0 || !seen.insert(levelID).second) {
            continue;
        }
        if (!HttpClient::get().getManifestEntry(levelID).has_value()) {
            manifestIds.push_back(levelID);
        }
        this->prefetchLevelAssets(levelID, priority);
        ++queued;
    }

    if (manifestIds.size() > 1) {
        HttpClient::get().fetchManifest(manifestIds, [](bool) { });
    }
}

void ThumbnailLoader::cancelLoad(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    std::unique_lock<std::shared_mutex> lock(m_queueMutex);
    auto it = m_tasks.find(key);
    if (it != m_tasks.end()) {
        PaimonDebug::log("[ThumbnailLoader] cancelLoad: key={}", key);
        it->second->cancelled = true;
    }
}

void ThumbnailLoader::processQueue() {
    // must be called with m_queueMutex held
    if (m_shuttingDown.load(std::memory_order_acquire)) return;

    if (m_batchMode && m_activeTaskCount > 0) {
        return;
    }

    while (m_activeTaskCount < m_maxConcurrentTasks && !m_priorityQueue.empty()) {
        auto topIt = m_priorityQueue.begin();
        int levelID = topIt->second;
        int entryPriority = topIt->first;
        auto taskIt = m_tasks.find(levelID);

        if (taskIt == m_tasks.end() || !taskIt->second || taskIt->second->running) {
            m_priorityQueue.erase(topIt);
            continue;
        }

        if (entryPriority < taskIt->second->priority) {
            m_priorityQueue.erase(topIt);
            continue;
        }

        if (taskIt->second->cancelled) {
            bool foundNonCancelled = false;
            auto searchIt = std::next(topIt);
            while (searchIt != m_priorityQueue.end() && searchIt->first == entryPriority) {
                auto sTaskIt = m_tasks.find(searchIt->second);
                if (sTaskIt != m_tasks.end() && sTaskIt->second && !sTaskIt->second->running && !sTaskIt->second->cancelled) {
                    levelID = searchIt->second;
                    taskIt = sTaskIt;
                    m_priorityQueue.erase(searchIt);
                    foundNonCancelled = true;
                    break;
                }
                ++searchIt;
            }
            if (!foundNonCancelled) {
                m_priorityQueue.erase(topIt);
            }
        } else {
            m_priorityQueue.erase(topIt);
        }

        startTask(taskIt->second);
    }
}

void ThumbnailLoader::startTask(std::shared_ptr<Task> task) {
    task->running = true;
    task->startedAt = std::chrono::steady_clock::now();
    m_activeTaskCount.fetch_add(1, std::memory_order_relaxed);
    PaimonDebug::log("[ThumbnailLoader] startTask: key={} active={}", task->levelID, m_activeTaskCount.load());
    auto diskJob = [this, task]() {
        if (m_shuttingDown.load(std::memory_order_acquire)) {
            return;
        }
        workerLoadFromDisk(task);
    };
    if (task->priority >= PriorityVisibleCell) {
        m_diskPool->enqueueFront(std::move(diskJob));
    } else {
        m_diskPool->enqueue(std::move(diskJob));
    }
}

void ThumbnailLoader::workerLoadFromDisk(std::shared_ptr<Task> task) {
    PaimonDebug::log("[ThumbnailLoader] workerLoadFromDisk: key={} cancelled={}", task->levelID, task->cancelled);

    // Early cancellation: with disk cache enabled, a cancelled cell isn't worth I/O for a touch.
    if (task->cancelled && paimon::settings::general::enableDiskCache()) {
        if (!m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this, task]() {
                finishTask(task, nullptr, false);
            });
        }
        return;
    }

    if (!paimon::settings::general::enableDiskCache()) {
        workerDownload(task);
        return;
    }

    bool isGif = task->levelID < 0;
    int realID = std::abs(task->levelID);
    auto& cache = paimon::cache::ThumbnailCache::get();

    std::filesystem::path diskPath;
    bool wasInManifest = false;
    {
        bool inIndex = cache.hasDiskEntry(realID, isGif);
        if (inIndex) {
            diskPath = getCachePath(realID, isGif);
            wasInManifest = true;
        } else if (!isGif && cache.hasDiskEntry(realID, true)) {
            diskPath = getCachePath(realID, true);
            wasInManifest = true;
        }

        if (diskPath.empty()) {
            std::error_code ec;
            auto tryPath = getCachePath(realID, isGif);
            if (std::filesystem::exists(tryPath, ec)) {
                diskPath = tryPath;
            } else if (!isGif) {
                auto gifPath = getCachePath(realID, true);
                if (std::filesystem::exists(gifPath, ec)) {
                    diskPath = gifPath;
                }
            }
        }
    }

    std::vector<uint8_t> data;
    if (!diskPath.empty()) {
        auto readStart = std::chrono::steady_clock::now();
        {
            std::error_code fsEc;
            auto fileSize = std::filesystem::file_size(diskPath, fsEc);
            if (!fsEc && fileSize > 0 && fileSize <= 64 * 1024 * 1024) {
                std::ifstream file(diskPath, std::ios::binary);
                if (file.is_open()) {
                    data.resize(static_cast<size_t>(fileSize));
                    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
                    if (!file) data.clear();
                }
            }
        }
        auto readEnd = std::chrono::steady_clock::now();
        auto readUs = std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart).count();
        cache.stats().diskReadTimeUsTotal.fetch_add(static_cast<uint64_t>(readUs), std::memory_order_relaxed);
        if (!data.empty()) {
            PaimonDebug::log("[ThumbnailLoader] disk read: {} bytes in {}us for level {}", data.size(), readUs, realID);
        }
    }

    bool fromLocalRgb = false;
    std::vector<uint8_t> localPixels;
    int localW = 0, localH = 0;

    if (data.empty()) {
        auto localResult = LocalThumbs::get().loadAsRGBA(realID);
        if (!localResult.pixels.empty()) {
            PaimonDebug::log("[ThumbnailLoader] fallback LocalThumbs pal nivel {}", realID);
            if (localResult.isRgb) {
                fromLocalRgb = true;
                localPixels = std::move(localResult.pixels);
                localW = localResult.width;
                localH = localResult.height;
            } else {
                data = std::move(localResult.pixels);
            }
        }
    }

    if (fromLocalRgb && !localPixels.empty()) {
        size_t pixelCount = static_cast<size_t>(localW) * localH;
        std::vector<uint8_t> rgbaBuf(pixelCount * 4);
        ImageConverter::rgbToRgbaFast(localPixels.data(), rgbaBuf.data(), pixelCount);

        if (!LevelColors::get().getPair(realID)) {
            LevelColors::get().extractFromRawData(realID, rgbaBuf.data(), localW, localH, true);
        }

        int maxDim = decodeMaxDimForQuality(task->quality);
        int origW = localW, origH = localH;
        int imgW = localW, imgH = localH;
        if (localW > maxDim || localH > maxDim) {
            auto ds = ImageLoadHelper::downsampleForCache(rgbaBuf.data(), localW, localH, maxDim);
            if (!ds.pixels.empty() && ds.width > 0 && ds.height > 0) {
                rgbaBuf = std::move(ds.pixels);
                imgW = ds.width;
                imgH = ds.height;
            }
        }

        enqueuePendingUpload({task, nullptr, std::move(rgbaBuf), imgW, imgH, realID,
            true/*fallbackToDownload*/, origW, origH});
        return;
    }

    if (data.empty()) {
        cache.stats().diskMisses.fetch_add(1, std::memory_order_relaxed);
        workerDownload(task);
        return;
    }

    cache.stats().diskHits.fetch_add(1, std::memory_order_relaxed);
    {
        bool actualIsGif = (!diskPath.empty() &&
            geode::utils::string::toLower(
                geode::utils::string::pathToString(diskPath.extension())) == ".gif");
        if (!wasInManifest) {
            paimon::cache::ThumbnailCache::DiskEntry de;
            de.levelID = realID;
            de.format = actualIsGif ? "gif" : "png";
            de.byteSize = data.size();
            de.isGif = actualIsGif;
            de.touchAccess();
            de.touchValidated();
            cache.upsertDisk(std::move(de));
            PaimonDebug::log("[ThumbnailLoader] workerLoadFromDisk: upserted FS-fallback entry for {} ({})",
                realID, actualIsGif ? "gif" : "png");
        } else {
            cache.touchDiskAccess(realID, actualIsGif);
        }
    }

    if (task->cancelled) {
        PaimonDebug::log("[ThumbnailLoader] workerLoadFromDisk: disk hit + cancelled — skip decode for {}", realID);
        if (!m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this, task]() {
                finishTask(task, nullptr, false);
            });
        }
        return;
    }

    spawnCpu([this, task, data = std::move(data), realID, isGif, diskPath]() {
        auto decoded = decodeImageData(data, realID, decodeMaxDimForQuality(task->quality));

        if (decoded.success && (decoded.image || !decoded.pixels.empty())) {
            if (decoded.isGif) {
                { std::unique_lock<std::shared_mutex> lock(m_gifLevelsMutex); m_gifLevels.insert(realID); }
            }
            if (!decoded.pixels.empty()) {
                enqueuePendingUpload({task, nullptr, std::move(decoded.pixels), decoded.width, decoded.height, realID,
                    true/*fallbackToDownload*/, decoded.originalWidth, decoded.originalHeight});
            } else {
                enqueuePendingUpload({task, decoded.image, {}, 0, 0, realID,
                    true/*fallbackToDownload*/, decoded.originalWidth, decoded.originalHeight});
            }
        } else {
            PaimonDebug::warn("[ThumbnailLoader] fallo decode pal nivel {} — purgando entrada corrupta del disco", realID);
            spawnDisk([this, task, realID, isGif, diskPath]() {
                std::error_code ec;
                if (!diskPath.empty()) std::filesystem::remove(diskPath, ec);
                auto& cache = paimon::cache::ThumbnailCache::get();
                cache.removeDisk(realID, isGif);
                if (!isGif) cache.removeDisk(realID, true);
                workerDownload(task);
            });
        }
    });
}

void ThumbnailLoader::workerDownload(std::shared_ptr<Task> task) {
    int realID = std::abs(task->levelID);
    bool isGif = task->levelID < 0;
    PaimonDebug::log("[ThumbnailLoader] workerDownload: levelID={} isGif={} cancelled={}", realID, isGif, task->cancelled);
    paimon::cache::ThumbnailCache::get().stats().downloads.fetch_add(1, std::memory_order_relaxed);

    auto retryCount = std::make_shared<std::atomic<int>>(0);
    enqueueBatchDownload(task, retryCount);
}

void ThumbnailLoader::processDownloadedData(std::shared_ptr<Task> task, std::vector<uint8_t> data, int realID) {
    bool taskCancelled = task->cancelled;
    auto self = this;

    spawnDisk([self, task, data = std::move(data), realID, taskCancelled]() {
        bool dataIsGif = paimon::format::isGif(data.data(), data.size());
        bool diskCacheEnabled = paimon::settings::general::enableDiskCache();

        if (diskCacheEnabled) {
            auto path = self->getCachePath(realID, dataIsGif);
            std::error_code dirEc;
            std::filesystem::create_directories(path.parent_path(), dirEc);
            if (dirEc) {
                log::error("[ThumbnailLoader] create_directories fallo para {}: {}",
                    geode::utils::string::pathToString(path.parent_path()), dirEc.message());
            }

            // Atomic write: tmp → rename to avoid corrupt files if the process dies mid-I/O.
            auto tmpPath = path;
            tmpPath += ".tmp";
            bool writeOk = false;
            {
                std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
                if (file) {
                    file.write(reinterpret_cast<char const*>(data.data()), data.size());
                    writeOk = file.good();
                }
            }
            if (writeOk) {
                std::error_code renameEc;
                std::filesystem::remove(path, renameEc);
                renameEc.clear();
                std::filesystem::rename(tmpPath, path, renameEc);
                if (renameEc) {
                    log::error("[ThumbnailLoader] rename fallo {}: {}",
                        geode::utils::string::pathToString(tmpPath), renameEc.message());
                    std::filesystem::remove(tmpPath, renameEc);
                } else {
                    PaimonDebug::log("[ThumbnailLoader] guardado en disco: {} ({} bytes)",
                        geode::utils::string::pathToString(path), data.size());

                    auto& cache = paimon::cache::ThumbnailCache::get();
                    paimon::cache::ThumbnailCache::DiskEntry de;
                    de.levelID = realID;
                    de.format = dataIsGif ? "gif" : "png";
                    de.byteSize = data.size();
                    de.isGif = dataIsGif;
                    de.touchAccess();
                    de.touchValidated();
                    cache.upsertDisk(std::move(de));
                }
            } else {
                log::error("[ThumbnailLoader] no se pudo escribir archivo temporal: {}",
                    geode::utils::string::pathToString(tmpPath));
                std::error_code rmEc;
                std::filesystem::remove(tmpPath, rmEc);
            }

            {
                static std::atomic<int64_t> s_lastEvictCheck{0};
                auto nowEvict = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                auto lastEv = s_lastEvictCheck.load(std::memory_order_relaxed);
                if (nowEvict - lastEv >= 30) {
                    if (s_lastEvictCheck.compare_exchange_strong(lastEv, nowEvict, std::memory_order_acq_rel)) {
                        paimon::cache::ThumbnailCache::get().evictDiskIfNeeded(
                            paimon::settings::quality::diskCacheBytes(),
                            std::chrono::hours(24 * 30));
                    }
                }
            }

            // Debounce disk index save (10s). Don't call Mod::saveData() here — it
            // serializes all saved values (MBs) and would hitch scroll.
            {
                static std::atomic<int64_t> s_lastDiskSave{0};
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                auto last = s_lastDiskSave.load(std::memory_order_relaxed);
                if (now - last >= 10) {
                    if (s_lastDiskSave.compare_exchange_strong(last, now, std::memory_order_acq_rel)) {
                        paimon::cache::ThumbnailCache::get().saveDiskIndex();
                    }
                }
            }
        }

        if (taskCancelled || task->cancelled) {
            PaimonDebug::log("[ThumbnailLoader] processDownloadedData: task cancelada para {} — disco guardado, skip decode/upload", realID);
            if (!self->m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
                Loader::get()->queueInMainThread([self, task]() {
                    self->finishTask(task, nullptr, false);
                });
            }
            return;
        }

        self->spawnCpu([self, task, data = std::move(data), realID]() {
            auto decoded = self->decodeImageData(data, realID, self->decodeMaxDimForQuality(task->quality));

            if (decoded.success && (decoded.image || !decoded.pixels.empty())) {
                if (decoded.isGif) {
                    { std::unique_lock<std::shared_mutex> lock(self->m_gifLevelsMutex); self->m_gifLevels.insert(realID); }
                }
                {
                    auto& cache = paimon::cache::ThumbnailCache::get();
                    auto existingEntry = cache.getDiskEntry(realID, decoded.isGif);
                    if (existingEntry.has_value()) {
                        auto updated = existingEntry.value();
                        updated.width = decoded.width;
                        updated.height = decoded.height;
                        cache.upsertDisk(std::move(updated));
                    }
                }

                if (!decoded.pixels.empty()) {
                    self->enqueuePendingUpload({task, nullptr, std::move(decoded.pixels), decoded.width, decoded.height, realID,
                        false/*fallbackToDownload*/, decoded.originalWidth, decoded.originalHeight});
                } else {
                    self->enqueuePendingUpload({task, decoded.image, {}, 0, 0, realID,
                        false/*fallbackToDownload*/, decoded.originalWidth, decoded.originalHeight});
                }
            } else {
                if (!self->m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
                    Loader::get()->queueInMainThread([self, task]() {
                        self->finishTask(task, nullptr, false);
                    });
                }
            }
        });
    });
}

void ThumbnailLoader::finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success, int origW, int origH) {
    if (task->startedAt.time_since_epoch().count() > 0) {
        auto pipelineUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - task->startedAt).count();
        PaimonDebug::log("[ThumbnailLoader] finishTask: key={} success={} cancelled={} hasTex={} pipeline={}us",
            task->levelID, success, task->cancelled, texture != nullptr, pipelineUs);
    } else {
        PaimonDebug::log("[ThumbnailLoader] finishTask: key={} url={} success={} cancelled={} hasTex={}",
            task->levelID, task->url, success, task->cancelled, texture != nullptr);
    }
    std::vector<LoadCallback> callbacks;
    bool shuttingDown = m_shuttingDown.load(std::memory_order_acquire);
    bool shouldNotify = !task->cancelled && !shuttingDown;

    auto& cache = paimon::cache::ThumbnailCache::get();

    {
        std::unique_lock<std::shared_mutex> lock(m_queueMutex);

        if (task->isUrlTask) {
            if (!shuttingDown && success && texture) {
                cache.addUrlToRam(task->urlCacheKey, texture);
            } else if (!shuttingDown && !task->cancelled) {
                cache.markFailed("url_" + task->urlCacheKey);
            }
            if (shouldNotify) callbacks = task->callbacks;
            m_urlTasks.erase(task->urlCacheKey);

            m_activeUrlTaskCount.fetch_sub(1, std::memory_order_relaxed);

            if (!shuttingDown) {
                processUrlQueue();
            }
        } else {
            int rid = std::abs(task->levelID);
            if (!shuttingDown && success && texture) {
                bool gf = task->levelID < 0;
                cache.addToRam(rid, gf, texture, -1, origW, origH);
            } else if (!shuttingDown && !task->cancelled) {
                std::string keyStr = paimon::cache::CacheKey::fromLegacy(task->levelID).toString();
                if (task->wasNotFound || HttpClient::get().isThumbnailNotFound(rid)) {
                    cache.markNotFound(keyStr);
                } else {
                    cache.markFailed(keyStr);
                }
                recordDownloadFailure();
            }
            if (shouldNotify) callbacks = task->callbacks;
            m_tasks.erase(task->levelID);

            m_activeTaskCount.fetch_sub(1, std::memory_order_relaxed);

            if (!shuttingDown) {
                processQueue();
            }
        }
    }

    // fire callbacks outside the lock — batched to drain max N per frame
    if (shouldNotify) {
        int realID = std::abs(task->levelID);
        for (auto& cb : callbacks) {
            if (cb) enqueuePendingCallback(std::move(cb), texture, success, realID);
        }
    }
}

void ThumbnailLoader::clearCache() {
    log::info("[ThumbnailLoader] clearCache: clearing RAM cache");
    paimon::cache::ThumbnailCache::get().clearRam();
    {
        std::unique_lock<std::shared_mutex> lock(m_gifLevelsMutex);
        m_gifLevels.clear();
    }
}

void ThumbnailLoader::clearFailedCache() {
    log::info("[ThumbnailLoader] clearFailedCache: clearing all failed entries");
    paimon::cache::ThumbnailCache::get().clearAllFailed();
    {
        std::lock_guard<std::mutex> lock(m_cooldownMutex);
        m_recentFailureCount.store(0, std::memory_order_relaxed);
        m_globalCooldownUntil = std::chrono::steady_clock::time_point{};
    }
}

void ThumbnailLoader::invalidateLevel(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    log::info("[ThumbnailLoader] invalidateLevel: levelID={} key={}", levelID, key);

    auto& cache = paimon::cache::ThumbnailCache::get();
    std::vector<InvalidationCallback> listeners;

    {
        std::unique_lock<std::shared_mutex> lock(m_queueMutex);
        cache.incrementInvalidation(levelID);

        cache.removeFromRam(levelID, false);
        cache.removeFromRam(levelID, true);

        std::string pngKey = paimon::cache::CacheKey::fromLegacy(levelID).toString();
        std::string gifKey = paimon::cache::CacheKey::fromLegacy(-levelID).toString();
        cache.clearFailed(pngKey);
        cache.clearFailed(gifKey);
        cache.clearNotFound(pngKey);
        cache.clearNotFound(gifKey);

        m_revisionCheckedThisSession.erase(levelID);

        cache.clearUrlsForLevel(levelID);

        listeners.reserve(m_invalidationListeners.size());
        for (auto const& [_, cb] : m_invalidationListeners) {
            if (cb) listeners.push_back(cb);
        }
    }

    {
        std::unique_lock<std::shared_mutex> gifLock(m_gifLevelsMutex);
        m_gifLevels.erase(levelID);
    }

    HttpClient::get().removeManifestEntry(levelID);

    HttpClient::get().removeExistsEntry(levelID);

    HttpClient::get().clearThumbnailNotFound(levelID);

    ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);

    VideoThumbnailSprite::removeForLevel(levelID);

    LocalThumbs::get().invalidateLookup(levelID);

    if (!listeners.empty() && !m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
        Loader::get()->queueInMainThread([listeners = std::move(listeners), levelID]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            for (auto& cb : listeners) {
                if (cb) cb(levelID);
            }
        });
    }

    {
        auto gifPathStr = geode::utils::string::pathToString(getCachePath(levelID, true));
        AnimatedGIFSprite::remove(gifPathStr);
    }

    if (paimon::settings::general::enableDiskCache()) {
        spawnDisk([levelID, this]() {
            std::error_code ec;
            auto pngPath = getCachePath(levelID, false);
            auto gifPath = getCachePath(levelID, true);
            if (std::filesystem::exists(pngPath, ec)) {
                std::filesystem::remove(pngPath, ec);
            }
            if (std::filesystem::exists(gifPath, ec)) {
                std::filesystem::remove(gifPath, ec);
            }
            auto gifPathStr = geode::utils::string::pathToString(gifPath);
            auto gifDiskCachePath = AnimatedGIFSprite::getCachePath(gifPathStr);
            std::filesystem::remove(gifDiskCachePath, ec);

            auto& cache = paimon::cache::ThumbnailCache::get();
            cache.removeDisk(levelID, false);
            cache.removeDisk(levelID, true);
        });
    }
}

int ThumbnailLoader::getInvalidationVersion(int levelID) const {
    return paimon::cache::ThumbnailCache::get().getInvalidationVersion(levelID);
}

int ThumbnailLoader::addInvalidationListener(InvalidationCallback callback) {
    if (!callback) return 0;
    std::unique_lock<std::shared_mutex> lock(m_queueMutex);
    int id = m_nextInvalidationListenerId++;
    m_invalidationListeners[id] = std::move(callback);
    return id;
}

void ThumbnailLoader::removeInvalidationListener(int listenerId) {
    if (listenerId <= 0) return;
    std::unique_lock<std::shared_mutex> lock(m_queueMutex);
    m_invalidationListeners.erase(listenerId);
}

void ThumbnailLoader::clearDiskCache() {
    log::info("[ThumbnailLoader] clearDiskCache: clearing disk cache (preserving main levels 1-22)");
    spawnDisk([this]() {
        auto cacheDir = paimon::quality::cacheDir();
        auto [preserved, removed] = paimon::clearCachePreservingMainLevels(
            cacheDir, {"gifs"});
        log::info("[ThumbnailLoader] clearDiskCache: preserved {} entries, removed {}",
            preserved, removed);

        paimon::cache::ThumbnailCache::get().clearDisk();
        initDiskCache();
    });
}

void ThumbnailLoader::clearPendingQueue() {
    std::unique_lock<std::shared_mutex> lock(m_queueMutex);
    for (auto& [id, task] : m_tasks) {
        task->cancelled = true;
    }
}

void ThumbnailLoader::updateSessionCache(int levelID, cocos2d::CCTexture2D* texture) {
    if (!texture) return;
    bool isGif = levelID < 0;
    int realID = std::abs(levelID);
    paimon::cache::ThumbnailCache::get().addToRam(realID, isGif, texture);
}

void ThumbnailLoader::cleanup() {
    if (!beginCleanup("cleanup")) {
        return;
    }
    logShutdownSnapshot("cleanup begin");

    log::info("[ThumbnailLoader] cleanup: shutting down loader");
    m_shuttingDown.store(true, std::memory_order_release);
    clearPendingQueue();
    waitBackgroundWorkers();

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        for (auto& pu : m_pendingUploads) {
            if (pu.image) {
                pu.image->release();
                pu.image = nullptr;
            }
        }
        m_pendingUploads.clear();
    }

    // Clear listeners before static destruction: they hold WeakRef<PaimonLevelCell>
    // whose dtor touches CCPoolManager (dead by then → crash).
    {
        std::unique_lock<std::shared_mutex> lock(m_queueMutex);
        m_invalidationListeners.clear();

        for (auto& [id, task] : m_tasks) {
            if (task) task->callbacks.clear();
        }
        m_tasks.clear();
        for (auto& [url, task] : m_urlTasks) {
            if (task) task->callbacks.clear();
        }
        m_urlTasks.clear();
    }

    clearCache();

    logShutdownSnapshot("cleanup end");
    m_cleanupFinished.store(true, std::memory_order_release);
}

void ThumbnailLoader::spawnDisk(std::function<void()> job) {
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    if (m_diskPool) m_diskPool->enqueue(std::move(job));
}

void ThumbnailLoader::spawnCpu(std::function<void()> job) {
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    if (m_cpuPool) m_cpuPool->enqueue(std::move(job));
}

void ThumbnailLoader::waitBackgroundWorkers() {
    if (m_diskPool) m_diskPool->shutdown();
    if (m_cpuPool) m_cpuPool->shutdown();
}

bool ThumbnailLoader::isTextureSane(cocos2d::CCTexture2D* tex) {
    if (!tex) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(tex);
    if (addr < 0x10000) return false;

    auto sz = tex->getContentSize();
    if (std::isnan(sz.width) || std::isnan(sz.height)) return false;
    return sz.width > 0.f && sz.height > 0.f;
}

void ThumbnailLoader::requestUrlLoad(std::string const& url, LoadCallback callback, int priority) {
    if (url.empty()) {
        if (callback) callback(nullptr, false);
        return;
    }

    auto& cache = paimon::cache::ThumbnailCache::get();
    std::string cacheKey = normalizeUrlKey(url);

    std::unique_lock<std::shared_mutex> lock(m_queueMutex);

    auto urlTex = cache.getUrlFromRam(cacheKey);
    if (urlTex.has_value()) {
        cache.stats().ramHits.fetch_add(1, std::memory_order_relaxed);
        auto tex = urlTex.value();
        if (callback) enqueuePendingCallback(std::move(callback), tex.data(), true);
        return;
    }
    cache.stats().ramMisses.fetch_add(1, std::memory_order_relaxed);

    std::string failKey = "url_" + cacheKey;
    if (cache.isFailed(failKey)) {
        if (callback) enqueuePendingCallback(std::move(callback), nullptr, false);
        return;
    }

    auto taskIt = m_urlTasks.find(cacheKey);
    if (taskIt != m_urlTasks.end()) {
        if (callback) taskIt->second->callbacks.push_back(callback);
        if (priority > taskIt->second->priority) {
            taskIt->second->priority = priority;
        }
        return;
    }

    auto task = std::make_shared<Task>();
    task->levelID = 0;
    task->url = url;
    task->urlCacheKey = cacheKey;
    task->priority = priority;
    task->isUrlTask = true;
    if (callback) task->callbacks.push_back(callback);

    m_urlTasks[cacheKey] = task;

    if (m_activeUrlTaskCount < m_maxConcurrentUrlTasks) {
        task->running = true;
        m_activeUrlTaskCount.fetch_add(1, std::memory_order_relaxed);
        spawnDisk([this, task]() {
            if (m_shuttingDown.load(std::memory_order_acquire)) {
                return;
            }
            workerUrlDownload(task);
        });
    }
}

void ThumbnailLoader::processUrlQueue() {
    // must be called with m_queueMutex held
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    while (m_activeUrlTaskCount < m_maxConcurrentUrlTasks) {
        std::shared_ptr<Task> bestTask = nullptr;
        for (auto& [_, task] : m_urlTasks) {
            if (!task || task->running || task->cancelled) continue;
            if (!bestTask || task->priority > bestTask->priority) {
                bestTask = task;
            }
        }
        if (!bestTask) break;

        bestTask->running = true;
        m_activeUrlTaskCount.fetch_add(1, std::memory_order_relaxed);
        spawnDisk([this, task = bestTask]() {
            if (m_shuttingDown.load(std::memory_order_acquire)) {
                return;
            }
            workerUrlDownload(task);
        });
    }
}

void ThumbnailLoader::requestUrlBatchLoad(std::vector<std::string> const& urls, LoadCallback perUrlCallback, int priority) {
    for (auto const& url : urls) {
        requestUrlLoad(url, perUrlCallback, priority);
    }
}

bool ThumbnailLoader::isUrlLoaded(std::string const& url) const {
    return paimon::cache::ThumbnailCache::get().getUrlFromRam(normalizeUrlKey(url)).has_value();
}

void ThumbnailLoader::cancelUrlLoad(std::string const& url) {
    std::unique_lock<std::shared_mutex> lock(m_queueMutex);
    auto it = m_urlTasks.find(normalizeUrlKey(url));
    if (it != m_urlTasks.end()) {
        it->second->cancelled = true;
    }
}

void ThumbnailLoader::workerUrlDownload(std::shared_ptr<Task> task) {
    if (task->cancelled || m_shuttingDown.load(std::memory_order_acquire)) {
        if (!paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this, task]() { finishTask(task, nullptr, false); });
        }
        return;
    }

    std::string url = task->url;
    paimon::cache::ThumbnailCache::get().stats().downloads.fetch_add(1, std::memory_order_relaxed);

    auto retryCount = std::make_shared<std::atomic<int>>(0);
    static constexpr int MAX_URL_DOWNLOAD_RETRIES = 1;

    auto attemptDownload = std::make_shared<std::function<void()>>(nullptr);
    // Weak self-ref: the closure must not own itself (that shared_ptr cycle never
    // frees -> leaks the retry closure + Task on every URL download). The in-flight
    // download callback below holds the only strong ref, so the closure is released
    // once the final (non-retrying) callback is destroyed.
    std::weak_ptr<std::function<void()>> weakAttempt = attemptDownload;
    *attemptDownload = [this, task, url, retryCount, weakAttempt]() {
        auto strongAttempt = weakAttempt.lock();
        if (!strongAttempt) return;
        ThumbnailTransportClient::get().downloadFromUrlData(url,
            [this, task, url, retryCount, strongAttempt](bool success, std::vector<uint8_t> const& data) {
                if (task->cancelled || m_shuttingDown.load(std::memory_order_acquire)) {
                    if (!paimon::isRuntimeShuttingDown()) {
                        Loader::get()->queueInMainThread([this, task]() {
                            finishTask(task, nullptr, false);
                        });
                    }
                    return;
                }
                if (success && !data.empty()) {
                    spawnCpu([this, task, data]() {
                        auto decoded = decodeImageData(data, 0, URL_CACHE_MAX_DIM);
                        if (decoded.success && (decoded.image || !decoded.pixels.empty())) {
                            if (!decoded.pixels.empty()) {
                                enqueuePendingUpload({task, nullptr, std::move(decoded.pixels), decoded.width, decoded.height, 0,
                                    false/*fallbackToDownload*/, decoded.originalWidth, decoded.originalHeight});
                            } else {
                                enqueuePendingUpload({task, decoded.image, {}, 0, 0, 0,
                                    false/*fallbackToDownload*/, decoded.originalWidth, decoded.originalHeight});
                            }
                        } else {
                            if (!m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
                                Loader::get()->queueInMainThread([this, task]() {
                                    finishTask(task, nullptr, false);
                                });
                            }
                        }
                    });
                    return;
                }

                // CCTextureCache hit returns success=true with empty data; the texture
                // already exists. Touch CCTextureCache only on the main thread.
                if (success && data.empty()) {
                    if (!m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
                        Loader::get()->queueInMainThread([this, task, url]() {
                            if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
                                return;
                            }
                            if (auto* tex = CCTextureCache::sharedTextureCache()->textureForKey(url.c_str())) {
                                finishTask(task, tex, true);
                            } else {
                                finishTask(task, nullptr, false);
                            }
                        });
                    }
                    return;
                }

                int attempt = retryCount->fetch_add(1);
                if (attempt < MAX_URL_DOWNLOAD_RETRIES) {
                    log::warn("[ThumbnailLoader] workerUrlDownload: download failed for {} (attempt {}), retrying...", url, attempt + 1);
                    paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                    (*strongAttempt)();
                } else {
                    log::warn("[ThumbnailLoader] workerUrlDownload: download failed for {} after retries", url);
                    paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                    if (!paimon::isRuntimeShuttingDown()) {
                        Loader::get()->queueInMainThread([this, task]() {
                            finishTask(task, nullptr, false);
                        });
                    }
                }
            }
        );
    };
    (*attemptDownload)();
}

ThumbnailLoader::DecodeResult ThumbnailLoader::decodeImageData(std::vector<uint8_t> const& data, int realID, int maxDim) {
    DecodeResult result;
    auto t0 = std::chrono::steady_clock::now();

    {
        int w = 0, h = 0, channels = 0;
        unsigned char* pixelData = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
        if (pixelData && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
            bool isAnim = paimon::format::isGif(data.data(), data.size());

            result.width = w;
            result.height = h;
            result.originalWidth = w;
            result.originalHeight = h;
            result.isGif = isAnim;

            if (realID > 0 && !LevelColors::get().getPair(realID)) {
                LevelColors::get().extractFromRawData(realID, pixelData, w, h, true);
            }

            if (maxDim > 0 && (w > maxDim || h > maxDim)) {
                auto ds = ImageLoadHelper::downsampleForCache(pixelData, w, h, maxDim);
                if (!ds.pixels.empty() && ds.width > 0 && ds.height > 0) {
                    result.pixels = std::move(ds.pixels);
                    result.width = ds.width;
                    result.height = ds.height;
                    result.success = true;
                }
            }

            if (!result.success) {
                size_t byteCount = static_cast<size_t>(w) * h * 4;
                result.pixels.assign(pixelData, pixelData + byteCount);
                result.success = true;
            }

            stbi_image_free(pixelData);
        } else if (pixelData) {
            stbi_image_free(pixelData);
        }
    }

    if (!result.success && !data.empty()) {
        auto* img = new CCImage();
        if (img->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
            result.image = img;
            result.width = img->getWidth();
            result.height = img->getHeight();
            result.originalWidth = result.width;
            result.originalHeight = result.height;
            result.success = true;
        } else {
            // CCImage is a CCObject: release() (not delete) to respect refcount.
            img->release();
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.decodeTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    paimon::cache::ThumbnailCache::get().stats().decodeTimeUsTotal.fetch_add(static_cast<uint64_t>(result.decodeTimeUs), std::memory_order_relaxed);

    return result;
}

void ThumbnailLoader::updateRemoteRevision(int levelID, std::string const& revisionToken) {
    if (levelID <= 0 || revisionToken.empty()) return;

    std::unique_lock<std::shared_mutex> lock(m_queueMutex);
    auto it = m_remoteRevisions.find(levelID);
    if (it != m_remoteRevisions.end() && it->second == revisionToken) {
        return;
    }

    bool hadPrevious = (it != m_remoteRevisions.end());
    m_remoteRevisions[levelID] = revisionToken;

    if (hadPrevious) {
        log::info("[ThumbnailLoader] remote revision changed for level {}, invalidating", levelID);
        if (!m_shuttingDown.load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this, levelID]() {
                invalidateLevel(levelID, false);
                invalidateLevel(levelID, true);
            });
        }
    }
}

void ThumbnailLoader::flushManifest() {
    spawnDisk([]() {
        paimon::cache::ThumbnailCache::get().saveDiskIndex();
    });
}

void ThumbnailLoader::enqueueBatchDownload(std::shared_ptr<Task> task,
                                           std::shared_ptr<std::atomic<int>> retryCount) {
    bool shouldFlushNow = false;
    {
        std::lock_guard<std::mutex> lock(m_batchPendingMutex);
        m_batchPendingDownloads.push_back({task, retryCount});
        if (m_batchPendingDownloads.size() >= BATCH_FLUSH_THRESHOLD) {
            shouldFlushNow = true;
        }
    }
    if (shouldFlushNow) {
        if (!paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this]() { flushBatchDownloads(); });
        }
        return;
    }
    scheduleBatchFlush();
}

void ThumbnailLoader::scheduleBatchFlush() {
    bool expected = false;
    if (!m_batchFlushScheduled.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    if (paimon::isRuntimeShuttingDown() || m_shuttingDown.load(std::memory_order_acquire)) {
        m_batchFlushScheduled.store(false, std::memory_order_release);
        return;
    }
    Loader::get()->queueInMainThread([this]() {
        if (paimon::isRuntimeShuttingDown() || m_shuttingDown.load(std::memory_order_acquire)) {
            m_batchFlushScheduled.store(false, std::memory_order_release);
            return;
        }
        float delay = static_cast<float>(BATCH_FLUSH_DELAY_MS) / 1000.f;
        paimon::scheduleMainThreadDelay(delay, [this]() {
            if (paimon::isRuntimeShuttingDown() || m_shuttingDown.load(std::memory_order_acquire)) {
                m_batchFlushScheduled.store(false, std::memory_order_release);
                return;
            }
            flushBatchDownloads();
        });
    });
}

void ThumbnailLoader::flushBatchDownloads() {
    static constexpr int MAX_DOWNLOAD_RETRIES = 1;

    std::vector<BatchPending> drained;
    bool moreLeft = false;
    {
        std::lock_guard<std::mutex> lock(m_batchPendingMutex);
        if (m_batchPendingDownloads.empty()) {
            m_batchFlushScheduled.store(false, std::memory_order_release);
            return;
        }
        size_t take = std::min<size_t>(m_batchPendingDownloads.size(), BATCH_FLUSH_THRESHOLD);
        drained.assign(m_batchPendingDownloads.begin(), m_batchPendingDownloads.begin() + take);
        m_batchPendingDownloads.erase(m_batchPendingDownloads.begin(), m_batchPendingDownloads.begin() + take);
        moreLeft = !m_batchPendingDownloads.empty();
        if (!moreLeft) {
            m_batchFlushScheduled.store(false, std::memory_order_release);
        }
    }

    if (moreLeft) {
        float delay = static_cast<float>(BATCH_FLUSH_DELAY_MS) / 1000.f;
        paimon::scheduleMainThreadDelay(delay, [this]() {
            if (paimon::isRuntimeShuttingDown() || m_shuttingDown.load(std::memory_order_acquire)) {
                m_batchFlushScheduled.store(false, std::memory_order_release);
                return;
            }
            flushBatchDownloads();
        });
    }

    if (drained.empty()) return;

    // Batch API only returns static PNGs; GIF tasks (negative levelID) must download individually.
    std::vector<BatchPending> gifTasks;
    std::vector<BatchPending> staticTasks;
    gifTasks.reserve(drained.size());
    staticTasks.reserve(drained.size());
    for (auto& pending : drained) {
        if (!pending.task) continue;
        if (pending.task->levelID < 0) gifTasks.push_back(std::move(pending));
        else staticTasks.push_back(std::move(pending));
    }

    auto dispatchSingleDownload = [this](BatchPending pending, bool isGif) {
        if (!pending.task) return;
        int realID = std::abs(pending.task->levelID);
        if (realID <= 0) return;

        int attempt = pending.retryCount ? pending.retryCount->fetch_add(1) : MAX_DOWNLOAD_RETRIES;
        if (attempt < MAX_DOWNLOAD_RETRIES && !isGlobalCooldownActive()) {
            std::shared_ptr<Task> task = pending.task;
            HttpClient::get().downloadThumbnail(realID, isGif,
                [this, task, realID](bool retrySuccess, std::vector<uint8_t> const& retryData, int, int) {
                    if (m_shuttingDown.load(std::memory_order_acquire)) {
                        if (!paimon::isRuntimeShuttingDown()) {
                            Loader::get()->queueInMainThread([this, task]() {
                                finishTask(task, nullptr, false);
                            });
                        }
                        return;
                    }
                    if (retrySuccess && !retryData.empty()) {
                        processDownloadedData(task, retryData, realID);
                    } else if (!paimon::isRuntimeShuttingDown()) {
                        Loader::get()->queueInMainThread([this, task]() {
                            finishTask(task, nullptr, false);
                        });
                    }
                });
        } else if (!paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this, pending]() {
                finishTask(pending.task, nullptr, false);
            });
        }
    };

    for (auto& pending : gifTasks) {
        dispatchSingleDownload(std::move(pending), true);
    }

    std::unordered_map<int, std::vector<BatchPending>> idToTasks;
    std::vector<int> ids;
    ids.reserve(staticTasks.size());
    for (auto& pending : staticTasks) {
        if (!pending.task) continue;
        int realID = std::abs(pending.task->levelID);
        if (realID <= 0) continue;
        if (idToTasks.find(realID) == idToTasks.end()) {
            ids.push_back(realID);
        }
        idToTasks[realID].push_back(std::move(pending));
    }

    if (ids.empty()) return;

    PaimonDebug::log("[ThumbnailLoader] flushBatchDownloads: dispatching {} unique ids ({} tasks)",
        ids.size(), drained.size());

    auto self = this;

    HttpClient::get().downloadThumbnailsBatch(ids,
        [self, idToTasks = std::move(idToTasks)]
        (bool success, std::unordered_map<int, HttpClient::BatchItem> const& items) {
            if (self->m_shuttingDown.load(std::memory_order_acquire)) {
                for (auto& [id, pendings] : idToTasks) {
                    for (auto& pending : pendings) {
                        if (pending.task && !paimon::isRuntimeShuttingDown()) {
                            Loader::get()->queueInMainThread([self, pending]() {
                                self->finishTask(pending.task, nullptr, false);
                            });
                        }
                    }
                }
                return;
            }

            for (auto& [realID, pendings] : idToTasks) {
                auto it = items.find(realID);
                bool itemOk = (it != items.end()) && it->second.ok && !it->second.data.empty();

                if (success && itemOk) {
                    for (auto& pending : pendings) {
                        if (!pending.task) continue;
                        if (pending.task->cancelled) {
                        }
                        std::vector<uint8_t> data = it->second.data;
                        self->processDownloadedData(pending.task, std::move(data), realID);
                    }
                    continue;
                }

                for (auto& pending : pendings) {
                    if (!pending.task) continue;
                    int attempt = pending.retryCount ? pending.retryCount->fetch_add(1) : MAX_DOWNLOAD_RETRIES;
                    if (attempt < MAX_DOWNLOAD_RETRIES && !self->isGlobalCooldownActive()) {
                        bool isGif = pending.task->levelID < 0;
                        log::warn("[ThumbnailLoader] flushBatch: item missing for level {}, falling back to single download", realID);
                        paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                        std::shared_ptr<Task> task = pending.task;
                        HttpClient::get().downloadThumbnail(realID, isGif,
                            [self, task, realID](bool retrySuccess, std::vector<uint8_t> const& retryData, int, int) {
                                if (self->m_shuttingDown.load(std::memory_order_acquire)) {
                                    if (!paimon::isRuntimeShuttingDown()) {
                                        Loader::get()->queueInMainThread([self, task]() {
                                            self->finishTask(task, nullptr, false);
                                        });
                                    }
                                    return;
                                }
                                if (retrySuccess && !retryData.empty()) {
                                    self->processDownloadedData(task, retryData, realID);
                                } else {
                                    log::warn("[ThumbnailLoader] flushBatch: single fallback also failed for level {}", realID);
                                    paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                                    if (!paimon::isRuntimeShuttingDown()) {
                                        Loader::get()->queueInMainThread([self, task]() {
                                            self->finishTask(task, nullptr, false);
                                        });
                                    }
                                }
                            });
                    } else {
                        log::warn("[ThumbnailLoader] flushBatch: download failed for level {} after retries", realID);
                        paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                        if (!paimon::isRuntimeShuttingDown()) {
                            Loader::get()->queueInMainThread([self, pending]() {
                                self->finishTask(pending.task, nullptr, false);
                            });
                        }
                    }
                }
            }
        });
}
