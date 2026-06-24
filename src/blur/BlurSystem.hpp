#pragma once
// Singleton wrapper over Shaders:: blur utilities (preserves existing includes).

#include <Geode/utils/cocos.hpp>
#include "../utils/Shaders.hpp"
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

class BlurSystem {
public:
    static BlurSystem* getInstance() {
        static BlurSystem s_instance;
        return &s_instance;
    }

    /// Gaussian 2-pass blur (delegates to Shaders::createBlurredSprite)
    cocos2d::CCSprite* createBlurredSprite(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity
    ) {
        return Shaders::createBlurredSprite(texture, targetSize, intensity);
    }

    /// Dual Kawase multi-pass blur (delegates to Shaders::createPaimonBlurSprite)
    /// DEPRECATED: synchronous — freezes when many cells blur at once. Prefer buildPaimonBlurAsync().
    cocos2d::CCSprite* createPaimonBlurSprite(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity
    ) {
        return Shaders::createPaimonBlurSprite(texture, targetSize, intensity);
    }

    /// Real-time single-pass blur shader for GIFs / animated sprites
    cocos2d::CCGLProgram* getRealtimeBlurShader() {
        return Shaders::getPaimonBlurShader();
    }

    /// Async Dual Kawase blur with LRU RAM cache.
    void buildPaimonBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::string cacheKey,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    void buildPaimonBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    /// High-priority variant: bypasses the concurrency limit.
    void buildPaimonBlurPriority(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::string cacheKey,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    /// Async Gaussian 2-pass blur with cache.
    void buildGaussianBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::string cacheKey,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    void buildGaussianBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    /// High-priority variant: bypasses the concurrency limit.
    void buildGaussianBlurPriority(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::string cacheKey,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    /// Force-clear the cache (shutdown / memory pressure).
    void clearBlurCache();

    /// Clear disk and RAM caches. Safe to call from UI.
    void clearDiskCache();

    /// Called on window resize — no-op (Shaders recalculates per-frame)
    void onWindowResized(int /*w*/, int /*h*/) {}

    /// Called on shutdown — cancel in-flight jobs and clear caches.
    void destroy();

public:
    struct BlurKey {
        std::string sourceKey;
        int w;
        int h;
        int intensityBucket;
        bool operator==(BlurKey const& o) const {
            return sourceKey == o.sourceKey && w == o.w && h == o.h && intensityBucket == o.intensityBucket;
        }
    };

    enum class BlurFlavor : uint8_t { Paimon, Gaussian };

    static BlurKey makeBlurKey(cocos2d::CCTexture2D* source, cocos2d::CCSize const& targetSize, float intensity, std::string const& cacheKey = {});

private:
    BlurSystem() = default;

    struct BlurKeyHash {
        std::size_t operator()(BlurKey const& k) const noexcept {
            std::size_t h = std::hash<std::string>{}(k.sourceKey);
            h = h * 31 + std::hash<int>{}(k.w);
            h = h * 31 + std::hash<int>{}(k.h);
            h = h * 31 + std::hash<int>{}(k.intensityBucket);
            return h;
        }
    };

    struct Entry {
        std::list<BlurKey>::iterator lruIt;
        geode::Ref<cocos2d::CCTexture2D> texture;
    };

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::size_t MAX_BLUR_CACHE_ENTRIES = 48;
#else
    // Desktop: 192 entries cover 20-40 visible cells + info/pause layers without
    // recompute. Each entry is a CCTexture2D ~100-400KB (~40-80MB total).
    static constexpr std::size_t MAX_BLUR_CACHE_ENTRIES = 192;
#endif

    std::list<BlurKey> m_blurLru;
    std::unordered_map<BlurKey, Entry, BlurKeyHash> m_blurCache;

    // In-flight jobs by key — consolidate duplicate callbacks.
    std::unordered_map<BlurKey, std::vector<std::function<void(cocos2d::CCSprite*)>>, BlurKeyHash> m_inFlight;

    // Global cap on parallel blur jobs; prevents GPU saturation when fast scroll makes
    // many cells visible at once. Extra jobs wait in the queue.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::size_t MAX_CONCURRENT_BLUR_JOBS = 1;
#else
    // Desktop: blur FBO passes compete with the game's render; cap concurrent jobs to reduce GPU stutter.
    static constexpr std::size_t MAX_CONCURRENT_BLUR_JOBS = 3;
#endif
    std::size_t m_activeJobCount = 0;

    struct QueuedJob {
        BlurKey key;
        geode::Ref<cocos2d::CCTexture2D> source;
        cocos2d::CCSize targetSize;
        float intensity;
        BlurFlavor flavor;
        bool fastMode = false;
    };
    std::list<QueuedJob> m_pendingJobs;

    bool m_shutdown = false;
    std::vector<geode::Ref<Shaders::ProgressiveBlurJob>> m_runningJobs;

    cocos2d::CCTexture2D* lookupBlur(BlurKey const& k);
    void insertBlur(BlurKey const& k, cocos2d::CCTexture2D* tex);
    static cocos2d::CCSprite* spriteFromCachedTexture(cocos2d::CCTexture2D* tex);

    void dispatchJob(QueuedJob const& job);
    void onJobCompleted(BlurKey const& key, cocos2d::CCSprite* result);
    void drainPendingJobs();
    bool tryDispatchFromDisk(BlurKey const& key, BlurFlavor flavor, QueuedJob const& fallbackJob);
};
