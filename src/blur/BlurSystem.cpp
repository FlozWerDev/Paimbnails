#include "BlurSystem.hpp"
#include "BlurDiskCache.hpp"
#include "../utils/UrlKeyNormalize.hpp"

#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace cocos2d;

// gaussian shares the cache; offset avoids collisions with paimon buckets
static constexpr int kGaussianBucketOffset = 1000;

BlurSystem::BlurKey BlurSystem::makeBlurKey(CCTexture2D* source, CCSize const& targetSize, float intensity, std::string const& cacheKey) {
    // Bucket intensity in 0.5 steps to avoid thrashing the cache on small slider deltas.
    int intensityBucket = paimon::cache::blurIntensityBucket(intensity);
    std::string sourceKey = cacheKey;
    if (sourceKey.empty()) {
        sourceKey = fmt::format("tex:{}", reinterpret_cast<uintptr_t>(source));
    }
    return BlurKey{
        std::move(sourceKey),
        static_cast<int>(std::round(targetSize.width)),
        static_cast<int>(std::round(targetSize.height)),
        intensityBucket
    };
}

// Returns empty for pointer-based ("tex:") source keys, which don't survive across sessions.
static std::string makeDiskKey(BlurSystem::BlurKey const& k, BlurSystem::BlurFlavor flavor) {
    if (k.sourceKey.empty() || k.sourceKey.rfind("tex:", 0) == 0) {
        return {};
    }
    std::string safe = k.sourceKey;
    for (auto& c : safe) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') {
            continue;
        }
        c = '_';
    }
    char const* styleStr = (flavor == BlurSystem::BlurFlavor::Paimon) ? "paimon" : "gauss";
    return fmt::format("{}_{}_q{}_{}x{}",
        safe, styleStr, k.intensityBucket, k.w, k.h);
}

CCTexture2D* BlurSystem::lookupBlur(BlurKey const& k) {
    auto it = m_blurCache.find(k);
    if (it == m_blurCache.end()) return nullptr;
    m_blurLru.erase(it->second.lruIt);
    m_blurLru.push_front(k);
    it->second.lruIt = m_blurLru.begin();
    return it->second.texture.data();
}

void BlurSystem::insertBlur(BlurKey const& k, CCTexture2D* tex) {
    if (!tex) return;

    auto existing = m_blurCache.find(k);
    if (existing != m_blurCache.end()) {
        existing->second.texture = tex;
        m_blurLru.erase(existing->second.lruIt);
        m_blurLru.push_front(k);
        existing->second.lruIt = m_blurLru.begin();
        return;
    }

    while (m_blurCache.size() >= MAX_BLUR_CACHE_ENTRIES && !m_blurLru.empty()) {
        auto const& oldKey = m_blurLru.back();
        m_blurCache.erase(oldKey);
        m_blurLru.pop_back();
    }

    m_blurLru.push_front(k);
    Entry e;
    e.lruIt = m_blurLru.begin();
    e.texture = tex;
    m_blurCache.emplace(k, std::move(e));
}

CCSprite* BlurSystem::spriteFromCachedTexture(CCTexture2D* tex) {
    if (!tex) return nullptr;
    return CCSprite::createWithTexture(tex);
}

void BlurSystem::clearBlurCache() {
    m_blurCache.clear();
    m_blurLru.clear();
}

void BlurSystem::destroy() {
    m_shutdown = true;
    abortJobs();
}

void BlurSystem::onGLContextReload() {
    abortJobs();
}

void BlurSystem::abortJobs() {
    for (auto& job : m_runningJobs) {
        if (job) job->cancel();
    }
    m_runningJobs.clear();

    for (auto& [key, callbacks] : m_inFlight) {
        for (auto& cb : callbacks) {
            if (cb) cb(nullptr);
        }
    }
    m_inFlight.clear();
    m_pendingJobs.clear();
    m_activeJobCount = 0;

    clearBlurCache();
}

bool BlurSystem::tryDispatchFromDisk(BlurKey const& key, BlurFlavor flavor, QueuedJob const& fallbackJob) {
    std::string diskKey = makeDiskKey(key, flavor);
    if (diskKey.empty()) return false;
    if (!paimon::blur::BlurDiskCache::get().hasEntry(diskKey)) return false;

    paimon::blur::BlurDiskCache::get().lookupAsync(diskKey,
        [this, key, fallbackJob](CCTexture2D* diskTex) {
            if (m_shutdown) return;
            if (diskTex) {
                insertBlur(key, diskTex);
                auto it = m_inFlight.find(key);
                if (it != m_inFlight.end()) {
                    auto callbacks = std::move(it->second);
                    m_inFlight.erase(it);
                    for (auto& cb : callbacks) {
                        if (cb) cb(spriteFromCachedTexture(diskTex));
                    }
                }
                drainPendingJobs();
            } else {
                // disk miss; fall back to a GPU job
                if (m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS) {
                    dispatchJob(fallbackJob);
                } else {
                    m_pendingJobs.push_back(fallbackJob);
                }
            }
        });
    return true;
}

void BlurSystem::dispatchJob(QueuedJob const& jobDesc) {
    // Reserve slot up-front so onJobCompleted() balances the counter on every exit path.
    ++m_activeJobCount;
    auto* src = jobDesc.source.data();
    if (!src) {
        onJobCompleted(jobDesc.key, nullptr);
        return;
    }

    auto completionCb = [this, key = jobDesc.key](CCSprite* result) {
        if (m_shutdown) return;
        onJobCompleted(key, result);
    };

    Shaders::ProgressiveBlurJob* job = nullptr;
    if (jobDesc.flavor == BlurFlavor::Paimon) {
        job = Shaders::ProgressiveBlurJob::createPaimonBlur(
            src, jobDesc.targetSize, jobDesc.intensity, std::move(completionCb));
    } else {
        job = Shaders::ProgressiveBlurJob::createGaussian(
            src, jobDesc.targetSize, jobDesc.intensity, std::move(completionCb));
    }

    if (!job) {
        onJobCompleted(jobDesc.key, nullptr);
        return;
    }
    job->setFastMode(jobDesc.fastMode);
    m_runningJobs.push_back(job);
    job->start();
}

void BlurSystem::onJobCompleted(BlurKey const& key, CCSprite* result) {
    if (m_shutdown) {
        if (m_activeJobCount > 0) --m_activeJobCount;
        return;
    }
    if (m_activeJobCount > 0) --m_activeJobCount;

    // Purge finished jobs so retained Ref<> don't pin FBO textures.
    m_runningJobs.erase(
        std::remove_if(m_runningJobs.begin(), m_runningJobs.end(),
            [](geode::Ref<Shaders::ProgressiveBlurJob> const& j) {
                return !j || j->isDone();
            }),
        m_runningJobs.end()
    );

    auto it = m_inFlight.find(key);
    if (it == m_inFlight.end()) {
        drainPendingJobs();
        return;
    }
    auto callbacks = std::move(it->second);
    m_inFlight.erase(it);

    CCTexture2D* cachedTex = nullptr;
    if (result) {
        cachedTex = result->getTexture();
        if (cachedTex) {
            insertBlur(key, cachedTex);

            // Persist to disk (fire-and-forget), only for persistent keys.
            BlurFlavor flavor = (key.intensityBucket >= kGaussianBucketOffset) ? BlurFlavor::Gaussian : BlurFlavor::Paimon;
            std::string diskKey = makeDiskKey(key, flavor);
            if (!diskKey.empty() && !paimon::blur::BlurDiskCache::get().hasEntry(diskKey)) {
                paimon::blur::BlurDiskCache::get().storeFromTextureAsync(
                    diskKey, cachedTex, key.w, key.h);
            }
        }
    }

    // Each callback gets its own sprite (a CCSprite can only have one parent).
    for (auto& cb : callbacks) {
        if (!cb) continue;
        if (cachedTex) {
            cb(spriteFromCachedTexture(cachedTex));
        } else {
            cb(nullptr);
        }
    }

    drainPendingJobs();
}

void BlurSystem::drainPendingJobs() {
    while (m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS && !m_pendingJobs.empty()) {
        QueuedJob next = std::move(m_pendingJobs.front());
        m_pendingJobs.pop_front();
        dispatchJob(next);
    }
}

void BlurSystem::enqueueBuild(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady,
    BlurFlavor flavor,
    bool fastMode,
    bool priority
) {
    if (!onReady) return;
    if (!source || targetSize.width <= 0.f || targetSize.height <= 0.f) {
        onReady(nullptr);
        return;
    }

    BlurKey key = makeBlurKey(source, targetSize, intensity, cacheKey);
    if (flavor == BlurFlavor::Gaussian) key.intensityBucket += kGaussianBucketOffset;

    if (auto* tex = lookupBlur(key)) {
        onReady(spriteFromCachedTexture(tex));
        return;
    }

    auto inFlightIt = m_inFlight.find(key);
    if (inFlightIt != m_inFlight.end()) {
        inFlightIt->second.push_back(std::move(onReady));
        return;
    }

    m_inFlight[key].push_back(std::move(onReady));
    QueuedJob job{key, geode::Ref<CCTexture2D>(source), targetSize, intensity, flavor, fastMode};

    // disk hit beats a GPU job even on the priority path
    if (tryDispatchFromDisk(key, flavor, job)) return;

    if (priority || m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS) {
        dispatchJob(job);
    } else {
        m_pendingJobs.push_back(std::move(job));
    }
}

void BlurSystem::buildPaimonBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    enqueueBuild(source, targetSize, intensity, std::move(cacheKey), std::move(onReady),
        BlurFlavor::Paimon, false, false);
}

void BlurSystem::buildPaimonBlurPriority(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    enqueueBuild(source, targetSize, intensity, std::move(cacheKey), std::move(onReady),
        BlurFlavor::Paimon, true, true);
}

void BlurSystem::buildGaussianBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    enqueueBuild(source, targetSize, intensity, std::move(cacheKey), std::move(onReady),
        BlurFlavor::Gaussian, false, false);
}

void BlurSystem::buildGaussianBlurPriority(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    enqueueBuild(source, targetSize, intensity, std::move(cacheKey), std::move(onReady),
        BlurFlavor::Gaussian, true, true);
}
