#include "EmoteCache.hpp"
#include "EmoteService.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/TimedJoin.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include <Geode/Geode.hpp>
#include "../../../utils/stb_image.h"
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

namespace {
void dispatchTextureCallback(EmoteCache::TextureCallback callback,
                             geode::Ref<CCTexture2D> texture,
                             bool isGif,
                             std::vector<uint8_t> gifData) {
    if (!callback) return;
    Loader::get()->queueInMainThread([callback = std::move(callback),
                                      texture = std::move(texture),
                                      isGif,
                                      gifData = std::move(gifData)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        callback(texture, isGif, gifData);
    });
}

void dispatchPreloadCallback(EmoteCache::PreloadCallback callback,
                             size_t downloaded,
                             size_t skipped,
                             size_t total) {
    if (!callback) return;
    Loader::get()->queueInMainThread([callback = std::move(callback), downloaded, skipped, total]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        callback(downloaded, skipped, total);
    });
}
} // namespace

namespace {
struct DecodedPixels {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool ok = false;
};

DecodedPixels decodeStaticPixels(std::vector<uint8_t> const& data) {
    DecodedPixels out;
    if (data.empty()) return out;

    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return out;
    }

    size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    out.rgba.assign(px, px + bytes);
    out.width = w;
    out.height = h;
    out.ok = true;
    stbi_image_free(px);
    return out;
}

CCTexture2D* pixelsToStaticTexture(DecodedPixels const& decoded) {
    if (!decoded.ok || decoded.rgba.empty()) return nullptr;

    auto* tex = new CCTexture2D();
    if (!tex->initWithData(
            decoded.rgba.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            decoded.width, decoded.height,
            CCSizeMake(static_cast<float>(decoded.width),
                       static_cast<float>(decoded.height)))) {
        tex->release();
        return nullptr;
    }
    tex->setAntiAliasTexParameters();
    tex->autorelease();
    return tex;
}

} // namespace

std::filesystem::path EmoteCache::getDiskCacheDir() const {
    return Mod::get()->getSaveDir() / "emote_cache";
}

std::filesystem::path EmoteCache::getDiskPath(std::string const& filename) const {
    return getDiskCacheDir() / filename;
}

bool EmoteCache::isDiskEntryValid(std::string const& filename) const {
    auto path = getDiskPath(filename);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;

    auto lastWrite = std::filesystem::last_write_time(path, ec);
    if (ec) return false;

    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        std::filesystem::file_time_type::clock::now() - lastWrite
    ).count();
    return age < DISK_TTL_SECONDS;
}

bool EmoteCache::loadFromDisk(std::string const& filename, std::vector<uint8_t>& outData) const {
    auto path = getDiskPath(filename);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;

    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return false;

    auto size = ifs.tellg();
    if (size <= 0) return false;
    ifs.seekg(0, std::ios::beg);

    outData.resize(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(outData.data()), size);
    return ifs.good();
}

void EmoteCache::saveToDisk(std::string const& filename, std::vector<uint8_t> const& data) {
    if (data.empty()) return;
    auto dir = getDiskCacheDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto path = dir / filename;
    std::ofstream ofs(path, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<char const*>(data.data()), data.size());
    }
}

void EmoteCache::addToRam(std::string const& name, RamEntry entry) {
    std::lock_guard lock(m_ramMutex);

    auto it = m_ramCache.find(name);
    if (it != m_ramCache.end()) {
        m_currentRamBytes -= it->second.byteSize;
        m_ramCache.erase(it);
        auto lruIt = m_lruMap.find(name);
        if (lruIt != m_lruMap.end()) {
            m_lruOrder.erase(lruIt->second);
            m_lruMap.erase(lruIt);
        }
    }

    m_currentRamBytes += entry.byteSize;
    m_ramCache[name] = std::move(entry);
    m_lruOrder.push_back(name);
    m_lruMap[name] = std::prev(m_lruOrder.end());

    evictRamIfNeeded();
}

void EmoteCache::touchLru(std::string const& name) {
    auto it = m_lruMap.find(name);
    if (it != m_lruMap.end()) {
        m_lruOrder.erase(it->second);
        m_lruOrder.push_back(name);
        it->second = std::prev(m_lruOrder.end());
    }
}

void EmoteCache::evictRamIfNeeded() {
    // Must be called with m_ramMutex held
    while ((m_ramCache.size() > MAX_RAM_ENTRIES || m_currentRamBytes > MAX_RAM_BYTES)
           && !m_lruOrder.empty()) {
        auto oldest = m_lruOrder.front();
        m_lruOrder.pop_front();
        m_lruMap.erase(oldest);

        auto it = m_ramCache.find(oldest);
        if (it != m_ramCache.end()) {
            m_currentRamBytes -= it->second.byteSize;
            m_ramCache.erase(it);
        }
    }
}

size_t EmoteCache::ramCacheCount() const {
    std::lock_guard lock(m_ramMutex);
    return m_ramCache.size();
}

bool EmoteCache::isInRamCache(std::string const& name) const {
    std::lock_guard lock(m_ramMutex);
    return m_ramCache.find(name) != m_ramCache.end();
}

void EmoteCache::loadEmote(EmoteInfo const& info, TextureCallback callback) {
    {
        geode::Ref<CCTexture2D> cachedTexture = nullptr;
        std::vector<uint8_t> cachedGifData;
        EmoteType cachedType = EmoteType::Static;
        bool hasHit = false;

        {
            std::lock_guard lock(m_ramMutex);
            auto it = m_ramCache.find(info.name);
            if (it != m_ramCache.end()) {
                touchLru(info.name);
                cachedType = it->second.type;
                cachedTexture = it->second.texture;
                cachedGifData = it->second.gifData;
                hasHit = true;
            }
        }

        if (hasHit) {
            if (cachedType == EmoteType::Gif) {
                dispatchTextureCallback(std::move(callback), nullptr, true, std::move(cachedGifData));
            } else {
                dispatchTextureCallback(std::move(callback), std::move(cachedTexture), false, {});
            }
            return;
        }
    }

    paimon::ThreadTracker::get().spawn([this, info, callback = std::move(callback)]() mutable {
        if (paimon::isRuntimeShuttingDown()) return;
        if (isDiskEntryValid(info.filename)) {
            std::vector<uint8_t> diskData;
            if (loadFromDisk(info.filename, diskData)) {
                if (info.type == EmoteType::Gif) {
                    geode::Loader::get()->queueInMainThread([this, info, diskData = std::move(diskData), cb = std::move(callback)]() mutable {
                        if (paimon::isRuntimeShuttingDown()) return;
                        RamEntry entry;
                        entry.type = EmoteType::Gif;
                        entry.gifData = diskData;
                        entry.byteSize = diskData.size();
                        entry.cachedAt = std::chrono::steady_clock::now();
                        addToRam(info.name, std::move(entry));
                        dispatchTextureCallback(std::move(cb), nullptr, true, std::move(diskData));
                    });
                    return;
                }

                DecodeTask task;
                task.info = info;
                task.data = std::move(diskData);
                task.callback = std::move(callback);
                enqueueDecode(std::move(task));
                return;
            }
        }

        geode::Loader::get()->queueInMainThread([this, info, cb = std::move(callback)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto emoteName = info.name;
            auto emoteFilename = info.filename;
            auto emoteType = info.type;
            auto emoteUrl = info.url;
            auto emoteInfo = info;

            HttpClient::get().downloadFromUrlRaw(info.url, [this, emoteInfo, emoteName, emoteFilename, emoteType, emoteUrl, callback = std::move(cb)](
                bool success, std::vector<uint8_t> const& data, int, int) mutable {

                if (paimon::isRuntimeShuttingDown()) return;

                if (!success || data.empty()) {
                    log::warn("[EmoteCache] Failed to download emote: {} (url: {})", emoteName, emoteUrl);
                    dispatchTextureCallback(std::move(callback), nullptr, false, {});
                    return;
                }

                if (emoteType == EmoteType::Gif) {
                    saveToDisk(emoteFilename, data);

                    RamEntry entry;
                    entry.type = EmoteType::Gif;
                    entry.gifData = data;
                    entry.byteSize = data.size();
                    entry.cachedAt = std::chrono::steady_clock::now();
                    addToRam(emoteName, std::move(entry));
                    dispatchTextureCallback(std::move(callback), nullptr, true, std::vector<uint8_t>(data.begin(), data.end()));
                    return;
                }

                saveToDisk(emoteFilename, data);

                DecodeTask task;
                task.info = emoteInfo;
                task.data = data;
                task.callback = std::move(callback);
                enqueueDecode(std::move(task));
            });
        });
    });
}

void EmoteCache::shutdown() {
    cancelPreload();
    shutdownDecodeWorker();
}

void EmoteCache::clearAll() {
    cancelPreload();

    clearRam();

    std::error_code ec;
    auto dir = getDiskCacheDir();
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
    }

    log::info("[EmoteCache] All caches cleared");
}

void EmoteCache::clearRam() {
    std::lock_guard lock(m_ramMutex);
    m_ramCache.clear();
    m_lruOrder.clear();
    m_lruMap.clear();
    m_currentRamBytes = 0;
    log::info("[EmoteCache] RAM cache cleared");
}

void EmoteCache::cancelPreload() {
    m_preloadCancel.store(true, std::memory_order_release);
}

void EmoteCache::preloadAllToDisk(PreloadCallback callback, PreloadProgressCallback progressCallback) {
    if (m_preloading.exchange(true, std::memory_order_acq_rel)) {
        dispatchPreloadCallback(std::move(callback), 0, 0, 0);
        return;
    }
    m_preloadCancel.store(false, std::memory_order_release);

    auto allEmotes = EmoteService::get().getAllEmotes();
    if (allEmotes.empty()) {
        m_preloading.store(false, std::memory_order_release);
        dispatchPreloadCallback(std::move(callback), 0, 0, 0);
        return;
    }

    auto idx = std::make_shared<size_t>(0);
    auto emotes = std::make_shared<std::vector<EmoteInfo>>(std::move(allEmotes));
    auto skipped = std::make_shared<size_t>(0);
    auto downloaded = std::make_shared<size_t>(0);
    auto cb = std::make_shared<PreloadCallback>(std::move(callback));
    auto progressCb = std::make_shared<PreloadProgressCallback>(std::move(progressCallback));

    auto reportProgress = [progressCb](size_t completed, size_t total) {
        if (!progressCb || !*progressCb) return;
        Loader::get()->queueInMainThread([progressCb, completed, total]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (*progressCb) (*progressCb)(completed, total);
        });
    };

    auto downloadNext = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakDownloadNext = downloadNext;
    *downloadNext = [this, idx, emotes, skipped, downloaded, weakDownloadNext, cb, reportProgress]() {
        if (m_preloadCancel.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            log::info("[EmoteCache] Preload cancelled ({}/{} done, {} skipped)",
                *downloaded, emotes->size(), *skipped);
            m_preloading.store(false, std::memory_order_release);
            reportProgress(*downloaded + *skipped, emotes->size());
            if (*cb) {
                size_t d = *downloaded, s = *skipped, t = emotes->size();
                dispatchPreloadCallback(*cb, d, s, t);
            }
            return;
        }

        while (*idx < emotes->size()) {
            auto const& info = (*emotes)[*idx];
            if (isDiskEntryValid(info.filename)) {
                ++(*skipped);
                ++(*idx);
                reportProgress(*downloaded + *skipped, emotes->size());
                continue;
            }
            break;
        }

        if (*idx >= emotes->size()) {
            log::info("[EmoteCache] Preload complete: {} downloaded, {} already cached",
                *downloaded, *skipped);
            m_preloading.store(false, std::memory_order_release);
            reportProgress(*downloaded + *skipped, emotes->size());
            if (*cb) {
                size_t d = *downloaded, s = *skipped, t = emotes->size();
                dispatchPreloadCallback(*cb, d, s, t);
            }
            return;
        }

        auto const& info = (*emotes)[*idx];
        auto filename = info.filename;
        auto url = info.url;
        ++(*idx);

        // Strong ref held only by the in-flight download + its main-thread
        // re-invoke; the closure itself holds a weak self-ref (avoids leak cycle).
        auto strongNext = weakDownloadNext.lock();
        if (!strongNext) return;
        HttpClient::get().downloadFromUrlRaw(url, [this, filename, downloaded, skipped, emotes, strongNext, reportProgress](
            bool success, std::vector<uint8_t> const& data, int, int) {

            if (success && !data.empty()) {
                saveToDisk(filename, data);
                ++(*downloaded);
            }
            reportProgress(*downloaded + *skipped, emotes->size());

            Loader::get()->queueInMainThread([strongNext]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (*strongNext) (*strongNext)();
            });
        });
    };

    log::info("[EmoteCache] Starting background preload of {} emotes", emotes->size());
    reportProgress(0, emotes->size());
    (*downloadNext)();
}

void EmoteCache::initDecodeWorker() {
    // Guard the whole check-then-spawn: enqueueDecode() calls this from the
    // per-emote worker threads (loadEmote's spawn), so without the lock two
    // threads could both pass a lock-free check, both spawn workers, and both
    // emplace_back into m_decodeWorkers concurrently (vector data race +
    // double the pool). enqueueDecode() takes this same mutex only afterwards,
    // so there is no re-entrant lock.
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (m_decodeRunning.load(std::memory_order_acquire)) return;

    m_decodeShutdown.store(false, std::memory_order_release);
    m_decodeRunning.store(true, std::memory_order_release);

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    constexpr int NUM_DECODE_WORKERS = 1;
#else
    constexpr int NUM_DECODE_WORKERS = 2;
#endif
    m_decodeWorkers.reserve(NUM_DECODE_WORKERS);
    for (int i = 0; i < NUM_DECODE_WORKERS; ++i) {
        m_decodeWorkers.emplace_back(&EmoteCache::decodeWorkerLoop, this);
    }
}

void EmoteCache::shutdownDecodeWorker() {
    if (!m_decodeRunning.load(std::memory_order_acquire)) return;

    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        m_decodeShutdown.store(true, std::memory_order_release);
        m_decodeRunning.store(false, std::memory_order_release);
        m_decodeQueue.clear();
    }
    m_decodeCV.notify_all();

    for (auto& t : m_decodeWorkers) {
        if (t.joinable()) {
            if (!paimon::timedJoin(t, std::chrono::seconds(5))) {
                geode::log::warn("[EmoteCache] Decode worker did not finish in 5s, detaching");
            }
        }
    }
    m_decodeWorkers.clear();
}

void EmoteCache::enqueueDecode(DecodeTask task) {
    initDecodeWorker();
    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        m_decodeQueue.push_back(std::move(task));
    }
    m_decodeCV.notify_one();
}

void EmoteCache::decodeWorkerLoop(EmoteCache* self) {
    while (true) {
        DecodeTask task;
        {
            std::unique_lock<std::mutex> lock(self->m_decodeMutex);
            self->m_decodeCV.wait(lock, [self]() {
                return !self->m_decodeQueue.empty() ||
                       !self->m_decodeRunning.load(std::memory_order_acquire);
            });

            if (!self->m_decodeRunning.load(std::memory_order_acquire) &&
                self->m_decodeQueue.empty()) {
                return;
            }

            task = std::move(self->m_decodeQueue.front());
            self->m_decodeQueue.pop_front();
        }

        auto decoded = decodeStaticPixels(task.data);
        if (!decoded.ok) {
            // stbi failed: fall back to CCImage (safe off-thread; only CCTexture2D needs GL).
            auto* ccImg = new CCImage();
            if (!ccImg->initWithImageData(const_cast<uint8_t*>(task.data.data()), task.data.size())) {
                ccImg->release();
                log::warn("[EmoteCache] Static decode failed for emote '{}', purging cached file", task.info.name);
                std::error_code ec;
                std::filesystem::remove(self->getDiskPath(task.info.filename), ec);
                if (task.callback) {
                    auto cb = std::move(task.callback);
                    Loader::get()->queueInMainThread([cb = std::move(cb)]() mutable {
                        if (paimon::isRuntimeShuttingDown()) return;
                        cb(nullptr, false, {});
                    });
                }
                continue;
            }

            EmoteInfo info = std::move(task.info);
            size_t rawBytesSize = task.data.size();
            auto cb = std::move(task.callback);

            Loader::get()->queueInMainThread(
                [self, info = std::move(info), rawBytesSize, cb = std::move(cb), ccImg]() mutable {
                    if (paimon::isRuntimeShuttingDown()) {
                        ccImg->release();
                        return;
                    }
                    auto* tex = new CCTexture2D();
                    if (!tex->initWithImage(ccImg)) {
                        tex->release();
                        ccImg->release();
                        if (cb) cb(nullptr, false, {});
                        return;
                    }
                    tex->setAntiAliasTexParameters();
                    ccImg->release();
                    tex->autorelease();

                    RamEntry entry;
                    entry.type = EmoteType::Static;
                    entry.texture = tex;
                    entry.byteSize = rawBytesSize;
                    entry.cachedAt = std::chrono::steady_clock::now();
                    self->addToRam(info.name, std::move(entry));
                    if (cb) cb(tex, false, {});
                });
            continue;
        }

        size_t origSize = task.data.size();
        EmoteInfo info = std::move(task.info);
        auto cb = std::move(task.callback);
        std::vector<uint8_t> rgba = std::move(decoded.rgba);
        int w = decoded.width;
        int h = decoded.height;

        self->finalizeStaticDecodeOnMainThread(
            std::move(info), std::move(rgba), w, h, origSize, std::move(cb));
    }
}

void EmoteCache::finalizeStaticDecodeOnMainThread(EmoteInfo info,
                                                  std::vector<uint8_t> rgba,
                                                  int width, int height,
                                                  size_t originalByteSize,
                                                  TextureCallback callback) {
    Loader::get()->queueInMainThread(
        [this, info = std::move(info), rgba = std::move(rgba),
         width, height, originalByteSize, callback = std::move(callback)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            DecodedPixels decoded;
            decoded.rgba = std::move(rgba);
            decoded.width = width;
            decoded.height = height;
            decoded.ok = true;

            auto* tex = pixelsToStaticTexture(decoded);
            if (!tex) {
                log::warn("[EmoteCache] GL upload failed for emote '{}'", info.name);
                if (callback) callback(nullptr, false, {});
                return;
            }

            RamEntry entry;
            entry.type = EmoteType::Static;
            entry.texture = tex;
            entry.byteSize = originalByteSize;
            entry.cachedAt = std::chrono::steady_clock::now();
            addToRam(info.name, std::move(entry));

            if (callback) callback(tex, false, {});
        });
}
