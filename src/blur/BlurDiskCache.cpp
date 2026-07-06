#include "BlurDiskCache.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/cocos/platform/CCImage.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <shared_mutex>
#include <system_error>

#include "../utils/ThreadPool.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::blur {

// Dedicated 1-thread I/O pool for the blur cache; separate from ThumbnailLoader's disk pool.
static std::unique_ptr<paimon::ThreadPool>& getBlurIOPool() {
    static std::unique_ptr<paimon::ThreadPool> pool;
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        pool = std::make_unique<paimon::ThreadPool>(1, "PaimonBlurIO");
    });
    return pool;
}

BlurDiskCache& BlurDiskCache::get() {
    // Heap leak is intentional: avoids destructor running during atexit while I/O
    // workers may still touch the index. shutdown() flips the atomic flag.
    static BlurDiskCache* instance = new BlurDiskCache();
    return *instance;
}

std::filesystem::path BlurDiskCache::cacheDir() const {
    return Mod::get()->getSaveDir() / "blur_cache";
}

std::filesystem::path BlurDiskCache::pathForKey(std::string const& key) const {
    return cacheDir() / (key + ".pblur");
}

void BlurDiskCache::init() {
    bool expected = false;
    if (!m_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    getBlurIOPool()->enqueue([this]() {
        if (m_shuttingDown.load(std::memory_order_acquire)) return;
        auto dir = cacheDir();
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            std::filesystem::create_directories(dir, ec);
            if (ec) {
                log::warn("[BlurDiskCache] could not create cache dir: {}", ec.message());
                return;
            }
        }

        std::unordered_map<std::string, IndexEntry> loaded;
        std::int64_t totalBytes = 0;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (m_shuttingDown.load(std::memory_order_acquire)) break;
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (path.extension() != ".pblur") continue;

            IndexEntry ie;
            ie.byteSize = static_cast<std::int64_t>(entry.file_size(ec));
            if (ec) { ec.clear(); continue; }
            totalBytes += ie.byteSize;

            auto ftime = std::filesystem::last_write_time(path, ec);
            if (!ec) {
                ie.mtimeEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count();
            }
            ec.clear();

            std::ifstream f(path, std::ios::binary);
            if (!f) continue;
            std::uint32_t header[5] = {0};
            f.read(reinterpret_cast<char*>(header), sizeof(header));
            if (!f || header[0] != MAGIC || header[1] != VERSION) continue;
            ie.width = static_cast<int>(header[2]);
            ie.height = static_cast<int>(header[3]);

            if (ie.width <= 0 || ie.height <= 0 || ie.width > 8192 || ie.height > 8192) {
                log::debug("[BlurDiskCache] entry {} has invalid dimensions {}x{}",
                    geode::utils::string::pathToString(path.stem()), ie.width, ie.height);
                continue;
            }

            std::int64_t expectedSize = HEADER_SIZE + static_cast<std::int64_t>(ie.width) * ie.height * 4;
            if (expectedSize != ie.byteSize) {
                log::debug("[BlurDiskCache] corrupted entry {}: expected {} bytes, got {}",
                    geode::utils::string::pathToString(path.stem()), expectedSize, ie.byteSize);
                continue;
            }

            loaded.emplace(geode::utils::string::pathToString(path.stem()), ie);
        }

        std::size_t entryCount = 0;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_index = std::move(loaded);
            evictIndexIfNeededLocked();
            entryCount = m_index.size();
        }
        log::info("[BlurDiskCache] initialized: {} entries, ~{} MB",
            entryCount, totalBytes / (1024 * 1024));
    });
}

void BlurDiskCache::evictIndexIfNeededLocked() {
    std::int64_t totalBytes = 0;
    for (auto const& [_, ie] : m_index) {
        totalBytes += ie.byteSize;
    }
    if (totalBytes <= MAX_DISK_SIZE_BYTES) return;

    std::vector<std::pair<std::string, IndexEntry>> sorted(m_index.begin(), m_index.end());
    std::sort(sorted.begin(), sorted.end(),
        [](auto const& a, auto const& b) { return a.second.mtimeEpoch < b.second.mtimeEpoch; });
    for (auto const& [key, ie] : sorted) {
        if (totalBytes <= MAX_DISK_SIZE_BYTES) break;
        std::error_code rmEc;
        std::filesystem::remove(pathForKey(key), rmEc);
        totalBytes -= ie.byteSize;
        m_index.erase(key);
    }
}

bool BlurDiskCache::hasEntry(std::string const& key) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_index.find(key) != m_index.end();
}

std::size_t BlurDiskCache::diskEntryCount() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_index.size();
}

CCTexture2D* BlurDiskCache::uploadRawRGBA(std::vector<uint8_t> const& pixels, int w, int h) {
    if (pixels.empty() || w <= 0 || h <= 0) return nullptr;
    std::size_t expected = static_cast<std::size_t>(w) * h * 4;
    if (pixels.size() != expected) return nullptr;

    auto* tex = new CCTexture2D();
    bool ok = tex->initWithData(
        pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
        static_cast<unsigned int>(w), static_cast<unsigned int>(h),
        CCSize(static_cast<float>(w), static_cast<float>(h)));
    if (!ok) {
        tex->release();
        return nullptr;
    }
    tex->autorelease();

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    tex->setTexParameters(&params);
    return tex;
}

void BlurDiskCache::lookupAsync(std::string const& key, ReadyCallback onReady) {
    if (!onReady) return;
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        onReady(nullptr);
        return;
    }

    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_index.find(key) == m_index.end()) {
            onReady(nullptr);
            return;
        }
    }

    getBlurIOPool()->enqueue([this, key, onReady = std::move(onReady)]() {
        if (m_shuttingDown.load(std::memory_order_acquire)) {
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
            return;
        }

        auto path = pathForKey(key);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            // Index is stale — clear it.
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_index.erase(key);
            }
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
            return;
        }

        std::ifstream f(path, std::ios::binary);
        if (!f) {
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
            return;
        }

        std::uint32_t header[5] = {0};
        f.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!f || header[0] != MAGIC || header[1] != VERSION) {
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
            return;
        }

        int w = static_cast<int>(header[2]);
        int h = static_cast<int>(header[3]);
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
            return;
        }

        std::size_t pixelBytes = static_cast<std::size_t>(w) * h * 4;
        std::vector<uint8_t> pixels(pixelBytes);
        f.read(reinterpret_cast<char*>(pixels.data()), pixelBytes);
        if (!f) {
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
            return;
        }

        auto pixelsPtr = std::make_shared<std::vector<uint8_t>>(std::move(pixels));
        Loader::get()->queueInMainThread([this, pixelsPtr, w, h, onReady]() {
            if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
                return;
            }
            auto* tex = uploadRawRGBA(*pixelsPtr, w, h);
            onReady(tex);
        });
    });
}

void BlurDiskCache::storeFromTextureAsync(std::string const& key, CCTexture2D* tex, int width, int height) {
    if (!tex || m_shuttingDown.load(std::memory_order_acquire)) return;
    if (width <= 0 || height <= 0) return;

    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_index.find(key) != m_index.end()) return;
    }

    // glReadPixels needs the GL main thread; render to a temp RT and read back via newCCImage.
    int w = width;
    int h = height;
    auto* rt = CCRenderTexture::create(w, h);
    if (!rt) return;

    auto* sprite = CCSprite::createWithTexture(tex);
    if (!sprite) return;
    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setPosition(CCPoint(w * 0.5f, h * 0.5f));
    sprite->setFlipY(true);

    rt->beginWithClear(0, 0, 0, 0);
    sprite->visit();
    rt->end();

    CCImage* img = rt->newCCImage(false);
    if (!img) return;

    int ow = img->getWidth();
    int oh = img->getHeight();
    unsigned char* data = img->getData();
    if (!data || ow <= 0 || oh <= 0) {
        img->release();
        return;
    }

    std::size_t pixelBytes = static_cast<std::size_t>(ow) * oh * 4;
    auto pixels = std::make_shared<std::vector<uint8_t>>(data, data + pixelBytes);
    img->release();

    persistPixelsAsync(key, std::move(pixels), ow, oh);
}

void BlurDiskCache::storeAsync(std::string const& key, CCRenderTexture* rt) {
    if (!rt || m_shuttingDown.load(std::memory_order_acquire)) return;

    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_index.find(key) != m_index.end()) return;
    }

    CCImage* img = rt->newCCImage(false);
    if (!img) return;

    int w = img->getWidth();
    int h = img->getHeight();
    unsigned char* data = img->getData();
    if (!data || w <= 0 || h <= 0) {
        img->release();
        return;
    }

    std::size_t pixelBytes = static_cast<std::size_t>(w) * h * 4;
    auto pixels = std::make_shared<std::vector<uint8_t>>(data, data + pixelBytes);
    img->release();

    persistPixelsAsync(key, std::move(pixels), w, h);
}

void BlurDiskCache::persistPixelsAsync(std::string key, std::shared_ptr<std::vector<uint8_t>> pixels, int w, int h) {
    getBlurIOPool()->enqueue([this, key = std::move(key), pixels = std::move(pixels), w, h]() {
        if (m_shuttingDown.load(std::memory_order_acquire)) return;

        auto path = pathForKey(key);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) {
            log::debug("[BlurDiskCache] could not write {}", geode::utils::string::pathToString(path));
            return;
        }

        std::uint32_t header[5] = {MAGIC, VERSION,
            static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), 0};
        f.write(reinterpret_cast<char const*>(header), sizeof(header));
        f.write(reinterpret_cast<char const*>(pixels->data()), pixels->size());
        if (!f) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return;
        }
        f.close();

        IndexEntry ie;
        ie.width = w;
        ie.height = h;
        ie.byteSize = static_cast<std::int64_t>(HEADER_SIZE + pixels->size());
        // Index with the file's own mtime: steady_clock and file_clock have
        // different epochs, which broke LRU eviction ordering across sessions.
        std::error_code mtEc;
        auto ftime = std::filesystem::last_write_time(path, mtEc);
        ie.mtimeEpoch = mtEc ? 0 : std::chrono::duration_cast<std::chrono::seconds>(
            ftime.time_since_epoch()).count();

        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_index[key] = ie;
            evictIndexIfNeededLocked();
        }
    });
}

void BlurDiskCache::invalidate(std::string const& key) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_index.erase(key);
    }
    auto path = pathForKey(key);
    getBlurIOPool()->enqueue([path]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    });
}

void BlurDiskCache::clear() {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_index.clear();
    }
    auto dir = cacheDir();
    getBlurIOPool()->enqueue([dir]() {
        std::error_code ec;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".pblur") {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
            }
        }
    });
}

void BlurDiskCache::shutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
}


std::string makeKey(std::int64_t sourceID, int thumbIndex, char const* style,
                    int intensity, int width, int height) {
    return fmt::format("lvl{}_i{}_{}_q{}_{}x{}",
        sourceID, thumbIndex, style ? style : "paimon", intensity, width, height);
}

} // namespace paimon::blur
