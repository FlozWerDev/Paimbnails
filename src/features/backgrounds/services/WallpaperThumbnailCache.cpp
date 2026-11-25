#include "WallpaperThumbnailCache.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/RgbaScaler.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::wallpaper {

namespace {

// preview decodes are io + cpu heavy; a couple at a time keeps the list warm
// without starving the game.
constexpr size_t kMaxConcurrentDecodes = 3;

// previews are jpegs/pngs picked by the wallpaper author; cap the read so a
// bad file cannot balloon memory.
constexpr size_t kMaxPreviewSizeMB = 16;

// downscaled entries are tiny, but keep a ceiling for very large libraries.
constexpr size_t kMaxEntries = 1024;

std::atomic<bool> s_wpThumbnailCacheInitialized{false};

} // namespace

bool WallpaperThumbnailCache::isInitialized() {
    return s_wpThumbnailCacheInitialized.load(std::memory_order_acquire);
}

WallpaperThumbnailCache& WallpaperThumbnailCache::get() {
    // leaked on purpose: releasing gl textures from a static destructor after
    // the context is gone crashes on exit.
    static auto* instance = []() {
        auto* cache = new WallpaperThumbnailCache();
        s_wpThumbnailCacheInitialized.store(true, std::memory_order_release);
        return cache;
    }();
    return *instance;
}

std::string WallpaperThumbnailCache::makeKey(std::filesystem::path const& path, int maxDim) {
    return paimon::paths::toUtf8(path) + "#" + std::to_string(maxDim);
}

void WallpaperThumbnailCache::shutdown() {
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
        m_queue.clear();
        m_ready.clear();
        m_failed.clear();
    }

    m_textures.clear();
    m_order.clear();
    m_revision.fetch_add(1, std::memory_order_acq_rel);
}

void WallpaperThumbnailCache::clearTextures() {
    {
        std::lock_guard lock(m_mutex);
        // decodes that finished before the reload still hold valid pixels.
        m_failed.clear();
    }
    m_textures.clear();
    m_order.clear();
    m_revision.fetch_add(1, std::memory_order_acq_rel);
}

cocos2d::CCTexture2D* WallpaperThumbnailCache::request(std::filesystem::path const& path, int maxDim) {
    if (path.empty() || maxDim <= 0) return nullptr;

    auto const key = makeKey(path, maxDim);
    if (auto it = m_textures.find(key); it != m_textures.end()) {
        return it->second.data();
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_stopping || m_requested.contains(key) || m_failed.contains(key)) return nullptr;
        m_requested.insert(key);
        m_queue.push_back(Pending{key, path, maxDim});
        startQueuedLocked();
    }

    return nullptr;
}

bool WallpaperThumbnailCache::hasPending() const {
    std::lock_guard lock(m_mutex);
    return !m_queue.empty() || m_running > 0;
}

void WallpaperThumbnailCache::startQueuedLocked() {
    while (!m_stopping && m_running < kMaxConcurrentDecodes && !m_queue.empty()) {
        auto pending = std::move(m_queue.front());
        m_queue.erase(m_queue.begin());
        ++m_running;

        bool const spawned = paimon::ThreadTracker::get().spawn([this, pending = std::move(pending)]() {
            geode::utils::thread::setName("PaimonWpThumb");
            decode(pending);
        });

        if (!spawned) {
            --m_running;
            m_requested.erase(pending.key);
            m_failed.insert(pending.key);
            break;
        }
    }
}

void WallpaperThumbnailCache::decode(Pending pending) {
    Decoded decoded;
    decoded.key = pending.key;

    try {
        auto bytes = ImageLoadHelper::readBinaryFile(pending.path, kMaxPreviewSizeMB);
        if (!bytes.empty() && !paimon::isRuntimeShuttingDown()) {
            int width = 0;
            int height = 0;
            ImageLoadHelper::StbDecodeError decodeError = ImageLoadHelper::StbDecodeError::None;
            auto pixels = ImageLoadHelper::decodeRGBAFromMemory(
                bytes.data(), bytes.size(), width, height, &decodeError);

            if (pixels && width > 0 && height > 0) {
                int const longest = std::max(width, height);
                int dstW = width;
                int dstH = height;

                if (longest > pending.maxDim) {
                    float const scale = static_cast<float>(pending.maxDim) / static_cast<float>(longest);
                    dstW = std::max(1, static_cast<int>(width * scale));
                    dstH = std::max(1, static_cast<int>(height * scale));
                }

                if (dstW > 0 && dstH > 0) {
                    auto scaled = paimon::rgba::scale(pixels.get(), width, height, dstW, dstH);
                    if (scaled) {
                        decoded.rgba = std::shared_ptr<uint8_t[]>(scaled.release());
                        decoded.width = dstW;
                        decoded.height = dstH;
                    }
                }
            }
        }
    } catch (...) {
        // a failed allocation must not leave the queue thinking this job runs.
        decoded.rgba.reset();
    }

    bool const ready = decoded.rgba != nullptr;
    {
        std::lock_guard lock(m_mutex);
        if (m_running > 0) --m_running;
        // once finished the key is either cached (map lookup wins) or failed;
        // dropping it from the pending set lets evicted entries decode again.
        m_requested.erase(pending.key);
        if (ready) {
            m_ready.push_back(std::move(decoded));
        } else {
            m_failed.insert(pending.key);
        }
        startQueuedLocked();
    }

    if (!ready) return;
    if (paimon::isRuntimeShuttingDown()) return;

    Loader::get()->queueInMainThread([]() {
        if (paimon::isRuntimeShuttingDown()) return;
        WallpaperThumbnailCache::get().pump();
    });
}

bool WallpaperThumbnailCache::addTexture(Decoded const& decoded) {
    if (!decoded.rgba || decoded.width <= 0 || decoded.height <= 0) return false;

    auto texture = geode::Ref<CCTexture2D>::adopt(new (std::nothrow) CCTexture2D());
    if (!texture) return false;

    if (!texture->initWithData(
            decoded.rgba.get(),
            kCCTexture2DPixelFormat_RGBA8888,
            decoded.width,
            decoded.height,
            CCSize(static_cast<float>(decoded.width), static_cast<float>(decoded.height)))) {
        return false;
    }

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    if (!m_textures.emplace(decoded.key, texture).second) return false;

    m_order.push_back(decoded.key);
    while (m_order.size() > kMaxEntries) {
        m_textures.erase(m_order.front());
        m_order.erase(m_order.begin());
    }

    m_revision.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

size_t WallpaperThumbnailCache::pump() {
    std::vector<Decoded> ready;
    {
        std::lock_guard lock(m_mutex);
        ready.swap(m_ready);
        startQueuedLocked();
    }

    size_t added = 0;
    for (auto const& decoded : ready) {
        if (addTexture(decoded)) ++added;
    }
    return added;
}

} // namespace paimon::wallpaper
