#include "EmoteCache.hpp"
#include "EmoteService.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <Geode/Geode.hpp>
#include <prevter.imageplus/include/events.hpp>
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

// ─── Helpers ───

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
        callback(texture, isGif, gifData);
    });
}

void dispatchPreloadCallback(EmoteCache::PreloadCallback callback,
                             size_t downloaded,
                             size_t skipped,
                             size_t total) {
    if (!callback) return;
    Loader::get()->queueInMainThread([callback = std::move(callback), downloaded, skipped, total]() mutable {
        callback(downloaded, skipped, total);
    });
}
} // namespace

static CCTexture2D* bytesToStaticTexture(std::vector<uint8_t> const& data) {
    if (data.empty()) return nullptr;

    // ImagePlus: decodes PNG, WebP, JPEG, QOI, JPEG XL automatically
    if (imgp::isAvailable()) {
        auto result = imgp::tryDecode(data.data(), data.size());
        if (result.isOk()) {
            auto& decoded = result.unwrap();
            if (auto* img = std::get_if<imgp::DecodedImage>(&decoded)) {
                if (*img && img->width > 0 && img->height > 0) {
                    auto* tex = new CCTexture2D();
                    if (tex->initWithData(
                            img->data.get(),
                            kCCTexture2DPixelFormat_RGBA8888,
                            img->width, img->height,
                            CCSizeMake(static_cast<float>(img->width), static_cast<float>(img->height)))) {
                        tex->autorelease();
                        return tex;
                    }
                    tex->release();
                }
            }
        }
    }

    // Fallback: CCImage for JPEG and others
    auto* ccImg = new CCImage();
    if (!ccImg->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
        ccImg->release();
        return nullptr;
    }
    auto* tex = new CCTexture2D();
    if (!tex->initWithImage(ccImg)) {
        tex->release();
        ccImg->release();
        return nullptr;
    }
    ccImg->release();
    tex->autorelease();
    return tex;
}

// ─── Disk cache paths ───

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

// ─── RAM cache ───

void EmoteCache::addToRam(std::string const& name, RamEntry entry) {
    std::lock_guard lock(m_ramMutex);

    // Remove old entry if exists
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

// ─── Main load method ───

void EmoteCache::loadEmote(EmoteInfo const& info, TextureCallback callback) {
    // 1) Check RAM cache
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

    // 2) Check disk cache
    if (isDiskEntryValid(info.filename)) {
        std::vector<uint8_t> diskData;
        if (loadFromDisk(info.filename, diskData)) {
            if (info.type == EmoteType::Gif) {
                // Store raw GIF bytes for AnimatedGIFSprite
                RamEntry entry;
                entry.type = EmoteType::Gif;
                entry.gifData = diskData;
                entry.byteSize = diskData.size();
                entry.cachedAt = std::chrono::steady_clock::now();
                addToRam(info.name, std::move(entry));
                dispatchTextureCallback(std::move(callback), nullptr, true, std::move(diskData));
                return;
            } else {
                auto* tex = bytesToStaticTexture(diskData);
                if (tex) {
                    RamEntry entry;
                    entry.type = EmoteType::Static;
                    entry.texture = tex;
                    entry.byteSize = diskData.size();
                    entry.cachedAt = std::chrono::steady_clock::now();
                    addToRam(info.name, std::move(entry));
                    dispatchTextureCallback(std::move(callback), geode::Ref<CCTexture2D>(tex), false, {});
                    return;
                }

                log::warn("[EmoteCache] Static disk cache decode failed for emote '{}', purging corrupt file", info.name);
                std::error_code ec;
                std::filesystem::remove(getDiskPath(info.filename), ec);
            }
        }
    }

    // 3) Download from URL
    auto emoteName = info.name;
    auto emoteFilename = info.filename;
    auto emoteType = info.type;
    auto emoteUrl = info.url;

    HttpClient::get().downloadFromUrlRaw(info.url, [this, emoteName, emoteFilename, emoteType, emoteUrl, callback = std::move(callback)](
        bool success, std::vector<uint8_t> const& data, int, int) mutable {

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
        } else {
            auto* tex = bytesToStaticTexture(data);
            if (!tex) {
                log::warn("[EmoteCache] Failed to decode static emote '{}'", emoteName);
                dispatchTextureCallback(std::move(callback), nullptr, false, {});
                return;
            }

            saveToDisk(emoteFilename, data);

            RamEntry entry;
            entry.type = EmoteType::Static;
            entry.texture = tex;
            entry.byteSize = data.size();
            entry.cachedAt = std::chrono::steady_clock::now();
            addToRam(emoteName, std::move(entry));
            dispatchTextureCallback(std::move(callback), geode::Ref<CCTexture2D>(tex), false, {});
        }
    });
}

// ─── Clear all ───

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

// ─── Background preload ───

void EmoteCache::cancelPreload() {
    m_preloadCancel.store(true, std::memory_order_release);
}

void EmoteCache::preloadAllToDisk(PreloadCallback callback) {
    if (m_preloading.exchange(true, std::memory_order_acq_rel)) {
        dispatchPreloadCallback(std::move(callback), 0, 0, 0);
        return; // already running
    }
    m_preloadCancel.store(false, std::memory_order_release);

    // Copy the emote list so we don't hold locks during downloads
    auto allEmotes = EmoteService::get().getAllEmotes();
    if (allEmotes.empty()) {
        m_preloading.store(false, std::memory_order_release);
        dispatchPreloadCallback(std::move(callback), 0, 0, 0);
        return;
    }

    // Shared index to track progress through the emote list
    auto idx = std::make_shared<size_t>(0);
    auto emotes = std::make_shared<std::vector<EmoteInfo>>(std::move(allEmotes));
    auto skipped = std::make_shared<size_t>(0);
    auto downloaded = std::make_shared<size_t>(0);
    auto cb = std::make_shared<PreloadCallback>(std::move(callback));

    // Recursive lambda: downloads one emote at a time, then schedules the next
    auto downloadNext = std::make_shared<std::function<void()>>();
    *downloadNext = [this, idx, emotes, skipped, downloaded, downloadNext, cb]() {
        if (m_preloadCancel.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            log::info("[EmoteCache] Preload cancelled ({}/{} done, {} skipped)",
                *downloaded, emotes->size(), *skipped);
            m_preloading.store(false, std::memory_order_release);
            if (*cb) {
                size_t d = *downloaded, s = *skipped, t = emotes->size();
                dispatchPreloadCallback(*cb, d, s, t);
            }
            return;
        }

        // Find next emote that's not already on disk
        while (*idx < emotes->size()) {
            auto const& info = (*emotes)[*idx];
            if (isDiskEntryValid(info.filename)) {
                ++(*skipped);
                ++(*idx);
                continue;
            }
            break;
        }

        if (*idx >= emotes->size()) {
            log::info("[EmoteCache] Preload complete: {} downloaded, {} already cached",
                *downloaded, *skipped);
            m_preloading.store(false, std::memory_order_release);
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

        HttpClient::get().downloadFromUrlRaw(url, [this, filename, downloaded, downloadNext](
            bool success, std::vector<uint8_t> const& data, int, int) {

            if (success && !data.empty()) {
                saveToDisk(filename, data);
                ++(*downloaded);
            }

            // Schedule next download with a small delay to avoid hammering the server
            Loader::get()->queueInMainThread([downloadNext]() {
                if (*downloadNext) (*downloadNext)();
            });
        });
    };

    // Start the chain
    log::info("[EmoteCache] Starting background preload of {} emotes", emotes->size());
    (*downloadNext)();
}
