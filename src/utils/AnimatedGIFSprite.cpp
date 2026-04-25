#include "AnimatedGIFSprite.hpp"
#include "DominantColors.hpp"
#include "Debug.hpp"
#include "../core/QualityConfig.hpp"
#include <Geode/loader/Log.hpp>
#include <fstream>
#include <filesystem>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include "TimedJoin.hpp"

using namespace geode::prelude;

static float getContentScaleFactorSafe() {
    // NOTE: GD/Geode UI layout assumes these GIF frames in point-space 1:1.
    // Using device contentScaleFactor here causes double-scaling in several
    // containers (LevelCell/InfoLayer/preview). Keep logical scale at 1.
    return 1.0f;
}

static size_t getSharedGIFDataSize(AnimatedGIFSprite::SharedGIFData const& data) {
    size_t entrySize = 0;
    for (auto* tex : data.textures) {
        if (!tex) continue;
        entrySize += tex->getPixelsWide() * tex->getPixelsHigh() * 4;
    }
    return entrySize;
}

std::unordered_map<std::string, AnimatedGIFSprite::SharedGIFData> AnimatedGIFSprite::s_gifCache;
std::list<std::string> AnimatedGIFSprite::s_lruList;
std::unordered_map<std::string, std::list<std::string>::iterator> AnimatedGIFSprite::s_lruMap;
std::unordered_set<std::string> AnimatedGIFSprite::s_pinnedGIFs;
std::mutex AnimatedGIFSprite::s_cacheMutex;
size_t AnimatedGIFSprite::s_currentCacheSize = 0;

size_t AnimatedGIFSprite::getMaxCacheMem() {
    bool ramCache = Mod::get()->getSettingValue<bool>("gif-ram-cache");
    if (ramCache) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        return 80 * 1024 * 1024; // 80 MB en movil
#else
        return 150 * 1024 * 1024; // 150 MB en desktop (was 300)
#endif
    } else {
        return 10 * 1024 * 1024; // 10 MB (cache minimo pa la escena actual)
    }
}

// cosas estaticas de la cola de workers
std::deque<AnimatedGIFSprite::GIFTask> AnimatedGIFSprite::s_taskQueue;
std::mutex AnimatedGIFSprite::s_queueMutex;
std::condition_variable AnimatedGIFSprite::s_queueCV;
std::thread AnimatedGIFSprite::s_workerThread;
std::atomic<bool> AnimatedGIFSprite::s_workerRunning = false;
std::atomic<bool> AnimatedGIFSprite::s_shutdownMode = false;
std::mutex AnimatedGIFSprite::s_workerLifecycleMutex;

void AnimatedGIFSprite::initWorker() {
    std::lock_guard<std::mutex> lock(s_workerLifecycleMutex);
    s_shutdownMode.store(false, std::memory_order_release);
    if (!s_workerRunning.load(std::memory_order_acquire)) {
        s_workerRunning.store(true, std::memory_order_release);
        s_workerThread = std::thread(workerLoop);
        PaimonDebug::log("[AnimatedGIFSprite] Worker thread started");
    }
}

void AnimatedGIFSprite::shutdownWorker() {
    std::lock_guard<std::mutex> lock(s_workerLifecycleMutex);
    if (!s_workerRunning.load(std::memory_order_acquire)) return;
    {
        // flag y clear bajo el mismo lock para que el worker los vea atomicamente
        std::lock_guard<std::mutex> queueLock(s_queueMutex);
        s_workerRunning.store(false, std::memory_order_release);
        s_taskQueue.clear();
    }
    s_queueCV.notify_all();
    if (s_workerThread.joinable()) {
        paimon::timedJoin(s_workerThread, std::chrono::seconds(3));
    }
}

// version del formato de cache en disco (cambiar si se modifica el formato)
static constexpr uint32_t DISK_CACHE_VERSION = 2;

// eviccion centralizada: quita entradas LRU hasta que el cache esta por debajo del limite
// debe llamarse con s_cacheMutex pillado
void AnimatedGIFSprite::evictIfNeeded() {
    size_t maxMem = getMaxCacheMem();
    // Evict down to 90% of max to leave headroom and avoid
    // re-triggering eviction on the very next insert
    size_t targetMem = maxMem * 9 / 10;
    while (s_currentCacheSize > targetMem && !s_lruList.empty()) {
        std::string toRemove = s_lruList.front();
        s_lruList.pop_front();
        s_lruMap.erase(toRemove);
        auto it = s_gifCache.find(toRemove);
        if (it != s_gifCache.end()) {
            size_t removeSize = 0;
            for (auto* tex : it->second.textures) {
                if (!tex) continue;
                removeSize += tex->getPixelsWide() * tex->getPixelsHigh() * 4;
                tex->release();
            }
            if (s_currentCacheSize >= removeSize) s_currentCacheSize -= removeSize;
            else s_currentCacheSize = 0;
            s_gifCache.erase(it);
        }
    }
}

void AnimatedGIFSprite::pinGIF(std::string const& key) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_pinnedGIFs.insert(key);
    // lo saco de la lista LRU si estaba, asi no se lo carga la limpieza
    auto lruIt = s_lruMap.find(key);
    if (lruIt != s_lruMap.end()) {
        s_lruList.erase(lruIt->second);
        s_lruMap.erase(lruIt);
    }
}

void AnimatedGIFSprite::unpinGIF(std::string const& key) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    if (s_pinnedGIFs.erase(key)) {
        // si lo despincho, lo meto de vuelta en la LRU
        // solo si sigue en el cache
        if (s_gifCache.find(key) != s_gifCache.end()) {
            s_lruList.push_back(key);
            s_lruMap[key] = std::prev(s_lruList.end());
            evictIfNeeded();
        }
    }
}

bool AnimatedGIFSprite::isPinned(std::string const& key) {
    // caller must hold s_cacheMutex or be on main thread with no concurrent access
    return s_pinnedGIFs.find(key) != s_pinnedGIFs.end();
}

AnimatedGIFSprite* AnimatedGIFSprite::create(std::string const& filename) {
    auto ret = new AnimatedGIFSprite();
    if (ret && ret->init()) {
        ret->autorelease();
        ret->m_filename = filename;
        
        if (ret->initFromCache(filename)) {
            return ret;
        }
        
        // carga sincrona
        std::ifstream file(filename, std::ios::binary);
        if (!file) return nullptr;
        
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        if (!imgp::formats::isGif(data.data(), data.size())) return nullptr;
        
        auto decodeResult = imgp::decode::gif(data.data(), data.size());
        if (!decodeResult.isOk()) return nullptr;
        auto& decoded = decodeResult.unwrap();
        auto* animPtr = std::get_if<imgp::DecodedAnimation>(&decoded);
        if (!animPtr || animPtr->frames.empty()) return nullptr;
        auto& gifData = *animPtr;

        float sf = getContentScaleFactorSafe();
        
        // lo guardo en cache
        SharedGIFData sharedData;
        sharedData.width = gifData.width;
        sharedData.height = gifData.height;
        
        for (auto const& frame : gifData.frames) {
            auto texture = new CCTexture2D();
            if (!texture->initWithData(
                frame.data.get(),
                kCCTexture2DPixelFormat_RGBA8888,
                gifData.width,
                gifData.height,
                CCSize(gifData.width / sf, gifData.height / sf)
            )) {
                texture->release();
                continue;
            }
            texture->setAntiAliasTexParameters();
            
            sharedData.textures.push_back(texture);
            sharedData.delays.push_back(frame.delay / 1000.0f);
            sharedData.frameRects.push_back(CCRect(0, 0, gifData.width, gifData.height));
        }
        
        {
            std::lock_guard<std::mutex> lock(s_cacheMutex);
            // guardo la entrada en cache
            s_gifCache[filename] = sharedData;

            // calculo tamano aproximado en RAM
            s_currentCacheSize += getSharedGIFDataSize(sharedData);

            if (!isPinned(filename)) {
                s_lruList.push_back(filename);
                s_lruMap[filename] = std::prev(s_lruList.end());
            }
            evictIfNeeded();
        }
        
        // lo vuelvo a inicializar pero ya tirando del cache
        ret->initFromCache(filename);
        
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

AnimatedGIFSprite* AnimatedGIFSprite::create(const void* data, size_t size) {
    auto ret = new AnimatedGIFSprite();
    if (ret && ret->init()) {
        ret->autorelease();
        ret->m_filename = "memory"; // nombre fake, solo indica que viene de memoria
        
        if (!imgp::formats::isGif(data, size)) return nullptr;
        
        auto decodeResult = imgp::decode::gif(data, size);
        if (!decodeResult.isOk()) return nullptr;
        auto& decoded = decodeResult.unwrap();
        auto* animPtr = std::get_if<imgp::DecodedAnimation>(&decoded);
        if (!animPtr || animPtr->frames.empty()) return nullptr;
        auto& gifData = *animPtr;
        
        // GIFs en memoria no se cachean globalmente salvo que tengamos key
        
        ret->m_canvasWidth = gifData.width;
        ret->m_canvasHeight = gifData.height;

        float sf = getContentScaleFactorSafe();
        
        for (auto const& frame : gifData.frames) {
            auto texture = new CCTexture2D();
            if (!texture->initWithData(
                frame.data.get(),
                kCCTexture2DPixelFormat_RGBA8888,
                gifData.width,
                gifData.height,
                CCSize(gifData.width / sf, gifData.height / sf)
            )) {
                texture->release();
                continue;
            }
            texture->setAntiAliasTexParameters();
            
            auto* gifFrame = new GIFFrame();
            gifFrame->texture = texture;
            gifFrame->delay = frame.delay / 1000.0f;
            gifFrame->rect = CCRect(0, 0, gifData.width, gifData.height);
            ret->m_frames.push_back(gifFrame);
            
            ret->m_frameColors.push_back({ {0,0,0}, {255,255,255} });
        }
        
        ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));
        ret->setCurrentFrame(0);
        ret->scheduleUpdate();
        
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void AnimatedGIFSprite::updateTextureLoading(float dt) {
    if (m_pendingFrames.empty()) {
        this->unschedule(schedule_selector(AnimatedGIFSprite::updateTextureLoading));
        
        SharedGIFData cacheEntry;
        cacheEntry.width = m_canvasWidth;
        cacheEntry.height = m_canvasHeight;
        
        for (auto* frame : m_frames) {
            cacheEntry.textures.push_back(frame->texture);
            cacheEntry.delays.push_back(frame->delay);
            cacheEntry.frameRects.push_back(frame->rect);
        }
        
        {
            std::lock_guard<std::mutex> lock(s_cacheMutex);
            s_gifCache[m_filename] = cacheEntry;
            
            // calculo tamano
            s_currentCacheSize += getSharedGIFDataSize(cacheEntry);

            // actualizo lru O(1)
            if (!isPinned(m_filename)) {
                auto lruIt = s_lruMap.find(m_filename);
                if (lruIt != s_lruMap.end()) {
                    s_lruList.erase(lruIt->second);
                }
                s_lruList.push_back(m_filename);
                s_lruMap[m_filename] = std::prev(s_lruList.end());
            }
            evictIfNeeded();
        }

        this->scheduleUpdate();
        return;
    }

    // Process multiple frames per update to reach full playback faster.
    // Desktop can afford more GPU uploads per frame; mobile stays conservative.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    int framesToProcess = 1;
#else
    int framesToProcess = 3;
#endif
    float sf = getContentScaleFactorSafe();
    
    while (framesToProcess > 0 && !m_pendingFrames.empty()) {
        auto frameData = m_pendingFrames.front();
        
        auto* gifFrame = new GIFFrame();
        auto* texture = new CCTexture2D();
        
        bool success = texture->initWithData(
            frameData.pixels.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            frameData.width,
            frameData.height,
            CCSize(frameData.width / sf, frameData.height / sf)
        );

        if (success) {
            texture->setAntiAliasTexParameters();
            gifFrame->texture = texture;
            gifFrame->delay = frameData.delayMs / 1000.0f;
            gifFrame->rect = CCRect(frameData.left, frameData.top, frameData.width, frameData.height);

            m_frames.push_back(gifFrame);
            m_frameColors.push_back({ {0,0,0}, {255,255,255} });
            
            texture->retain();
        } else {
            delete gifFrame;
            texture->release();
        }
        
        m_pendingFrames.pop_front();
        framesToProcess--;
    }
    
    if (m_frames.size() == 1) {
        this->setCurrentFrame(0);
    }

    // Start animation as soon as 2 frames are available instead of
    // waiting for all pending frames to finish loading.
    // Verify both frame 0 and 1 have valid textures before starting.
    if (m_frames.size() == 2 && m_isPlaying &&
        m_frames[0] && m_frames[0]->texture &&
        m_frames[1] && m_frames[1]->texture) {
        this->scheduleUpdate();
    }
}

bool AnimatedGIFSprite::processNextPendingFrame() {
    if (m_pendingFrames.empty()) return false;

    float sf = getContentScaleFactorSafe();
    auto frameData = m_pendingFrames.front();

    auto* gifFrame = new GIFFrame();
    auto* texture = new CCTexture2D();

    bool success = false;
    {
        success = texture->initWithData(
            frameData.pixels.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            frameData.width,
            frameData.height,
            CCSize(frameData.width / sf, frameData.height / sf)
        );
    }

    if (success) {
        texture->setAntiAliasTexParameters();
        gifFrame->texture = texture;
        gifFrame->delay = frameData.delayMs / 1000.0f;
        gifFrame->rect = CCRect(frameData.left, frameData.top, frameData.width, frameData.height);

        m_frames.push_back(gifFrame);
        m_frameColors.push_back({ {0,0,0}, {255,255,255} });
        texture->retain();
    } else {
        delete gifFrame;
        texture->release();
    }

    m_pendingFrames.pop_front();

    if (success && m_frames.size() == 1) {
        this->setCurrentFrame(0);
    }

    return success;
}

std::string AnimatedGIFSprite::getCachePath(std::string const& path) {
    auto cacheDir = getDiskCacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        std::filesystem::create_directories(cacheDir, ec);
    }
    
    std::hash<std::string> hasher;
    auto hash = hasher(path);
    return geode::utils::string::pathToString(cacheDir / (std::to_string(hash) + ".bin"));
}

std::filesystem::path AnimatedGIFSprite::getDiskCacheDir() {
    return paimon::quality::cacheDir() / "gifs";
}

void AnimatedGIFSprite::pruneDiskCache() {
    auto cacheDir = getDiskCacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) return;

    struct CacheEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
        size_t bytes;
    };
    std::vector<CacheEntry> entries;
    size_t totalBytes = 0;

    for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        if (geode::utils::string::toLower(geode::utils::string::pathToString(entry.path().extension())) != ".bin") continue;
        size_t bytes = static_cast<size_t>(entry.file_size(ec));
        if (ec) continue;
        auto mtime = std::filesystem::last_write_time(entry.path(), ec);
        if (ec) continue;
        entries.push_back({entry.path(), mtime, bytes});
        totalBytes += bytes;
    }

    auto now = std::filesystem::file_time_type::clock::now();
    for (auto const& e : entries) {
        auto age = std::chrono::duration_cast<std::chrono::hours>(now - e.mtime);
        if (age > MAX_DISK_CACHE_AGE) {
            std::filesystem::remove(e.path, ec);
            if (!ec) totalBytes = (totalBytes >= e.bytes) ? (totalBytes - e.bytes) : 0;
        }
    }

    if (totalBytes <= paimon::settings::quality::diskCacheBytes()) return;
    std::sort(entries.begin(), entries.end(), [](CacheEntry const& a, CacheEntry const& b) {
        return a.mtime < b.mtime;
    });
    for (auto const& e : entries) {
        if (totalBytes <= paimon::settings::quality::diskCacheBytes()) break;
        std::filesystem::remove(e.path, ec);
        if (!ec) totalBytes = (totalBytes >= e.bytes) ? (totalBytes - e.bytes) : 0;
    }
}

bool AnimatedGIFSprite::loadFromDiskCache(std::string const& path, DiskCacheEntry& outEntry) {
    auto cachePath = getCachePath(path);
    std::error_code existsEc;
    if (!std::filesystem::exists(cachePath, existsEc) || existsEc) return false;

    // miro la fecha de modificacion
    std::error_code ec;
    auto cacheTime = std::filesystem::last_write_time(cachePath, ec);
    if (ec) return false;
    auto sourceTime = std::filesystem::last_write_time(path, ec);
    if (ec) return false;

    // si la fuente es mas nueva que la cache, la tiro
    if (sourceTime > cacheTime) return false;

    std::ifstream file(cachePath, std::ios::binary);
    if (!file) return false;

    // cabecera: version, ancho, alto, nº de frames
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != DISK_CACHE_VERSION) return false;

    file.read(reinterpret_cast<char*>(&outEntry.width), sizeof(outEntry.width));
    file.read(reinterpret_cast<char*>(&outEntry.height), sizeof(outEntry.height));

    uint32_t frameCount;
    file.read(reinterpret_cast<char*>(&frameCount), sizeof(frameCount));

    outEntry.frames.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        auto& frame = outEntry.frames[i];
        file.read(reinterpret_cast<char*>(&frame.delay), sizeof(frame.delay));
        file.read(reinterpret_cast<char*>(&frame.width), sizeof(frame.width));
        file.read(reinterpret_cast<char*>(&frame.height), sizeof(frame.height));

        uint32_t dataSize;
        file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));

        frame.pixels.resize(dataSize);
        file.read(reinterpret_cast<char*>(frame.pixels.data()), dataSize);
    }

    return file.good();
}

void AnimatedGIFSprite::saveToDiskCache(std::string const& path, DiskCacheEntry const& entry) {
    auto cachePath = getCachePath(path);
    std::ofstream file(cachePath, std::ios::binary);
    if (!file) return;

    uint32_t version = DISK_CACHE_VERSION;
    file.write(reinterpret_cast<char const*>(&version), sizeof(version));
    file.write(reinterpret_cast<char const*>(&entry.width), sizeof(entry.width));
    file.write(reinterpret_cast<char const*>(&entry.height), sizeof(entry.height));

    uint32_t frameCount = entry.frames.size();
    file.write(reinterpret_cast<char const*>(&frameCount), sizeof(frameCount));

    for (auto const& frame : entry.frames) {
        file.write(reinterpret_cast<char const*>(&frame.delay), sizeof(frame.delay));
        file.write(reinterpret_cast<char const*>(&frame.width), sizeof(frame.width));
        file.write(reinterpret_cast<char const*>(&frame.height), sizeof(frame.height));

        uint32_t dataSize = frame.pixels.size();
        file.write(reinterpret_cast<char const*>(&dataSize), sizeof(dataSize));
        file.write(reinterpret_cast<char const*>(frame.pixels.data()), dataSize);
    }
    file.flush();
    pruneDiskCache();
}

void AnimatedGIFSprite::workerLoop() {
    while (true) {
        GIFTask task;
        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            s_queueCV.wait(lock, []{ return !s_taskQueue.empty() || !s_workerRunning.load(std::memory_order_acquire); });
            
            if (!s_workerRunning.load(std::memory_order_acquire) && s_taskQueue.empty()) break;
            
            task = s_taskQueue.front();
            s_taskQueue.pop_front();
        }
        
        // proceso la tarea que toque
        if (task.isData) {
            // decodificar desde memoria
            if (!imgp::formats::isGif(task.data.data(), task.data.size())) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }
            
            auto decodeResult = imgp::decode::gif(task.data.data(), task.data.size());
            if (!decodeResult.isOk()) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }

            // mover el resultado al lambda del main thread
            struct DecodedGIF {
                std::vector<std::vector<uint8_t>> framePixels;
                std::vector<uint32_t> frameDelays;
                uint16_t width = 0;
                uint16_t height = 0;
            };

            auto sharedGif = std::make_shared<DecodedGIF>();
            auto& decoded = decodeResult.unwrap();
            auto* animPtr = std::get_if<imgp::DecodedAnimation>(&decoded);
            if (!animPtr || animPtr->frames.empty()) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }
            sharedGif->width = animPtr->width;
            sharedGif->height = animPtr->height;
            for (auto& frame : animPtr->frames) {
                size_t pixelSize = static_cast<size_t>(animPtr->width) * animPtr->height * 4;
                std::vector<uint8_t> pixels(frame.data.get(), frame.data.get() + pixelSize);
                sharedGif->framePixels.push_back(std::move(pixels));
                sharedGif->frameDelays.push_back(frame.delay);
            }
            
            Loader::get()->queueInMainThread([key = task.key, sharedGif, cb = task.callback]() mutable {
                if (s_shutdownMode.load(std::memory_order_acquire)) {
                    if (cb) cb(nullptr);
                    return;
                }
                if (sharedGif->framePixels.empty()) {
                    if (cb) cb(nullptr);
                    return;
                }
                
                auto ret = new AnimatedGIFSprite();
                if (ret) {
                    ret->m_filename = key;
                    ret->m_canvasWidth = sharedGif->width;
                    ret->m_canvasHeight = sharedGif->height;

                    if (!ret->init()) {
                        CC_SAFE_DELETE(ret);
                        if (cb) cb(nullptr);
                        return;
                    }

                    float sf = getContentScaleFactorSafe();
                    ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));

                    // proceso todos los frames ya pa dejar el GIF entero en cache
                    for (size_t i = 0; i < sharedGif->framePixels.size(); ++i) {
                        auto* gifFrame = new GIFFrame();
                        auto* texture = new CCTexture2D();

                        bool success = texture->initWithData(
                            sharedGif->framePixels[i].data(),
                            kCCTexture2DPixelFormat_RGBA8888,
                            sharedGif->width,
                            sharedGif->height,
                            CCSize(sharedGif->width / sf, sharedGif->height / sf)
                        );

                        if (success) {
                            texture->setAntiAliasTexParameters();
                            gifFrame->texture = texture;
                            gifFrame->delay = sharedGif->frameDelays[i] / 1000.0f;
                            gifFrame->rect = CCRect(0, 0, sharedGif->width, sharedGif->height);
                            ret->m_frames.push_back(gifFrame);
                            ret->m_frameColors.push_back({ {0,0,0}, {255,255,255} });
                            texture->retain();
                        } else {
                            delete gifFrame;
                            texture->release();
                        }
                    }

                    if (ret->m_frames.empty()) {
                        CC_SAFE_DELETE(ret);
                        if (cb) cb(nullptr);
                        return;
                    }

                    // guardo en cache con todos los frames ya listos
                    SharedGIFData cacheEntry;
                    cacheEntry.width = ret->m_canvasWidth;
                    cacheEntry.height = ret->m_canvasHeight;

                    for (auto* frame : ret->m_frames) {
                        cacheEntry.textures.push_back(frame->texture);
                        cacheEntry.delays.push_back(frame->delay);
                        cacheEntry.frameRects.push_back(frame->rect);
                    }

                    {
                        std::lock_guard<std::mutex> lock(s_cacheMutex);
                        s_gifCache[key] = cacheEntry;
                        s_currentCacheSize += getSharedGIFDataSize(cacheEntry);
                        if (!isPinned(key)) {
                            s_lruList.push_back(key);
                            s_lruMap[key] = std::prev(s_lruList.end());
                        }
                        evictIfNeeded();
                    }

                    log::info("[AnimatedGIFSprite] Cached complete GIF from data with key: {} ({} frames)", key, ret->m_frames.size());

                    // pongo el primer frame y arranco la animacion
                    ret->setCurrentFrame(0);
                    ret->scheduleUpdate();
                    ret->autorelease();

                    if (cb) cb(ret);
                } else {
                    if (cb) cb(nullptr);
                }
            });
            
        } else {
            // decodificar desde archivo
            // primero intento tirar del cache en disco
            DiskCacheEntry cachedEntry;
            if (loadFromDiskCache(task.path, cachedEntry)) {
                Loader::get()->queueInMainThread([path = task.path, cachedEntry = std::move(cachedEntry), cb = task.callback]() mutable {
                    if (s_shutdownMode.load(std::memory_order_acquire)) {
                        if (cb) cb(nullptr);
                        return;
                    }
                    auto ret = new AnimatedGIFSprite();
                    if (ret) {
                        ret->m_filename = path;
                        ret->m_canvasWidth = cachedEntry.width;
                        ret->m_canvasHeight = cachedEntry.height;
                        
                        if (!ret->init()) {
                            CC_SAFE_DELETE(ret);
                            if (cb) cb(nullptr);
                            return;
                        }
                        
                        float sf = getContentScaleFactorSafe();
                        ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));
                        
                        // cargo todos los frames desde el cache (ya vienen procesados)
                        for (auto const& frame : cachedEntry.frames) {
                            auto texture = new CCTexture2D();
                            if (!texture->initWithData(
                                frame.pixels.data(),
                                kCCTexture2DPixelFormat_RGBA8888,
                                frame.width,
                                frame.height,
                                CCSize(frame.width / sf, frame.height / sf)
                            )) {
                                texture->release();
                                continue;
                            }
                            texture->setAntiAliasTexParameters();
                            
                            auto* gifFrame = new GIFFrame();
                            gifFrame->texture = texture;
                            gifFrame->delay = frame.delay;
                            gifFrame->rect = CCRect(0, 0, frame.width, frame.height);
                            
                            ret->m_frames.push_back(gifFrame);
                            ret->m_frameColors.push_back({ {0,0,0}, {255,255,255} });
                            texture->retain();
                        }
                        
                        if (!ret->m_frames.empty()) {
                            ret->setCurrentFrame(0);
                            ret->scheduleUpdate();
                        }
                        
                        ret->autorelease();
                        if (cb) cb(ret);
                    } else {
                        if (cb) cb(nullptr);
                    }
                });
                continue;
            }

            std::ifstream file(task.path, std::ios::binary);
            if (!file) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }

            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            if (!imgp::formats::isGif(data.data(), data.size())) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }

            auto decodeResult = imgp::decode::gif(data.data(), data.size());
            if (!decodeResult.isOk()) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }

            auto& decoded = decodeResult.unwrap();
            auto* animPtr = std::get_if<imgp::DecodedAnimation>(&decoded);
            if (!animPtr || animPtr->frames.empty()) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }
            
            // guardo en cache de disco (RGBA8888)
            DiskCacheEntry newCacheEntry;
            newCacheEntry.width = animPtr->width;
            newCacheEntry.height = animPtr->height;

            // construyo pending frames para carga incremental y cache disco
            std::deque<PendingFrame> pendingFrames;
            
            for (auto& frame : animPtr->frames) {
                size_t pixelSize = static_cast<size_t>(animPtr->width) * animPtr->height * 4;

                DiskCacheEntry::Frame cacheFrame;
                cacheFrame.delay = frame.delay / 1000.0f;
                cacheFrame.width = animPtr->width;
                cacheFrame.height = animPtr->height;
                cacheFrame.pixels.assign(frame.data.get(), frame.data.get() + pixelSize);
                newCacheEntry.frames.push_back(std::move(cacheFrame));

                PendingFrame pf;
                pf.pixels.assign(frame.data.get(), frame.data.get() + pixelSize);
                pf.width = animPtr->width;
                pf.height = animPtr->height;
                pf.delayMs = frame.delay;
                pendingFrames.push_back(std::move(pf));
            }
            saveToDiskCache(task.path, newCacheEntry);
            
            Loader::get()->queueInMainThread([path = task.path, pendingFrames = std::move(pendingFrames),
                                              canvasW = static_cast<int>(animPtr->width),
                                              canvasH = static_cast<int>(animPtr->height),
                                              cb = task.callback]() mutable {
                if (s_shutdownMode.load(std::memory_order_acquire)) {
                    if (cb) cb(nullptr);
                    return;
                }
                if (pendingFrames.empty()) {
                    if (cb) cb(nullptr);
                    return;
                }
                
                auto ret = new AnimatedGIFSprite();
                if (ret) {
                    ret->m_filename = path;
                    ret->m_canvasWidth = canvasW;
                    ret->m_canvasHeight = canvasH;
                    ret->m_pendingFrames = std::move(pendingFrames);

                    if (!ret->init()) {
                        CC_SAFE_DELETE(ret);
                        if (cb) cb(nullptr);
                        return;
                    }

                    float sf = getContentScaleFactorSafe();
                    ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));

                    // me aseguro de tener al menos un frame antes del callback
                    ret->processNextPendingFrame();

                    // el resto de frames se cargan poco a poco en el update
                    if (!ret->m_pendingFrames.empty()) {
                        ret->schedule(schedule_selector(AnimatedGIFSprite::updateTextureLoading));
                    } else {
                        ret->scheduleUpdate();
                    }
                    ret->autorelease();

                    if (cb) cb(ret);
                } else {
                    if (cb) cb(nullptr);
                }
            });
        }
    }
}

void AnimatedGIFSprite::clearCache() {
    s_shutdownMode.store(true, std::memory_order_release);
    shutdownWorker();
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    for (auto& [key, data] : s_gifCache) {
        for (auto* tex : data.textures) {
            if (tex) tex->release();
        }
    }
    s_gifCache.clear();
    s_lruList.clear();
    s_lruMap.clear();
    s_pinnedGIFs.clear();
    s_currentCacheSize = 0;
    PaimonDebug::log("[AnimatedGIFSprite] Cache cleared");
}

void AnimatedGIFSprite::remove(std::string const& filename) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    auto it = s_gifCache.find(filename);
    if (it != s_gifCache.end()) {
        size_t removeSize = 0;
        for (auto* tex : it->second.textures) {
            if (tex) {
                removeSize += tex->getPixelsWide() * tex->getPixelsHigh() * 4;
                tex->release();
            }
        }
        if (s_currentCacheSize >= removeSize) s_currentCacheSize -= removeSize;
        else s_currentCacheSize = 0;
        s_gifCache.erase(it);
        auto lruIt = s_lruMap.find(filename);
        if (lruIt != s_lruMap.end()) {
            s_lruList.erase(lruIt->second);
            s_lruMap.erase(lruIt);
        }
        PaimonDebug::log("[AnimatedGIFSprite] Removed from cache: {}", filename);
    }
}

bool AnimatedGIFSprite::isCached(std::string const& filename) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    return s_gifCache.find(filename) != s_gifCache.end();
}

AnimatedGIFSprite::~AnimatedGIFSprite() {
    PaimonDebug::log("[AnimatedGIFSprite] Destroying sprite for file: {}", m_filename);
    this->unscheduleUpdate();
    this->setTexture(nullptr);
    
    for (auto* frame : m_frames) {
        if (frame) {
            delete frame;
        }
    }
    m_frames.clear();
}

bool AnimatedGIFSprite::initFromCache(std::string const& cacheKey) {
    m_filename = cacheKey;

    SharedGIFData cachedData;
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_gifCache.find(cacheKey);
        if (it == s_gifCache.end()) {
            return false;
        }
        cachedData = it->second;

        // actualizar LRU O(1)
        auto lruIt = s_lruMap.find(cacheKey);
        if (lruIt != s_lruMap.end()) {
            s_lruList.erase(lruIt->second);
        }
        s_lruList.push_back(cacheKey);
        s_lruMap[cacheKey] = std::prev(s_lruList.end());
    }
    m_canvasWidth = cachedData.width;
    m_canvasHeight = cachedData.height;
    
    PaimonDebug::log("[AnimatedGIFSprite] Cache hit for: {}, size: {}x{}, frames: {}", 
        cacheKey, m_canvasWidth, m_canvasHeight, cachedData.textures.size());

    for (size_t i = 0; i < cachedData.textures.size(); ++i) {
        auto* gifFrame = new GIFFrame();
        gifFrame->texture = cachedData.textures[i];
        gifFrame->texture->retain(); // retain pa esta instancia del sprite
        gifFrame->delay = (i < cachedData.delays.size()) ? cachedData.delays[i] : 0.1f;
        gifFrame->rect = (i < cachedData.frameRects.size()) ? cachedData.frameRects[i] : CCRect(0, 0, m_canvasWidth, m_canvasHeight);
        m_frames.push_back(gifFrame);
        
        // colores por defecto pa evitar indice fuera de rango
        m_frameColors.push_back({ {0,0,0}, {255,255,255} });
    }
    
    if (m_frames.empty()) {
        log::error("[AnimatedGIFSprite] Cached frames empty for: {}", cacheKey);
        return false;
    }
    
    // init con primer frame
    if (!CCSprite::init()) {
        log::error("[AnimatedGIFSprite] CCSprite::init failed");
        return false;
    }

    float sf = getContentScaleFactorSafe();
    this->setContentSize(CCSize(m_canvasWidth / sf, m_canvasHeight / sf));
    this->setCurrentFrame(0);
    this->scheduleUpdate();
    
    return true;
}

AnimatedGIFSprite* AnimatedGIFSprite::createFromCache(std::string const& key) {
    auto ret = new (std::nothrow) AnimatedGIFSprite();
    if (ret && ret->initFromCache(key)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void AnimatedGIFSprite::createAsync(std::vector<uint8_t> const& data, std::string const& key, AsyncCallback callback) {
    log::debug("[AnimatedGIFSprite] createAsync(data): key={} size={}", key, data.size());
    if (data.empty()) {
        if (callback) callback(nullptr);
        return;
    }

    // comprobar cache primero
    if (isCached(key)) {
        auto ret = createFromCache(key);
        if (callback) callback(ret);
        return;
    }

    initWorker();

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        GIFTask task;
        task.data = data;
        task.key = key;
        task.callback = callback;
        task.isData = true;
        s_taskQueue.push_back(task);
    }
    s_queueCV.notify_one();
}



void AnimatedGIFSprite::createAsync(std::string const& path, AsyncCallback callback) {
    log::debug("[AnimatedGIFSprite] createAsync(path): {}", path);
    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc) || existsEc) {
        if (callback) callback(nullptr);
        return;
    }

    // comprobar cache primero (Main Thread)
    if (isCached(path)) {
        auto ret = createFromCache(path);
        if (callback) callback(ret);
        return;
    }

    initWorker();

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        GIFTask task;
        task.path = path;
        task.callback = callback;
        task.isData = false;
        s_taskQueue.push_back(task);
    }
    s_queueCV.notify_one();
}

void AnimatedGIFSprite::update(float dt) {
    CCSprite::update(dt);
    updateAnimation(dt);
}

void AnimatedGIFSprite::updateAnimation(float dt) {
    if (!m_isPlaying || m_frames.empty()) {
        return;
    }
    
    m_frameTimer += dt;
    
    // Advance through frames using subtraction so accumulated time
    // beyond each delay carries over. This prevents the systematic
    // slowdown caused by resetting m_frameTimer to 0 every transition.
    // Also handles multi-frame skips on lag spikes (large dt).
    bool frameChanged = false;
    int maxIterations = static_cast<int>(m_frames.size()) + 1;
    
    while (maxIterations-- > 0) {
        float currentDelay = 0.1f;
        if (m_currentFrame < m_frames.size() && m_frames[m_currentFrame]) {
            currentDelay = m_frames[m_currentFrame]->delay;
        }
        if (currentDelay <= 0.0f) {
            currentDelay = 0.1f; // Delay por defecto de 100ms
        }
        
        if (m_frameTimer < currentDelay) {
            break;
        }
        
        m_frameTimer -= currentDelay;
        
        // Avanzar al siguiente frame
        m_currentFrame++;
        
        if (m_currentFrame >= m_frames.size()) {
            if (m_loop) {
                m_currentFrame = 0;
            } else {
                m_currentFrame = m_frames.size() - 1;
                m_isPlaying = false;
                m_frameTimer = 0.0f;
                frameChanged = true;
                break;
            }
        }
        frameChanged = true;
    }
    
    if (frameChanged) {
        setCurrentFrame(m_currentFrame);
    }
}

void AnimatedGIFSprite::setCurrentFrame(unsigned int frame) {
    if (frame >= m_frames.size()) {
        log::warn("[AnimatedGIFSprite] Invalid frame index: {}", frame);
        return;
    }
    
    m_currentFrame = frame;
    m_frameTimer = 0.0f;
    
    if (m_frames[m_currentFrame] && m_frames[m_currentFrame]->texture) {
        auto* gifFrame = m_frames[m_currentFrame];

        float sf = getContentScaleFactorSafe();
        
        // offset pa CCSpriteFrame (gif top-left, cocos bottom-left)
        float left = gifFrame->rect.origin.x;
        float top = gifFrame->rect.origin.y;
        float w = gifFrame->rect.size.width;
        float h = gifFrame->rect.size.height;
        
        float centerX = left + w / 2.0f;
        float centerY = (m_canvasHeight - top) - h / 2.0f;
        
        float canvasCenterX = m_canvasWidth / 2.0f;
        float canvasCenterY = m_canvasHeight / 2.0f;
        
        CCPoint offset((centerX - canvasCenterX) / sf, (centerY - canvasCenterY) / sf);
        
        // comprobar si la textura es placeholder (mas chica que el frame)
        auto texPx = gifFrame->texture->getContentSizeInPixels();
        bool isPlaceholder = (texPx.width < w || texPx.height < h);
        
        CCRect rectToUse = CCRect(0, 0, w, h);
        CCPoint offsetToUse = offset;
        
        if (isPlaceholder) {
            rectToUse = CCRect(0, 0, texPx.width, texPx.height);
            offsetToUse = CCPoint(0, 0);
        }

        auto spriteFrame = CCSpriteFrame::createWithTexture(
            gifFrame->texture, 
            rectToUse, 
            false, 
            offsetToUse, 
            CCSize(m_canvasWidth / sf, m_canvasHeight / sf)
        );
        
        this->setDisplayFrame(spriteFrame);
    }
}

void AnimatedGIFSprite::draw() {
    if (getShaderProgram()) {
        getShaderProgram()->use();
        getShaderProgram()->setUniformsForBuiltins();

        GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
        if (intensityLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
        }

        GLint timeLoc = getShaderProgram()->getUniformLocationForName("u_time");
        if (timeLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(timeLoc, m_time);
        }

        GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
        if (brightLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
        }

        GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
        if (sizeLoc != -1) {
            if (getTexture()) {
                m_texSize = getTexture()->getContentSizeInPixels();
            }
            float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
            float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
        }

        GLint screenSizeLoc = getShaderProgram()->getUniformLocationForName("u_screenSize");
        if (screenSizeLoc != -1 && m_screenSize.width > 0.0f && m_screenSize.height > 0.0f) {
            getShaderProgram()->setUniformLocationWith2f(screenSizeLoc, m_screenSize.width, m_screenSize.height);
        }
    }

    CCSprite::draw();
}
