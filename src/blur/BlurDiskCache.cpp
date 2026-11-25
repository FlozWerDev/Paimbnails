#include "../utils/FilesystemPath.hpp"
#include "BlurDiskCache.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/cocos/platform/CCImage.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <shared_mutex>
#include <system_error>

#include "../utils/AtomicFileWrite.hpp"
#include "../utils/HashKey.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../utils/ThreadPool.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::blur {

// dedicated i/o pool. heap-leaked on purpose: joining during atexit hung exit
// for seconds; shutdown() / waitShutdown() joins it on exiting instead. two
// threads: a visible-tile lookup no longer waits behind another tile's write.
static std::mutex& blurIOPoolMutex() {
    static auto* m = new std::mutex();
    return *m;
}
static std::atomic<paimon::ThreadPool*> s_blurIOPool{nullptr};
static std::atomic<bool> s_blurShuttingDown{false};
static std::atomic<bool> s_blurInitialized{false};

static paimon::ThreadPool* getBlurIOPool(bool createIfMissing = true) {
    if (s_blurShuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return s_blurIOPool.load(std::memory_order_acquire);
    }
    if (!createIfMissing) {
        return s_blurIOPool.load(std::memory_order_acquire);
    }
    std::lock_guard<std::mutex> lock(blurIOPoolMutex());
    if (s_blurShuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return s_blurIOPool.load(std::memory_order_acquire);
    }
    auto* p = s_blurIOPool.load(std::memory_order_relaxed);
    if (!p) {
        p = new paimon::ThreadPool(2, "PaimonBlurIO");
        s_blurIOPool.store(p, std::memory_order_release);
    }
    return p;
}

static std::string normalizeDiskKey(std::string const& key) {
    if (key.size() <= 64) {
        return key;
    }
    return fmt::format("{}_{:016x}", key.substr(0, 24), paimon::hash::fnv1a64(key));
}

// validates a .pblur header; returns {width, height} or nullopt.
std::optional<std::pair<int, int>> BlurDiskCache::parseHeaderDims(std::uint32_t const header[5]) {
    if (header[0] != MAGIC || header[1] != VERSION) {
        return std::nullopt;
    }
    int w = static_cast<int>(header[2]);
    int h = static_cast<int>(header[3]);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 ||
        static_cast<std::uint64_t>(w) * h > MAX_PIXEL_COUNT) {
        return std::nullopt;
    }
    return std::pair{w, h};
}

bool BlurDiskCache::isInitialized() {
    return s_blurInitialized.load(std::memory_order_acquire);
}

BlurDiskCache& BlurDiskCache::get() {
    // heap leak is intentional: avoids destructor running during atexit while i/o
    // workers may still touch the index. shutdown() flips the atomic flag.
    static BlurDiskCache* instance = []() {
        auto* cache = new BlurDiskCache();
        s_blurInitialized.store(true, std::memory_order_release);
        return cache;
    }();
    return *instance;
}

std::filesystem::path BlurDiskCache::cacheDir() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "blur_cache";
}

std::filesystem::path BlurDiskCache::pathForKey(std::string const& key) const {
    return cacheDir() / (normalizeDiskKey(key) + ".pblur");
}

void BlurDiskCache::init() {
    if (m_shuttingDown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return;
    }
    bool expected = false;
    if (!m_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    auto* pool = getBlurIOPool();
    if (!pool || pool->isStopped()) {
        return;
    }
    pool->enqueue([this]() {
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
            if (path.extension() == ".tmp") {
                // stale AtomicFileWrite temp (<key>.pblur.<pid>.<stamp>.<seq>.tmp)
                // left by a crash mid-write: never indexed, never evicted.
                std::error_code rmEc;
                std::filesystem::remove(path, rmEc);
                continue;
            }
            if (path.extension() != ".pblur") continue;

            // NOTE (Windows): MSVC opens ifstream without delete-sharing, so
            // remove() fails while the handle is open. removeCorrupt always
            // closes the stream first; close() on a non-open stream is a no-op.
            std::ifstream f;
            auto removeCorrupt = [&path, &f] {
                f.close();
                std::error_code rmEc;
                std::filesystem::remove(path, rmEc);
            };

            IndexEntry ie;
            ie.byteSize = static_cast<std::int64_t>(entry.file_size(ec));
            if (ec) { ec.clear(); removeCorrupt(); continue; }

            auto ftime = std::filesystem::last_write_time(path, ec);
            if (!ec) {
                ie.mtimeEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count();
            }
            ec.clear();

            f.open(path, std::ios::binary);
            if (!f) { removeCorrupt(); continue; }
            std::uint32_t header[5] = {0};
            f.read(reinterpret_cast<char*>(header), sizeof(header));
            if (!f || header[0] != MAGIC || header[1] != VERSION) { removeCorrupt(); continue; }
            auto dims = parseHeaderDims(header);
            if (!dims) {
                log::debug("[BlurDiskCache] entry {} has invalid dimensions {}x{}",
                    paimon::paths::toUtf8(path.stem()),
                    static_cast<int>(header[2]), static_cast<int>(header[3]));
                removeCorrupt();
                continue;
            }
            ie.width = dims->first;
            ie.height = dims->second;

            std::int64_t expectedSize = HEADER_SIZE + static_cast<std::int64_t>(ie.width) * ie.height * 4;
            if (expectedSize != ie.byteSize) {
                log::debug("[BlurDiskCache] corrupted entry {}: expected {} bytes, got {}",
                    paimon::paths::toUtf8(path.stem()), expectedSize, ie.byteSize);
                removeCorrupt();
                continue;
            }

            totalBytes += ie.byteSize;
            loaded.emplace(paimon::paths::toUtf8(path.stem()), ie);
        }

        std::size_t entryCount = 0;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            // merge instead of replace: with a multi-threaded pool a write
            // queued behind the scan can publish an entry that is not in
            // `loaded`, and replacing would drop it from the index.
            for (auto& [key, ie] : loaded) {
                if (m_index.emplace(key, ie).second) {
                    m_totalBytes += ie.byteSize;
                }
            }
            // evictIndexIfNeededLocked keeps m_totalBytes in sync as it erases.
            evictIndexIfNeededLocked();
            entryCount = m_index.size();
            totalBytes = m_totalBytes;
        }
        log::info("[BlurDiskCache] initialized: {} entries, ~{} MB",
            entryCount, totalBytes / (1024 * 1024));
    });
}

void BlurDiskCache::evictIndexIfNeededLocked() {
    if (m_totalBytes <= MAX_DISK_SIZE_BYTES) return;

    std::vector<std::pair<std::string, IndexEntry>> sorted(m_index.begin(), m_index.end());
    std::sort(sorted.begin(), sorted.end(),
        [](auto const& a, auto const& b) { return a.second.mtimeEpoch < b.second.mtimeEpoch; });
    for (auto const& [key, ie] : sorted) {
        if (m_totalBytes <= MAX_DISK_SIZE_BYTES) break;
        std::error_code rmEc;
        std::filesystem::remove(pathForKey(key), rmEc);
        if (!rmEc) {
            m_totalBytes -= ie.byteSize;
            m_index.erase(key);
        }
    }
}

bool BlurDiskCache::hasEntry(std::string const& key) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_index.find(normalizeDiskKey(key)) != m_index.end();
}

CCTexture2D* BlurDiskCache::uploadRawRGBA(std::vector<uint8_t> const& pixels, int w, int h) {
    if (pixels.empty() || w <= 0 || h <= 0) return nullptr;
    std::size_t expected = static_cast<std::size_t>(w) * h * 4;
    if (pixels.size() != expected) return nullptr;

    // blur tiles can exceed the shared 4096 cap, so the helper limit is raised
    // to this cache's own 8192 bound (already validated by lookupAsync).
    auto img = ImageLoadHelper::createFromRGBA(pixels.data(), w, h, false, 8192);
    if (!img.success || !img.texture) return nullptr;
    img.texture->autorelease();
    return img.texture;
}

void BlurDiskCache::lookupAsync(std::string const& rawKey, ReadyCallback onReady) {
    if (!onReady) return;
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        onReady(nullptr);
        return;
    }

    std::string key = normalizeDiskKey(rawKey);
    bool found = false;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        found = m_index.find(key) != m_index.end();
    }
    if (!found) {
        onReady(nullptr);
        return;
    }

    auto* pool = getBlurIOPool();
    if (!pool || pool->isStopped()) {
        onReady(nullptr);
        return;
    }

    pool->enqueue([this, key, onReady = std::move(onReady)]() {
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
                if (auto it = m_index.find(key); it != m_index.end()) {
                    m_totalBytes -= it->second.byteSize;
                    m_index.erase(it);
                }
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
        auto dims = parseHeaderDims(header);
        if (!f || !dims) {
            fail();
            return;
        }

        int w = dims->first;
        int h = dims->second;

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
                onReady(nullptr);
                return;
            }
            auto const uploadStart = std::chrono::steady_clock::now();
            auto* tex = uploadRawRGBA(*pixelsPtr, w, h);
            auto const uploadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - uploadStart).count();
            if (uploadMs >= 40) {
                log::warn("[BlurDiskCache] main-thread GPU upload {}x{} took {}ms", w, h, uploadMs);
            }
            onReady(tex);
        });
    });
}

void BlurDiskCache::storeFromTextureAsync(std::string const& rawKey, CCTexture2D* tex, int width, int height) {
    if (!tex || m_shuttingDown.load(std::memory_order_acquire)) return;
    if (paimon::isRuntimeShuttingDown()) return;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        static_cast<std::uint64_t>(width) * height > MAX_PIXEL_COUNT) return;

    std::string key = normalizeDiskKey(rawKey);

    // CCRenderTexture::create/begin/visit/end + newCCImage are main/gl-thread
    // only. callers run on the main thread (blur completion); refuse to run
    // on a dead context rather than crashing inside gl.
    auto* director = CCDirector::get();
    if (!director || !director->getOpenGLView()) return;
    if (typeinfo_cast<CCTransitionScene*>(director->getRunningScene()) != nullptr) return;

    // Rate-limit glReadPixels readbacks to avoid multi-frame hitches during scene transitions.
    static auto s_lastReadback = std::chrono::steady_clock::time_point{};
    auto const now = std::chrono::steady_clock::now();
    if (s_lastReadback.time_since_epoch().count() != 0 &&
        now - s_lastReadback < std::chrono::milliseconds(40)) {
        return;
    }
    s_lastReadback = now;

    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_index.find(key) != m_index.end()) return;
    }

    auto const readbackStart = std::chrono::steady_clock::now();

    // glreadpixels needs the gl main thread; render to a temp rt and read back via newccimage.
    // persist at 1x point resolution: the game can run at a 4x content scale,
    // where a 569x320 tile would otherwise cost an 11.6 MB readback + copies
    // on the main thread and an 11.6 MB file on disk.
    float contentScale = director->getContentScaleFactor();
    if (!(contentScale > 0.f)) contentScale = 1.f;
    int w = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) / contentScale)));
    int h = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) / contentScale)));
    auto* rt = CCRenderTexture::create(w, h);
    if (!rt) return;

    auto* sprite = CCSprite::createWithTexture(tex);
    if (!sprite) return;
    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setPosition(CCPoint(w * 0.5f, h * 0.5f));
    sprite->setScaleX(static_cast<float>(w) / static_cast<float>(width));
    sprite->setScaleY(static_cast<float>(h) / static_cast<float>(height));
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

    auto const readbackMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - readbackStart).count();
    if (readbackMs >= 40) {
        log::warn("[BlurDiskCache] main-thread readback {}x{} -> {}x{} took {}ms",
            width, height, ow, oh, readbackMs);
    }

    persistPixelsAsync(key, std::move(pixels), ow, oh);
}

void BlurDiskCache::persistPixelsAsync(std::string key, std::shared_ptr<std::vector<uint8_t>> pixels, int w, int h) {
    auto* pool = getBlurIOPool();
    if (!pool || pool->isStopped()) return;
    pool->enqueue([this, key = std::move(key), pixels = std::move(pixels), w, h]() {
        if (m_shuttingDown.load(std::memory_order_acquire)) return;

        auto path = pathForKey(key);
        std::uint32_t header[5] = {MAGIC, VERSION,
            static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), 0};
        // single contiguous blob so the write below is atomic (tmp+rename):
        // readers never see a truncated .pblur.
        std::vector<uint8_t> blob(sizeof(header) + pixels->size());
        std::memcpy(blob.data(), header, sizeof(header));
        std::memcpy(blob.data() + sizeof(header), pixels->data(), pixels->size());
        if (!paimon::file::writeAtomically(path, std::span<uint8_t const>(blob.data(), blob.size()))) {
            log::debug("[BlurDiskCache] could not write {}", paimon::paths::toUtf8(path));
            return;
        }

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
            if (auto it = m_index.find(key); it != m_index.end()) {
                m_totalBytes -= it->second.byteSize;
            }
            m_index[key] = ie;
            m_totalBytes += ie.byteSize;
            evictIndexIfNeededLocked();
        }
    });
}

void BlurDiskCache::clear() {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_index.clear();
        m_totalBytes = 0;
    }
    auto dir = cacheDir();
    auto wipe = [dir]() {
        std::error_code ec;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            auto const ext = entry.path().extension();
            if (ext == ".pblur" || ext == ".tmp") {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
            }
        }
    };

    // never spawn a fresh IO pool after shutdown has begun; wipe inline if stopped or absent.
    auto* pool = getBlurIOPool(false);
    if (!pool || pool->isStopped()) {
        wipe();
        return;
    }
    pool->enqueue(std::move(wipe));
}

void BlurDiskCache::requestShutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    s_blurShuttingDown.store(true, std::memory_order_release);
    if (auto* pool = s_blurIOPool.load(std::memory_order_acquire)) {
        pool->requestStop();
    }
}

void BlurDiskCache::waitShutdown(std::chrono::steady_clock::time_point deadline) {
    requestShutdown();
    // join here, not in static destruction
    if (auto* pool = s_blurIOPool.load(std::memory_order_acquire)) {
        pool->waitShutdown(deadline);
    }
}

void BlurDiskCache::shutdown() {
    waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}


} // namespace paimon::blur
