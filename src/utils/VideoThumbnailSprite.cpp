// VideoThumbnailSprite.cpp — CCSprite wrapper for video thumbnails.

#include "VideoThumbnailSprite.hpp"
#include "WebHelper.hpp"
#include "../core/Settings.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "ThreadPool.hpp"
#include "../video/AudioExtractor.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <algorithm>
#include <future>
#include <mutex>
#include <unordered_set>

using namespace geode::prelude;
namespace fs = std::filesystem;

std::mutex VideoThumbnailSprite::s_cacheMutex;
std::unordered_map<std::string, std::string> VideoThumbnailSprite::s_tempFiles;
std::unordered_map<std::string, std::shared_ptr<VideoThumbnailSprite::DownloadRequest>> VideoThumbnailSprite::s_downloadRequests;
std::deque<std::string> VideoThumbnailSprite::s_downloadQueue;
std::deque<VideoThumbnailSprite::CreateJob> VideoThumbnailSprite::s_createQueue;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> VideoThumbnailSprite::s_recentFailures;
std::atomic<int> VideoThumbnailSprite::s_activeDownloads{0};
std::atomic<int> VideoThumbnailSprite::s_activeCreates{0};
std::atomic<bool> VideoThumbnailSprite::s_asyncShutdown{false};

// VideoPlayer cache
std::mutex VideoThumbnailSprite::s_playerCacheMutex;
std::deque<VideoThumbnailSprite::CachedPlayer> VideoThumbnailSprite::s_playerCache;

// Active-sprite budget (FIX-5/6/7)
std::mutex VideoThumbnailSprite::s_activeSpritesMutex;
int VideoThumbnailSprite::s_activeSpriteCount = 0;

// Disk-cache LRU bookkeeping (FIX-8). Parallel map next to s_tempFiles so
// we don't break the existing API. Touched whenever an entry is read/written
// while holding s_cacheMutex.
namespace {
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
    tempFilesLastUsed() {
        static std::unordered_map<std::string, std::chrono::steady_clock::time_point> m;
        return m;
    }
}

int VideoThumbnailSprite::adaptiveSpriteFPS(int activeCount) {
    int baseFPS = paimon::settings::video::fpsLimit();
    if (baseFPS <= 0) baseFPS = 30;
    if (!paimon::settings::video::adaptiveFPS() || activeCount <= 1) {
        return baseFPS;
    }
    int minFPS = paimon::settings::video::minVideoFPS();
    if (minFPS < 1) minFPS = 1;
    if (minFPS > baseFPS) minFPS = baseFPS;
    int target = baseFPS / activeCount;
    if (target < minFPS) target = minFPS;
    if (target > baseFPS) target = baseFPS;
    return target;
}

bool VideoThumbnailSprite::tryAcquireActiveSlot() {
    if (m_holdsActiveSlot) return true;
    int newCount = 0;
    {
        std::lock_guard lock(s_activeSpritesMutex);
        if (s_activeSpriteCount >= MAX_ACTIVE_SPRITES) {
            return false;
        }
        ++s_activeSpriteCount;
        newCount = s_activeSpriteCount;
    }
    m_holdsActiveSlot = true;
    if (m_player) {
        m_player->setTargetFPS(adaptiveSpriteFPS(newCount));
    }
    return true;
}

void VideoThumbnailSprite::releaseActiveSlot() {
    if (!m_holdsActiveSlot) return;
    m_holdsActiveSlot = false;
    int newCount = 0;
    {
        std::lock_guard lock(s_activeSpritesMutex);
        s_activeSpriteCount = std::max(0, s_activeSpriteCount - 1);
        newCount = s_activeSpriteCount;
    }
    // We don't have a list of active sprites to rebalance here (would require
    // a global registry). The next sprite that enters update() will recompute
    // its own FPS via tryAcquireActiveSlot, which is good enough in practice
    // because list scrolling already triggers new acquisitions every frame.
    (void)newCount;
}

// ── static helpers ──────────────────────────────────────────────────

std::string VideoThumbnailSprite::getTempPath(std::string const& cacheKey) {
    auto dir = dirs::getModRuntimeDir() / "video_cache";
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto hashed = std::to_string(std::hash<std::string>{}(cacheKey));
    return geode::utils::string::pathToString(dir / ("video_" + hashed + ".mp4"));
}

std::string VideoThumbnailSprite::makeRequestKey(std::string const& url, std::string const& cacheKey) {
    if (!url.empty()) {
        return "url:" + url;
    }
    return "cache:" + cacheKey;
}

std::string VideoThumbnailSprite::getCachedPathLocked(std::string const& key) {
    auto it = s_tempFiles.find(key);
    if (it != s_tempFiles.end()) {
        std::error_code ec;
        if (fs::exists(it->second, ec)) {
            tempFilesLastUsed()[key] = std::chrono::steady_clock::now();
            return it->second;
        }
        s_tempFiles.erase(it);
        tempFilesLastUsed().erase(key);
    }

    auto fallback = getTempPath(key);
    std::error_code ec;
    if (fs::exists(fallback, ec)) {
        s_tempFiles[key] = fallback;
        tempFilesLastUsed()[key] = std::chrono::steady_clock::now();
        return fallback;
    }
    return {};
}

void VideoThumbnailSprite::registerCachedPathLocked(std::string const& key, std::string const& path) {
    if (!key.empty() && !path.empty()) {
        s_tempFiles[key] = path;
        tempFilesLastUsed()[key] = std::chrono::steady_clock::now();
    }
}

void VideoThumbnailSprite::enforceTempFilesBudgetLocked() {
    // Compute total size on disk for entries we know about, and collect
    // them sorted by lastUsed (oldest first).
    struct Entry {
        std::string key;
        std::string path;
        size_t size;
        std::chrono::steady_clock::time_point lastUsed;
    };
    std::vector<Entry> entries;
    entries.reserve(s_tempFiles.size());

    auto& touched = tempFilesLastUsed();
    auto now = std::chrono::steady_clock::now();
    size_t totalBytes = 0;
    for (auto const& [k, p] : s_tempFiles) {
        std::error_code ec;
        size_t sz = static_cast<size_t>(fs::file_size(p, ec));
        if (ec) sz = 0;
        totalBytes += sz;
        auto tIt = touched.find(k);
        auto t = (tIt != touched.end()) ? tIt->second : now;
        entries.push_back({k, p, sz, t});
    }

    bool overCount = static_cast<int>(entries.size()) > MAX_TEMP_FILES;
    bool overBytes = totalBytes > MAX_TEMP_FILES_BYTES;
    if (!overCount && !overBytes) return;

    std::sort(entries.begin(), entries.end(),
              [](Entry const& a, Entry const& b) {
                  return a.lastUsed < b.lastUsed;
              });

    int evicted = 0;
    for (auto const& e : entries) {
        if (!overCount && !overBytes) break;
        std::error_code ec;
        fs::remove(e.path, ec);
        s_tempFiles.erase(e.key);
        touched.erase(e.key);
        if (totalBytes >= e.size) totalBytes -= e.size;
        ++evicted;
        overCount = (static_cast<int>(s_tempFiles.size()) > MAX_TEMP_FILES);
        overBytes = (totalBytes > MAX_TEMP_FILES_BYTES);
    }
    if (evicted > 0) {
        log::info("[VideoThumbSprite] LRU evicted {} on-disk temp files "
                  "(remaining {} entries / {:.1f} MB)",
                  evicted,
                  s_tempFiles.size(),
                  static_cast<double>(totalBytes) / (1024.0 * 1024.0));
    }
}

void VideoThumbnailSprite::cleanupOrphanedDiskFiles() {
    static std::atomic<bool> s_ranOnce{false};
    bool expected = false;
    if (!s_ranOnce.compare_exchange_strong(expected, true)) {
        return; // Already ran this session.
    }

    auto dir = dirs::getModRuntimeDir() / "video_cache";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    // Build a set of paths we currently know about so we can detect orphans.
    std::unordered_set<std::string> known;
    {
        std::lock_guard lock(s_cacheMutex);
        known.reserve(s_tempFiles.size());
        for (auto const& [k, p] : s_tempFiles) {
            known.insert(p);
        }
    }

    // Cap the total size of ff_*.raw first-frame files: keep oldest deletable.
    struct FFFile { fs::path path; size_t size; std::filesystem::file_time_type mtime; };
    std::vector<FFFile> ffFiles;
    size_t ffTotal = 0;
    int orphans = 0;

    fs::directory_iterator it(dir, ec);
    if (ec) return;
    for (auto const& entry : it) {
        if (!entry.is_regular_file(ec) || ec) continue;
        auto p = entry.path();
        auto name = geode::utils::string::pathToString(p.filename());

        if (name.starts_with("ff_") && name.ends_with(".raw")) {
            size_t sz = static_cast<size_t>(entry.file_size(ec));
            if (ec) sz = 0;
            ffTotal += sz;
            ffFiles.push_back({p, sz, entry.last_write_time(ec)});
        } else if (name.starts_with("video_") && name.ends_with(".mp4")) {
            // Orphan MP4s (file on disk but not in s_tempFiles) — likely from
            // a previous session whose s_tempFiles map was lost. Safe to remove
            // because s_tempFiles is rebuilt from the path layout on demand.
            if (known.find(geode::utils::string::pathToString(p)) == known.end()) {
                std::error_code rmEc;
                fs::remove(p, rmEc);
                if (!rmEc) ++orphans;
            }
        }
    }

    // Trim ff_*.raw down to MAX_FIRST_FRAME_BYTES (oldest first).
    if (ffTotal > MAX_FIRST_FRAME_BYTES && !ffFiles.empty()) {
        std::sort(ffFiles.begin(), ffFiles.end(),
                  [](FFFile const& a, FFFile const& b) {
                      return a.mtime < b.mtime;
                  });
        size_t removedBytes = 0;
        int removedCount = 0;
        for (auto const& f : ffFiles) {
            if (ffTotal - removedBytes <= MAX_FIRST_FRAME_BYTES) break;
            std::error_code rmEc;
            fs::remove(f.path, rmEc);
            if (!rmEc) {
                removedBytes += f.size;
                ++removedCount;
            }
        }
        if (removedCount > 0) {
            log::info("[VideoThumbSprite] Cleanup: removed {} first-frame .raw "
                      "files ({:.1f} MB freed)",
                      removedCount,
                      static_cast<double>(removedBytes) / (1024.0 * 1024.0));
        }
    }
    if (orphans > 0) {
        log::info("[VideoThumbSprite] Cleanup: removed {} orphaned mp4 files",
                  orphans);
    }
}

void VideoThumbnailSprite::pruneRecentFailuresLocked(std::chrono::steady_clock::time_point now) {
    for (auto it = s_recentFailures.begin(); it != s_recentFailures.end();) {
        if (now - it->second >= FAILED_REQUEST_TTL) {
            it = s_recentFailures.erase(it);
        } else {
            ++it;
        }
    }
}

void VideoThumbnailSprite::pumpAsyncQueues() {
    std::vector<std::pair<std::string, std::string>> downloadsToStart;
    std::vector<CreateJob> createsToStart;

    {
        std::lock_guard lock(s_cacheMutex);
        if (s_asyncShutdown) {
            return;
        }

        while (s_activeDownloads < MAX_CONCURRENT_DOWNLOADS && !s_downloadQueue.empty()) {
            auto requestKey = std::move(s_downloadQueue.front());
            s_downloadQueue.pop_front();

            auto it = s_downloadRequests.find(requestKey);
            if (it == s_downloadRequests.end() || !it->second || it->second->started) {
                continue;
            }

            it->second->started = true;
            ++s_activeDownloads;
            downloadsToStart.emplace_back(requestKey, it->second->url);
        }

        while (s_activeCreates < MAX_CONCURRENT_CREATES && !s_createQueue.empty()) {
            createsToStart.push_back(std::move(s_createQueue.front()));
            s_createQueue.pop_front();
            ++s_activeCreates;
        }
    }

    for (auto& [requestKey, url] : downloadsToStart) {
        auto req = web::WebRequest();
        req.acceptEncoding("gzip, deflate");
        req.timeout(std::chrono::seconds(60));
        WebHelper::dispatch(std::move(req), "GET", url, [requestKey](web::WebResponse res) mutable {
            handleDownloadResponse(std::move(requestKey), std::move(res));
        });
    }

    for (auto& job : createsToStart) {
        Loader::get()->queueInMainThread([job = std::move(job)]() mutable {
            handleCreateJob(std::move(job));
        });
    }
}

void VideoThumbnailSprite::handleDownloadResponse(std::string requestKey, web::WebResponse&& response) {
    std::vector<PendingCreateCallback> callbacks;
    std::string localPath;
    bool downloadOk = false;
    bool shouldPumpQueues = true;

    {
        std::lock_guard lock(s_cacheMutex);
        auto it = s_downloadRequests.find(requestKey);
        if (it == s_downloadRequests.end() || !it->second) {
            s_activeDownloads = std::max(0, s_activeDownloads - 1);
            shouldPumpQueues = false;
        } else {
            callbacks = std::move(it->second->callbacks);
            localPath = it->second->localPath;
            s_downloadRequests.erase(it);
            s_activeDownloads = std::max(0, s_activeDownloads - 1);
        }
    }

    if (!shouldPumpQueues) {
        pumpAsyncQueues();
        return;
    }

    if (!s_asyncShutdown && response.ok()) {
        auto data = response.data();
        if (!data.empty()) {
            // Enforce max video file size to prevent OOM
            if (data.size() > paimon::settings::video::kMaxVideoFileSize) {
                log::warn("[VideoThumbSprite] Download too large ({} bytes, max {}), skipping", data.size(), paimon::settings::video::kMaxVideoFileSize);
            } else {
                auto writeRes = geode::utils::file::writeBinary(localPath, data);
                downloadOk = writeRes.isOk();
                if (downloadOk) {
                    log::info("[VideoThumbSprite] Downloaded {} bytes to {}", data.size(), localPath);
                } else {
                    log::warn("[VideoThumbSprite] Failed to write download to {}", localPath);
                }
            }
        } else {
            log::warn("[VideoThumbSprite] Download returned empty data for requestKey={}", requestKey);
        }
    } else if (!s_asyncShutdown) {
        log::warn("[VideoThumbSprite] Download HTTP error for requestKey={} code={}", requestKey, response.code());
    }

    {
        std::lock_guard lock(s_cacheMutex);
        if (downloadOk && !s_asyncShutdown) {
            registerCachedPathLocked(requestKey, localPath);
            for (auto& pending : callbacks) {
                registerCachedPathLocked(pending.cacheKey, localPath);
                s_createQueue.push_back(CreateJob{
                    requestKey,
                    pending.cacheKey,
                    localPath,
                    std::move(pending.callback)
                });
            }
            // Trim the on-disk cache to its budget after adding a new entry.
            enforceTempFilesBudgetLocked();
        } else {
            auto now = std::chrono::steady_clock::now();
            pruneRecentFailuresLocked(now);
            s_recentFailures[requestKey] = now;
        }
    }

    if (!downloadOk || s_asyncShutdown) {
        for (auto& pending : callbacks) {
            if (!pending.callback) continue;
            Loader::get()->queueInMainThread([callback = std::move(pending.callback)]() mutable {
                callback(nullptr);
            });
        }
    }

    pumpAsyncQueues();
}

void VideoThumbnailSprite::handleCreateJob(CreateJob job) {
    VideoThumbnailSprite* sprite = nullptr;
    if (!s_asyncShutdown) {
        sprite = create(job.localPath);
        if (sprite) {
            sprite->m_cacheKey = job.cacheKey;
        }
    }

    {
        std::lock_guard lock(s_cacheMutex);
        s_activeCreates = std::max(0, s_activeCreates - 1);
        if (!sprite && !s_asyncShutdown) {
            // Only poison s_recentFailures if the file genuinely doesn't exist
            // or is empty. Decoder failures are often transient (e.g. MF busy,
            // codec not ready) and should not block retries from other contexts
            // (popup, profile, etc.).
            bool fileValid = false;
            {
                std::error_code ec;
                auto fsize = std::filesystem::file_size(job.localPath, ec);
                fileValid = !ec && fsize > 1024; // non-trivial file
            }
            if (!fileValid) {
                auto now = std::chrono::steady_clock::now();
                pruneRecentFailuresLocked(now);
                s_recentFailures[job.requestKey] = now;
                log::info("[VideoThumbSprite] handleCreateJob: marking failed (file missing/empty) requestKey={}", job.requestKey);
            } else {
                log::warn("[VideoThumbSprite] handleCreateJob: decoder failed but file exists ({}), NOT poisoning failure cache", job.localPath);
            }
            s_tempFiles.erase(job.cacheKey);
            if (!job.requestKey.empty()) {
                s_tempFiles.erase(job.requestKey);
            }
        }
    }

    if (job.callback) {
        job.callback(sprite);
    }

    pumpAsyncQueues();
}

// ── first-frame disk cache ──────────────────────────────────────────

std::string VideoThumbnailSprite::getFirstFrameCachePath(std::string const& videoPath) {
    auto dir = dirs::getModRuntimeDir() / "video_cache";
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto hashed = std::to_string(std::hash<std::string>{}(videoPath));
    return geode::utils::string::pathToString(dir / ("ff_" + hashed + ".raw"));
}

void VideoThumbnailSprite::saveFirstFrameToCache() {
    if (m_firstFrameSavedToCache || !m_player) return;
    m_firstFrameSavedToCache = true;

    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!m_player->copyCurrentFramePixels(pixels, w, h)) return;
    if (pixels.empty() || w <= 0 || h <= 0) return;

    // Determine cache path from cacheKey (which is the video file path or URL key)
    std::string cachePath = getFirstFrameCachePath(m_cacheKey.empty() ? "unknown" : m_cacheKey);

    // Off-main-thread write: use a small dedicated I/O pool.  Note: the prior
    // `std::async(std::launch::async, …)` pattern was actually synchronous —
    // the discarded `std::future` blocks in its destructor — so this also
    // restores intended async behaviour while avoiding per-call thread spawn
    // overhead during fast cell scrolling.
    static std::once_flag s_videoFFInitFlag;
    static std::unique_ptr<paimon::ThreadPool> s_videoFFPool;
    std::call_once(s_videoFFInitFlag, []() {
        s_videoFFPool = std::make_unique<paimon::ThreadPool>(1, "PaimonVideoFF");
    });
    if (!s_videoFFPool || s_videoFFPool->isStopped()) return;

    s_videoFFPool->enqueue([cachePath, pixels = std::move(pixels), w, h]() {
        if (VideoThumbnailSprite::s_asyncShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            return;
        }
        // Build full binary buffer: uint32 width + uint32 height + raw RGBA pixels
        uint32_t uw = static_cast<uint32_t>(w);
        uint32_t uh = static_cast<uint32_t>(h);
        std::vector<uint8_t> buf;
        buf.reserve(sizeof(uw) + sizeof(uh) + pixels.size());
        auto const* pw = reinterpret_cast<uint8_t const*>(&uw);
        auto const* ph = reinterpret_cast<uint8_t const*>(&uh);
        buf.insert(buf.end(), pw, pw + sizeof(uw));
        buf.insert(buf.end(), ph, ph + sizeof(uh));
        buf.insert(buf.end(), pixels.begin(), pixels.end());
        if (VideoThumbnailSprite::s_asyncShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            return;
        }
        (void)geode::utils::file::writeBinary(cachePath, buf);
    });
}

bool VideoThumbnailSprite::loadFirstFrameFromCache(std::string const& videoPath) {
    std::string cachePath = getFirstFrameCachePath(videoPath);

    auto readRes = geode::utils::file::readBinary(cachePath);
    if (readRes.isErr()) return false;
    auto& buf = readRes.unwrap();
    if (buf.size() < sizeof(uint32_t) * 2) return false;

    uint32_t w = 0, h = 0;
    std::memcpy(&w, buf.data(), sizeof(w));
    std::memcpy(&h, buf.data() + sizeof(w), sizeof(h));
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    size_t expectedSize = static_cast<size_t>(w) * h * 4;
    size_t headerSize = sizeof(w) + sizeof(h);
    if (buf.size() < headerSize + expectedSize) return false;

    std::vector<uint8_t> pixels(buf.begin() + headerSize, buf.begin() + headerSize + expectedSize);

    auto* tex = new cocos2d::CCTexture2D();
    if (!tex->initWithData(
            pixels.data(),
            cocos2d::kCCTexture2DPixelFormat_RGBA8888,
            static_cast<int>(w), static_cast<int>(h),
            cocos2d::CCSizeMake(static_cast<float>(w), static_cast<float>(h)))) {
        tex->release();
        return false;
    }

    this->setTexture(tex);
    this->setTextureRect(cocos2d::CCRectMake(0, 0, static_cast<float>(w), static_cast<float>(h)));
    this->setContentSize(cocos2d::CCSizeMake(static_cast<float>(w), static_cast<float>(h)));
    tex->release(); // CCSprite retains it
    m_firstFrame = true;
    return true;
}

// ── factory methods ─────────────────────────────────────────────────

bool VideoThumbnailSprite::isCached(std::string const& cacheKey) {
    std::lock_guard lock(s_cacheMutex);
    return !getCachedPathLocked(cacheKey).empty();
}

std::string VideoThumbnailSprite::getCachedPathForKey(std::string const& cacheKey) {
    std::lock_guard lock(s_cacheMutex);
    return getCachedPathLocked(cacheKey);
}

VideoThumbnailSprite* VideoThumbnailSprite::createFromCache(std::string const& cacheKey) {
    std::string path;
    {
        std::lock_guard lock(s_cacheMutex);
        path = getCachedPathLocked(cacheKey);
    }
    if (path.empty()) return nullptr;

    // Clear any cached player for this key to avoid stale state
    {
        std::lock_guard lock(s_playerCacheMutex);
        for (auto it = s_playerCache.begin(); it != s_playerCache.end(); ++it) {
            if (it->cacheKey == path) {
                if (it->player) {
                    it->player->stop();
                }
                s_playerCache.erase(it);
                break;
            }
        }
    }

    auto* sprite = create(path);
    if (sprite) sprite->m_cacheKey = cacheKey;
    return sprite;
}

VideoThumbnailSprite* VideoThumbnailSprite::create(std::string const& filePath) {
    // Best-effort one-shot cleanup of orphaned disk files (FIX-9). The flag
    // inside the function is compare-exchange so this only runs once per
    // process lifetime; subsequent calls return immediately.
    cleanupOrphanedDiskFiles();

    // Try to get a cached player first (avoids re-decoding)
    auto cachedPlayer = getCachedPlayer(filePath);
    if (cachedPlayer) {
        auto* sprite = new (std::nothrow) VideoThumbnailSprite();
        if (sprite && sprite->initWithPlayer(std::move(cachedPlayer))) {
            sprite->autorelease();
            sprite->m_cacheKey = filePath;
            sprite->m_firstFrame = true; // Cached player already has frames
            log::debug("[VideoThumbSprite] Reusing cached player for: {}", filePath);
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
    }
    
    // Create new player
    auto player = paimon::video::VideoPlayer::create(filePath);
    if (!player) {
        log::warn("[VideoThumbSprite] Failed to create player for: {}", filePath);
        return nullptr;
    }

    auto* sprite = new (std::nothrow) VideoThumbnailSprite();
    if (sprite && sprite->initWithPlayer(std::move(player))) {
        sprite->autorelease();
        // Try loading cached first frame for instant display on restart
        if (!sprite->m_firstFrame) {
            sprite->loadFirstFrameFromCache(filePath);
        }
        // Store filePath as cacheKey fallback for first-frame saving
        if (sprite->m_cacheKey.empty()) {
            sprite->m_cacheKey = filePath;
        }
        return sprite;
    }
    CC_SAFE_DELETE(sprite);
    return nullptr;
}

VideoThumbnailSprite* VideoThumbnailSprite::createFromData(std::vector<uint8_t> const& data, std::string const& cacheKey) {
    if (data.size() < 12) {
        log::warn("[VideoThumbSprite] Data too small to be a valid video: {} bytes", data.size());
        return nullptr;
    }

    bool isMp4 = false;
    for (size_t i = 0; i + 3 < data.size() && i < 12; ++i) {
        if (data[i] == 'f' && data[i+1] == 't' && data[i+2] == 'y' && data[i+3] == 'p') {
            isMp4 = true;
            break;
        }
    }
    if (!isMp4) {
        log::warn("[VideoThumbSprite] Data does not contain valid MP4 ftyp box");
        return nullptr;
    }

    std::string tempPath;
    {
        std::lock_guard lock(s_cacheMutex);
        tempPath = getCachedPathLocked(cacheKey);
    }

    if (tempPath.empty()) {
        tempPath = getTempPath(cacheKey);
        {
            auto writeRes = geode::utils::file::writeBinary(tempPath, data);
            if (writeRes.isErr()) {
                log::error("[VideoThumbSprite] Failed to write temp file: {}", tempPath);
                return nullptr;
            }
        }
        std::lock_guard lock(s_cacheMutex);
        registerCachedPathLocked(cacheKey, tempPath);
        // Trim the on-disk cache to its budget after writing a new entry.
        enforceTempFilesBudgetLocked();
    }

    auto* sprite = create(tempPath);
    if (sprite) sprite->m_cacheKey = cacheKey;
    return sprite;
}

void VideoThumbnailSprite::createAsync(std::string const& url, std::string const& cacheKey, AsyncCallback callback) {
    if (cacheKey.empty()) {
        Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
            if (callback) callback(nullptr);
        });
        return;
    }

    std::string requestKey = makeRequestKey(url, cacheKey);
    std::string cachedPath;

    {
        std::lock_guard lock(s_cacheMutex);
        if (s_asyncShutdown) {
            Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
                if (callback) callback(nullptr);
            });
            return;
        }

        pruneRecentFailuresLocked(std::chrono::steady_clock::now());
        cachedPath = getCachedPathLocked(cacheKey);
        if (cachedPath.empty()) {
            cachedPath = getCachedPathLocked(requestKey);
        }

        if (!cachedPath.empty()) {
            // File found on disk — always try to create, even if requestKey was
            // previously in s_recentFailures. A prior decoder failure from a
            // different context (e.g. LevelCell) should not block the popup.
            s_recentFailures.erase(requestKey);
            registerCachedPathLocked(cacheKey, cachedPath);
            s_createQueue.push_back(CreateJob{requestKey, cacheKey, cachedPath, std::move(callback)});
        } else {
            if (url.empty()) {
                log::debug("[VideoThumbSprite] createAsync: empty URL for cacheKey={}", cacheKey);
                Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
                    if (callback) callback(nullptr);
                });
                return;
            }

            auto failIt = s_recentFailures.find(requestKey);
            if (failIt != s_recentFailures.end()) {
                log::info("[VideoThumbSprite] createAsync: skipping recently failed requestKey={} (cacheKey={})", requestKey, cacheKey);
                Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
                    if (callback) callback(nullptr);
                });
                return;
            }

            auto requestIt = s_downloadRequests.find(requestKey);
            if (requestIt != s_downloadRequests.end() && requestIt->second) {
                requestIt->second->callbacks.push_back(PendingCreateCallback{cacheKey, std::move(callback)});
                registerCachedPathLocked(cacheKey, requestIt->second->localPath);
            } else {
                auto request = std::make_shared<DownloadRequest>();
                request->key = requestKey;
                request->url = url;
                request->localPath = getTempPath(requestKey);
                request->callbacks.push_back(PendingCreateCallback{cacheKey, std::move(callback)});

                registerCachedPathLocked(requestKey, request->localPath);
                registerCachedPathLocked(cacheKey, request->localPath);
                s_downloadRequests[requestKey] = request;
                s_downloadQueue.push_back(requestKey);
            }
        }
    }

    pumpAsyncQueues();
}

// ── init ────────────────────────────────────────────────────────────

bool VideoThumbnailSprite::initWithPlayer(std::unique_ptr<paimon::video::VideoPlayer> player) {
    if (!player) return false;

    // Use getResolvedRGBATexture() instead of getCurrentFrameTexture():
    // when GPU YUV mode is active, getCurrentFrameTexture() returns ONLY the
    // luma (Y) plane, which renders as vertical stripes/grayscale when bound
    // to a plain CCSprite (no YUV shader). getResolvedRGBATexture() blits
    // the YUV planes to a cached RGBA FBO on the GPU and returns that.
    // This matches what LayerBackgroundManager::applyVideoBg uses for
    // non-blur video backgrounds (e.g. MenuLayer), so the profile and other
    // layers that go through VideoThumbnailSprite render correctly too.
    auto* tex = player->hasVisibleFrame() ? player->getResolvedRGBATexture() : nullptr;
    if (!tex) {
        // Pre-init with a 1x1 white pixel as placeholder
        if (!CCSprite::init()) return false;
    } else {
        if (!CCSprite::initWithTexture(tex)) return false;
        m_firstFrame = true;
    }

    m_player = std::move(player);
    m_player->setLoop(true);
    m_player->setVolume(0.0f); // muted autoplay

    // Ensure contentSize matches actual video dimensions even if the
    // pre-allocated texture was not available at init time (CCSprite::init
    // gives a tiny 1x1 sprite).  Scale calculations in displayVideoThumbnail
    // and LevelCell rely on contentSize being correct.
    // NOTE: Do NOT set textureRect to video dimensions when the actual texture
    // is 1x1 — this causes the placeholder pixel to be UV-mapped across the
    // full rect, creating glitchy edge artifacts. The textureRect will be
    // properly set in update() when the real frame arrives.
    int vw = m_player->getVideoWidth();
    int vh = m_player->getVideoHeight();
    if (vw > 0 && vh > 0) {
        auto videoSize = cocos2d::CCSizeMake(static_cast<float>(vw), static_cast<float>(vh));
        if (this->getContentSize().width < 2.f || this->getContentSize().height < 2.f) {
            this->setContentSize(videoSize);
        }
    }

    return true;
}

VideoThumbnailSprite::~VideoThumbnailSprite() {
    this->unscheduleUpdate();
    // Always release the global active slot so the budget recovers even if
    // the sprite was destroyed while still mid-update (e.g. parent removed).
    releaseActiveSlot();
    if (m_player) {
        // Always stop before caching to prevent stale playback
        m_player->stop();
        // Return player to cache instead of destroying it (avoids re-decoding)
        if (!m_cacheKey.empty() && m_firstFrame) {
            returnPlayerToCache(m_cacheKey, std::move(m_player));
        } else {
            m_player.reset();
        }
    }
}

// ── playback control ────────────────────────────────────────────────

void VideoThumbnailSprite::play() {
    if (!m_player) return;
    m_playing = true;
    m_player->play();
    this->scheduleUpdate();
}

void VideoThumbnailSprite::pause() {
    if (!m_player) return;
    m_playing = false;
    m_player->pause();
    // Yield the global slot so other sprites can run while we sit paused.
    releaseActiveSlot();
}

void VideoThumbnailSprite::stop() {
    if (!m_player) return;
    m_playing = false;
    m_player->stop();
    releaseActiveSlot();
    this->unscheduleUpdate();
}

void VideoThumbnailSprite::setLoop(bool loop) {
    if (m_player) m_player->setLoop(loop);
}

void VideoThumbnailSprite::setVolume(float v) {
    if (m_player) m_player->setVolume(v);
}

bool VideoThumbnailSprite::isPlaying() const {
    return m_playing && m_player && m_player->isPlaying();
}

bool VideoThumbnailSprite::hasVisibleFrame() const {
    return m_firstFrame || (m_player && m_player->hasVisibleFrame());
}

cocos2d::CCSize VideoThumbnailSprite::getVideoSize() const {
    if (m_player && m_player->getVideoWidth() > 0 && m_player->getVideoHeight() > 0) {
        return cocos2d::CCSizeMake(
            static_cast<float>(m_player->getVideoWidth()),
            static_cast<float>(m_player->getVideoHeight())
        );
    }

    if (auto* tex = const_cast<VideoThumbnailSprite*>(this)->getTexture()) {
        auto size = tex->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            return size;
        }
    }

    auto size = this->getContentSize();
    if (size.width > 0.f && size.height > 0.f) {
        return size;
    }

    return cocos2d::CCSizeMake(1.f, 1.f);
}

void VideoThumbnailSprite::setOnFirstVisibleFrame(FrameReadyCallback callback) {
    if (!callback) {
        m_onFirstVisibleFrame = nullptr;
        return;
    }

    if (this->hasVisibleFrame()) {
        this->retain();
        callback(this);
        this->release();
        return;
    }

    m_onFirstVisibleFrame = std::move(callback);
}

// ── scene graph ─────────────────────────────────────────────────────

void VideoThumbnailSprite::onEnter() {
    CCSprite::onEnter();
    if (m_playing && m_player) {
        m_player->play();
        this->scheduleUpdate();
    }
}

void VideoThumbnailSprite::onExit() {
    if (m_player) {
        m_player->pause();
    }
    // Yield the active slot when leaving the scene graph; the next onEnter
    // call will re-acquire one inside update().
    releaseActiveSlot();
    m_offscreenAccumulator = 0.0f;
    this->unscheduleUpdate();
    CCSprite::onExit();
}

void VideoThumbnailSprite::dispatchFirstVisibleFrame() {
    if (!m_onFirstVisibleFrame) {
        return;
    }

    auto callback = std::move(m_onFirstVisibleFrame);
    m_onFirstVisibleFrame = nullptr;
    this->retain();
    callback(this);
    this->release();
}

// ── update ──────────────────────────────────────────────────────────

void VideoThumbnailSprite::update(float dt) {
    if (!m_player || !m_playing) return;

    // FIX-4: detect terminal player and self-stop. A terminal decoder will
    // never produce frames again — keep ticking it costs CPU for nothing.
    if (m_player->isTerminal()) {
        log::debug("[VideoThumbSprite] Player became terminal, stopping update loop");
        m_playing = false;
        releaseActiveSlot();
        this->unscheduleUpdate();
        return;
    }

    // Viewport culling: if the sprite is fully off-screen, skip the heavy
    // work. After a brief grace period (kOffscreenPauseThreshold) we also
    // pause the underlying decoder so its worker thread stops producing
    // frames into the ring buffer (FIX-7).
    bool offscreen = false;
    if (this->getParent()) {
        CCRect bbox = this->boundingBox();
        CCPoint worldMin = this->getParent()->convertToWorldSpace(ccp(bbox.getMinX(), bbox.getMinY()));
        CCPoint worldMax = this->getParent()->convertToWorldSpace(ccp(bbox.getMaxX(), bbox.getMaxY()));
        auto* director = cocos2d::CCDirector::get();
        CCSize visibleSize = director->getWinSize();
        // Expand viewport ~20% to avoid pop-in while scrolling.
        float padX = visibleSize.width * 0.2f;
        float padY = visibleSize.height * 0.2f;
        offscreen = (worldMax.x < -padX || worldMin.x > visibleSize.width + padX ||
                     worldMax.y < -padY || worldMin.y > visibleSize.height + padY);
    }

    if (offscreen) {
        m_offscreenAccumulator += dt;
        // After a brief grace, pause the decoder and yield the active slot.
        // Resume happens automatically on the next visible update() below.
        if (m_offscreenAccumulator >= kOffscreenPauseThreshold) {
            if (m_holdsActiveSlot) {
                releaseActiveSlot();
            }
            if (m_player->isPlaying()) {
                m_player->pause();
            }
            // Free the YUV->RGBA resolve FBO (~8 MB at 1080p).  Off-screen
            // sprites don't need it; if/when we come back on-screen the FBO
            // will be lazily recreated on the next getResolvedRGBATexture()
            // call.  This is the per-sprite analogue to the layer-background
            // TTL stale handling.
            m_player->releaseGPUResolveCache();
        }
        return;
    }

    // We are visible. Reset the off-screen timer and try to claim a slot
    // from the global budget. If the budget is full, leave the decoder
    // paused — another sprite will yield a slot soon (FIX-5).
    m_offscreenAccumulator = 0.0f;
    if (!m_holdsActiveSlot) {
        if (!tryAcquireActiveSlot()) {
            // Budget full: keep the decoder paused this frame.
            if (m_player->isPlaying()) {
                m_player->pause();
            }
            return;
        }
    }

    // We hold a slot — make sure playback is running.
    if (!m_player->isPlaying()) {
        m_player->play();
    }

    m_player->update(dt);

    // See note in initWithPlayer(): use the GPU-resolved RGBA texture so
    // the plain CCSprite renders the colour video instead of the bare Y
    // (luma) plane that getCurrentFrameTexture() exposes when GPU YUV is
    // active.  Without this fix the sprite shows the video as vertical
    // grey stripes (the most visible symptom on profile video backgrounds).
    auto* tex = m_player->getResolvedRGBATexture();
    if (!m_player->hasVisibleFrame() || !tex) {
        return;
    }

    auto size = cocos2d::CCSizeMake(
        static_cast<float>(m_player->getVideoWidth()),
        static_cast<float>(m_player->getVideoHeight())
    );

    if (tex != this->getTexture()) {
        this->setTexture(tex);
    }

    auto currentSize = this->getContentSize();
    if (!m_firstFrame || currentSize.width != size.width || currentSize.height != size.height) {
        this->setTextureRect(cocos2d::CCRectMake(0, 0, size.width, size.height));
        this->setContentSize(size);
    }

    if (!m_firstFrame) {
        m_firstFrame = true;
        saveFirstFrameToCache();
        dispatchFirstVisibleFrame();
    }
}

// ── cache management ────────────────────────────────────────────────

void VideoThumbnailSprite::removeForLevel(int levelID) {
    std::lock_guard lock(s_cacheMutex);
    std::string prefix1 = fmt::format("thumb_video_{}", levelID);
    std::string prefix2 = fmt::format("gallery_video_{}_", levelID);

    for (auto it = s_tempFiles.begin(); it != s_tempFiles.end();) {
        if (it->first == prefix1 || it->first.starts_with(prefix2)) {
            std::error_code ec;
            fs::remove(it->second, ec);
            tempFilesLastUsed().erase(it->first);
            it = s_tempFiles.erase(it);
        } else {
            ++it;
        }
    }

    // Also remove download requests and failures related to this level
    for (auto it = s_downloadRequests.begin(); it != s_downloadRequests.end();) {
        if (it->first.find(prefix1) != std::string::npos || it->first.find(prefix2) != std::string::npos) {
            it = s_downloadRequests.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = s_recentFailures.begin(); it != s_recentFailures.end();) {
        if (it->first.find(prefix1) != std::string::npos || it->first.find(prefix2) != std::string::npos) {
            it = s_recentFailures.erase(it);
        } else {
            ++it;
        }
    }

    log::debug("[VideoThumbnailSprite] removeForLevel: cleared cache for level {}", levelID);
}

void VideoThumbnailSprite::removeForCacheKey(std::string const& cacheKey) {
    if (cacheKey.empty()) return;

    std::string pathToRemove;
    {
        std::lock_guard lock(s_cacheMutex);
        auto it = s_tempFiles.find(cacheKey);
        if (it != s_tempFiles.end()) {
            pathToRemove = it->second;
            s_tempFiles.erase(it);
        }
        tempFilesLastUsed().erase(cacheKey);

        // Also drop any pending download / failure record under this key.
        s_downloadRequests.erase(cacheKey);
        s_recentFailures.erase(cacheKey);
    }
    if (!pathToRemove.empty()) {
        std::error_code ec;
        fs::remove(pathToRemove, ec);
        // Si alguien extrajo el audio de ese video (modo Audio Video del
        // perfil), tambien borrar el WAV asociado para que no quede como
        // fantasma en disco.  La extraccion se gatilla por path, asi que
        // basta con pasar el path original que acabamos de borrar.
        paimon::video::cleanupAudioCache(pathToRemove);
    }

    // Drop any cached player still holding this cacheKey alive.
    {
        std::lock_guard lock(s_playerCacheMutex);
        for (auto it = s_playerCache.begin(); it != s_playerCache.end(); ) {
            if (it->cacheKey == cacheKey) {
                if (it->player) {
                    it->player->stop();
                }
                it = s_playerCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Drop the saved first-frame preview too so the next acquire doesn't
    // briefly flash the old video.
    auto ffPath = getFirstFrameCachePath(cacheKey);
    std::error_code ec;
    fs::remove(ffPath, ec);

    log::debug("[VideoThumbnailSprite] removeForCacheKey: cleared {}", cacheKey);
}

void VideoThumbnailSprite::clearCache() {
    std::lock_guard lock(s_cacheMutex);
    s_asyncShutdown = true;
    s_downloadRequests.clear();
    s_downloadQueue.clear();
    s_createQueue.clear();
    s_recentFailures.clear();
    for (auto const& [key, path] : s_tempFiles) {
        std::error_code ec;
        fs::remove(path, ec);
    }
    s_tempFiles.clear();
    tempFilesLastUsed().clear();

    // Also try to remove entire video_cache directory
    auto dir = dirs::getModRuntimeDir() / "video_cache";
    std::error_code ec;
    fs::remove_all(dir, ec);

    // Clean the normalizer's canonical cache (in save dir)
    auto normDir = Mod::get()->getSaveDir() / "video_cache";
    fs::remove_all(normDir, ec);

    // Clear player cache
    clearPlayerCache();
}

// ── VideoPlayer cache ───────────────────────────────────────────────

std::unique_ptr<paimon::video::VideoPlayer> VideoThumbnailSprite::getCachedPlayer(std::string const& cacheKey) {
    std::lock_guard lock(s_playerCacheMutex);

    for (auto it = s_playerCache.begin(); it != s_playerCache.end(); ++it) {
        if (it->cacheKey == cacheKey && it->player) {
            // FIX-1: Validate that the cached player is still healthy before
            // handing it out. A player that became terminal (decoder thread
            // detached) or never had a visible frame will produce nothing
            // useful and may crash on play(). Discard it and let the caller
            // build a fresh player.
            auto& cached = *it;
            if (cached.player->isTerminal() || !cached.player->hasVisibleFrame()) {
                log::warn("[VideoThumbSprite] Discarding unhealthy cached player "
                          "(terminal={}, hasFrame={}) for: {}",
                          cached.player->isTerminal(),
                          cached.player->hasVisibleFrame(), cacheKey);
                cached.player->forceStop();
                cached.player.reset();
                s_playerCache.erase(it);
                return nullptr;
            }

            auto player = std::move(cached.player);
            s_playerCache.erase(it);
            log::debug("[VideoThumbSprite] Retrieved cached player for: {}", cacheKey);
            return player;
        }
    }
    return nullptr;
}

void VideoThumbnailSprite::returnPlayerToCache(std::string const& cacheKey, std::unique_ptr<paimon::video::VideoPlayer> player) {
    if (!player || cacheKey.empty()) return;

    if (MAX_CACHED_PLAYERS == 0) {
        player->stop();
        return;
    }

    // FIX-2: Don't cache a dead/never-started player. If the decoder bailed
    // (terminal) or it never produced a frame, returning it to the cache
    // means the next createFromCache hands out a broken player.
    if (player->isTerminal() || !player->hasVisibleFrame()) {
        log::debug("[VideoThumbSprite] Not caching unhealthy player "
                   "(terminal={}, hasFrame={}) for: {}",
                   player->isTerminal(), player->hasVisibleFrame(), cacheKey);
        player->forceStop();
        player.reset();
        return;
    }

    std::lock_guard lock(s_playerCacheMutex);

    // Don't cache if shutting down
    if (s_asyncShutdown) {
        player->stop();
        return;
    }

    // Check if already cached
    for (auto& cached : s_playerCache) {
        if (cached.cacheKey == cacheKey) {
            // Already have this one, just update
            cached.player = std::move(player);
            cached.lastUsed = std::chrono::steady_clock::now();
            return;
        }
    }

    // Pause the player before caching (keeps decoded frames in memory)
    player->pause();

    // Free the YUV->RGBA resolve FBO (~8 MB at 1080p) before caching.  The
    // cached player will be re-bound to a new VideoThumbnailSprite later;
    // that sprite will trigger getResolvedRGBATexture() on its first frame
    // and the FBO will be lazily recreated.  Until then, no point keeping
    // the render-target alive in VRAM.
    player->releaseGPUResolveCache();

    // Evict any cached players that have been idle longer than the TTL.
    // This catches the case where MAX_CACHED_PLAYERS > number of recently
    // used videos, leaving stale entries hanging on for the rest of the
    // session.
    constexpr auto kPlayerCacheTTL = std::chrono::seconds(30);
    auto now = std::chrono::steady_clock::now();
    for (auto it = s_playerCache.begin(); it != s_playerCache.end(); ) {
        if (now - it->lastUsed > kPlayerCacheTTL) {
            if (it->player) it->player->stop();
            log::debug("[VideoThumbSprite] Evicting cached player past TTL: {}", it->cacheKey);
            it = s_playerCache.erase(it);
        } else {
            ++it;
        }
    }

    CachedPlayer cached;
    cached.player = std::move(player);
    cached.cacheKey = cacheKey;
    cached.lastUsed = std::chrono::steady_clock::now();
    s_playerCache.push_back(std::move(cached));

    // Evict oldest if at capacity
    while (s_playerCache.size() > static_cast<size_t>(MAX_CACHED_PLAYERS)) {
        auto& oldest = s_playerCache.front();
        if (oldest.player) {
            oldest.player->stop();
        }
        s_playerCache.pop_front();
        log::debug("[VideoThumbSprite] Evicted oldest cached player");
    }

    log::debug("[VideoThumbSprite] Cached player for: {} (cache size={})", cacheKey, s_playerCache.size());
}

void VideoThumbnailSprite::clearPlayerCache() {
    std::lock_guard lock(s_playerCacheMutex);
    for (auto& cached : s_playerCache) {
        if (cached.player) {
            cached.player->stop();
        }
    }
    s_playerCache.clear();
    log::debug("[VideoThumbSprite] Cleared player cache");
}
