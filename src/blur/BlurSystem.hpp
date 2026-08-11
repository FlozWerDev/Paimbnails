#pragma once

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

    cocos2d::CCSprite* createBlurredSprite(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity
    ) {
        return Shaders::createBlurredSprite(texture, targetSize, intensity);
    }

    // DEPRECATED: synchronous — freezes when many cells blur at once. Prefer buildPaimonBlurAsync().
    cocos2d::CCSprite* createPaimonBlurSprite(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity
    ) {
        return Shaders::createPaimonBlurSprite(texture, targetSize, intensity);
    }

    cocos2d::CCGLProgram* getRealtimeBlurShader() {
        return Shaders::getPaimonBlurShader();
    }

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

    // Bypasses the concurrency limit.
    void buildPaimonBlurPriority(
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
        std::string cacheKey,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    void buildGaussianBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    // Bypasses the concurrency limit.
    void buildGaussianBlurPriority(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::string cacheKey,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    void clearBlurCache();
    void clearDiskCache();
    // Cancela jobs y suelta texturas RAM antes de que GD recree el contexto GL
    // (GameManager::reloadAll). A diferencia de destroy(), el sistema sigue
    // usable: los blurs se regeneran lazy (disco/GPU) tras el reload.
    void onGLContextReload();
    void onWindowResized(int /*w*/, int /*h*/) {}
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
    // Each entry is a CCTexture2D ~100-400KB (~40-80MB total at 192 entries).
    static constexpr std::size_t MAX_BLUR_CACHE_ENTRIES = 192;
#endif

    std::list<BlurKey> m_blurLru;
    std::unordered_map<BlurKey, Entry, BlurKeyHash> m_blurCache;

    // Consolidate duplicate callbacks for in-flight jobs.
    std::unordered_map<BlurKey, std::vector<std::function<void(cocos2d::CCSprite*)>>, BlurKeyHash> m_inFlight;

    // Cap on parallel blur jobs to prevent GPU saturation during fast scroll.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::size_t MAX_CONCURRENT_BLUR_JOBS = 1;
#else
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
