#include "BlurDiskCache.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/cocos/platform/CCImage.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <shared_mutex>
#include <system_error>

#include "../utils/ThreadPool.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::blur {

// dedicated 1-thread i/o pool. heap-leaked on purpose: joining during atexit
// hung exit for seconds; shutdownbluriopool() joins it on exiting instead.
static std::atomic<paimon::ThreadPool*> s_blurIOPool{nullptr};

static paimon::ThreadPool* getBlurIOPool() {
    static auto* pool = []() {
        auto* p = new paimon::ThreadPool(1, "PaimonBlurIO");
        s_blurIOPool.store(p, std::memory_order_release);
        return p;
    }();
    return pool;
}

BlurDiskCache& BlurDiskCache::get() {
    // heap leak is intentional: avoids destructor running during atexit while i/o
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

            if (ie.width <= 0 || ie.height <= 0 || ie.width > 8192 || ie.height > 8192 ||
                static_cast<std::uint64_t>(ie.width) * ie.height > MAX_PIXEL_COUNT) {
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
        if (!rmEc) {
            totalBytes -= ie.byteSize;
            m_index.erase(key);
        }
    }
}

bool BlurDiskCache::hasEntry(std::string const& key) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_index.find(key) != m_index.end();
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

    bool found = false;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        found = m_index.find(key) != m_index.end();
    }
    if (!found) {
        onReady(nullptr);
        return;
    }

    getBlurIOPool()->enqueue([this, key, onReady = std::move(onReady)]() {
        auto fail = [&onReady] {
            Loader::get()->queueInMainThread([onReady]() { onReady(nullptr); });
        };
        if (m_shuttingDown.load(std::memory_order_acquire)) {
            fail();
            return;
        }

        auto path = pathForKey(key);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_index.erase(key);
            }
            fail();
            return;
        }

        std::ifstream f(path, std::ios::binary);
        if (!f) {
            fail();
            return;
        }

        std::uint32_t header[5] = {0};
        f.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!f || header[0] != MAGIC || header[1] != VERSION) {
            fail();
            return;
        }

        int w = static_cast<int>(header[2]);
        int h = static_cast<int>(header[3]);
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192 ||
            static_cast<std::uint64_t>(w) * h > MAX_PIXEL_COUNT) {
            fail();
            return;
        }

        std::size_t pixelBytes = static_cast<std::size_t>(w) * h * 4;
        auto const fileBytes = std::filesystem::file_size(path, ec);
        if (ec || fileBytes != HEADER_SIZE + pixelBytes) {
            fail();
            return;
        }
        std::vector<uint8_t> pixels(pixelBytes);
        f.read(reinterpret_cast<char*>(pixels.data()), pixelBytes);
        if (!f) {
            fail();
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
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        static_cast<std::uint64_t>(width) * height > MAX_PIXEL_COUNT) return;

    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_index.find(key) != m_index.end()) return;
    }

    // glreadpixels needs the gl main thread; render to a temp rt and read back via newccimage.
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
    if (!data || ow <= 0 || oh <= 0 || ow > 8192 || oh > 8192 ||
        static_cast<std::uint64_t>(ow) * oh > MAX_PIXEL_COUNT) {
        img->release();
        return;
    }

    std::size_t pixelBytes = static_cast<std::size_t>(ow) * oh * 4;
    auto pixels = std::make_shared<std::vector<uint8_t>>(data, data + pixelBytes);
    img->release();

    persistPixelsAsync(key, std::move(pixels), ow, oh);
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
        // index with the file's own mtime: steady_clock and file_clock have
        // different epochs, which broke lru eviction ordering across sessions.
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

void BlurDiskCache::clear() {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_index.clear();
    }
    auto dir = cacheDir();
    auto wipe = [dir]() {
        std::error_code ec;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".pblur") {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
            }
        }
    };

    // a stopped pool drops enqueued jobs, so wipe inline when it is gone.
    auto* pool = getBlurIOPool();
    if (!pool || pool->isStopped()) {
        wipe();
        return;
    }
    pool->enqueue(std::move(wipe));
}

void BlurDiskCache::shutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    // join here, not in static destruction
    if (auto* pool = s_blurIOPool.load(std::memory_order_acquire)) {
        pool->shutdown();
    }
}


} // namespace paimon::blur
