// VideoThumbnailSprite.cpp — CCSprite wrapper for video thumbnails.

#include "VideoThumbnailSprite.hpp"
#include "WebHelper.hpp"
#include "../core/Settings.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <algorithm>

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
            return it->second;
        }
        s_tempFiles.erase(it);
    }

    auto fallback = getTempPath(key);
    std::error_code ec;
    if (fs::exists(fallback, ec)) {
        s_tempFiles[key] = fallback;
        return fallback;
    }
    return {};
}

void VideoThumbnailSprite::registerCachedPathLocked(std::string const& key, std::string const& path) {
    if (!key.empty() && !path.empty()) {
        s_tempFiles[key] = path;
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

    // Write asynchronously to avoid blocking the main thread
    std::thread([cachePath, pixels = std::move(pixels), w, h]() {
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
        (void)geode::utils::file::writeBinary(cachePath, buf);
    }).detach();
}

bool VideoThumbnailSprite::loadFirstFrameFromCache(std::string const& videoPath) {
    std::string cachePath = getFirstFrameCachePath(videoPath);

    auto readRes = geode::utils::file::readBinary(cachePath);
    if (readRes.isErr()) return false;
    auto& buf = readRes.unwrap();
    if (buf.size() < sizeof(uint32_t) * 2) return false;

    uint32_t w = 0, h = 0;
    std::memcpy(&w, buf.data(), sizeof(w));
    std::memcpy(&h, buf.data() + sizeof(h), sizeof(h));
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

VideoThumbnailSprite* VideoThumbnailSprite::createFromCache(std::string const& cacheKey) {
    std::string path;
    {
        std::lock_guard lock(s_cacheMutex);
        path = getCachedPathLocked(cacheKey);
    }
    if (path.empty()) return nullptr;
    auto* sprite = create(path);
    if (sprite) sprite->m_cacheKey = cacheKey;
    return sprite;
}

VideoThumbnailSprite* VideoThumbnailSprite::create(std::string const& filePath) {
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

    auto* tex = player->hasVisibleFrame() ? player->getCurrentFrameTexture() : nullptr;
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
    if (m_player) {
        // Return player to cache instead of destroying it (avoids re-decoding)
        if (!m_cacheKey.empty() && m_firstFrame) {
            returnPlayerToCache(m_cacheKey, std::move(m_player));
        } else {
            m_player->stop();
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
}

void VideoThumbnailSprite::stop() {
    if (!m_player) return;
    m_playing = false;
    m_player->stop();
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

    m_player->update(dt);

    auto* tex = m_player->getCurrentFrameTexture();
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
            auto player = std::move(it->player);
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

    // Pause BEFORE taking the lock.  stopDecoding() joins the decode thread,
    // which may take tens of milliseconds (GPU readback, codec flush, etc.).
    // Holding s_playerCacheMutex during that wait would starve any concurrent
    // getCachedPlayer() call and block the main thread unnecessarily.
    player->pause();

    // Collect players that need to be stopped outside the lock so we don't
    // block while holding s_playerCacheMutex.
    std::vector<std::unique_ptr<paimon::video::VideoPlayer>> toStop;

    {
        std::lock_guard lock(s_playerCacheMutex);

        if (s_asyncShutdown) {
            toStop.push_back(std::move(player));
        } else {
            // Check if this cacheKey is already in the cache — update in place.
            bool foundExisting = false;
            for (auto& cached : s_playerCache) {
                if (cached.cacheKey == cacheKey) {
                    if (cached.player) toStop.push_back(std::move(cached.player));
                    cached.player = std::move(player);
                    cached.lastUsed = std::chrono::steady_clock::now();
                    foundExisting = true;
                    break;
                }
            }

            if (!foundExisting) {
                CachedPlayer cached;
                cached.player = std::move(player);
                cached.cacheKey = cacheKey;
                cached.lastUsed = std::chrono::steady_clock::now();
                s_playerCache.push_back(std::move(cached));

                // Collect oldest players to evict (without stopping them yet).
                while (s_playerCache.size() > static_cast<size_t>(MAX_CACHED_PLAYERS)) {
                    if (s_playerCache.front().player) {
                        toStop.push_back(std::move(s_playerCache.front().player));
                    }
                    s_playerCache.pop_front();
                    log::debug("[VideoThumbSprite] Evicted oldest cached player");
                }
            }
        }

        log::debug("[VideoThumbSprite] Cached player for: {} (cache size={})", cacheKey, s_playerCache.size());
    }

    // Stop evicted players OUTSIDE the lock to avoid blocking while holding mutex.
    for (auto& p : toStop) {
        if (p) p->stop();
    }
}

void VideoThumbnailSprite::clearPlayerCache() {
    // Move all cached players out while holding the lock, then stop them
    // outside the lock so we don't block getCachedPlayer() callers.
    std::deque<CachedPlayer> toStop;
    {
        std::lock_guard lock(s_playerCacheMutex);
        toStop = std::move(s_playerCache);
        s_playerCache.clear();
    }
    for (auto& cached : toStop) {
        if (cached.player) {
            cached.player->stop();
        }
    }
    log::debug("[VideoThumbSprite] Cleared player cache");
}
