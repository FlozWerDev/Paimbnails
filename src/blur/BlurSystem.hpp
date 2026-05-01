#pragma once
// BlurSystem — thin singleton wrapper over Shaders:: blur utilities.
// Keeps all existing #include "../blur/BlurSystem.hpp" working without changes.

#include <Geode/utils/cocos.hpp>
#include "../utils/Shaders.hpp"
#include <functional>
#include <list>
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
    /// DEPRECATED: sincrono — causa freezes cuando N celdas aplican blur a la vez.
    /// Prefiere buildPaimonBlurAsync() para listas con muchas celdas.
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

    /// Async Dual Kawase blur con cache RAM (LRU).
    /// - Cache hit: llama onReady en el mismo frame con un sprite nuevo del texture cacheado.
    /// - Cache miss: despacha un ProgressiveBlurJob que reparte los ~4-16 passes FBO
    ///   entre varios frames (desktop: 6 ops/tick, mobile: 2) y llama onReady cuando termina.
    /// - Varios callers con la misma key comparten un solo job.
    /// Diseñado para LevelCell list-view: evita el freeze de N × passes en main thread.
    void buildPaimonBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    /// Async Gaussian 2-pass blur con la misma estrategia que buildPaimonBlurAsync.
    /// Para callers que antes usaban createBlurredSprite() sincrono (LevelInfoLayer,
    /// GJScoreCell, etc.) pero se puede diferir al siguiente frame sin salto visible.
    void buildGaussianBlurAsync(
        cocos2d::CCTexture2D* source,
        cocos2d::CCSize const& targetSize,
        float intensity,
        std::function<void(cocos2d::CCSprite*)> onReady
    );

    /// Forzar limpieza del cache (shutdown / memory pressure)
    void clearBlurCache();

    /// Called on window resize — no-op (Shaders recalculates per-frame)
    void onWindowResized(int /*w*/, int /*h*/) {}

    /// Called on shutdown — no-op (shader cache lives in CCShaderCache)
    void destroy() { clearBlurCache(); }

private:
    BlurSystem() = default;

    struct BlurKey {
        std::uintptr_t texId;
        int w;
        int h;
        int intensityBucket;
        bool operator==(BlurKey const& o) const {
            return texId == o.texId && w == o.w && h == o.h && intensityBucket == o.intensityBucket;
        }
    };

    struct BlurKeyHash {
        std::size_t operator()(BlurKey const& k) const noexcept {
            std::size_t h = std::hash<std::uintptr_t>{}(k.texId);
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
    // Desktop: usuario puede tener 20-40 celdas visibles + info layer + pause
    // layer con blur simultaneamente. 192 entradas cubren re-entries sin
    // recalcular blur, incluyendo los blur pre-calculados en startup.
    // Cada entry es un CCTexture2D ~100-400KB, total ~40-80MB.
    static constexpr std::size_t MAX_BLUR_CACHE_ENTRIES = 192;
#endif

    std::list<BlurKey> m_blurLru;
    std::unordered_map<BlurKey, Entry, BlurKeyHash> m_blurCache;

    // Jobs en vuelo por key — consolidan callbacks duplicados.
    std::unordered_map<BlurKey, std::vector<std::function<void(cocos2d::CCSprite*)>>, BlurKeyHash> m_inFlight;

    static BlurKey makeBlurKey(cocos2d::CCTexture2D* source, cocos2d::CCSize const& targetSize, float intensity);
    cocos2d::CCTexture2D* lookupBlur(BlurKey const& k);
    void insertBlur(BlurKey const& k, cocos2d::CCTexture2D* tex);
    static cocos2d::CCSprite* spriteFromCachedTexture(cocos2d::CCTexture2D* tex);
};
