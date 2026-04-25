#include "BlurSystem.hpp"

#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace cocos2d;

// ─────────────────────────────────────────────────────────────────────
// Cache helpers
// ─────────────────────────────────────────────────────────────────────

BlurSystem::BlurKey BlurSystem::makeBlurKey(CCTexture2D* source, CCSize const& targetSize, float intensity) {
    // Bucket de intensidad en pasos de 0.5 — evita perder cache por deltas
    // pequeños cuando el usuario mueve el slider, pero mantiene la clave estable
    // para valores típicos (2.0, 3.5, 5.0, 7.5, etc).
    int intensityBucket = std::clamp(static_cast<int>(std::round(intensity * 2.0f)), 0, 20);
    return BlurKey{
        reinterpret_cast<uintptr_t>(source),
        static_cast<int>(std::round(targetSize.width)),
        static_cast<int>(std::round(targetSize.height)),
        intensityBucket
    };
}

CCTexture2D* BlurSystem::lookupBlur(BlurKey const& k) {
    auto it = m_blurCache.find(k);
    if (it == m_blurCache.end()) return nullptr;
    // Move-to-front en LRU
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

    // Evict si estamos sobre capacidad
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

// ─────────────────────────────────────────────────────────────────────
// Async Dual Kawase con cache
// ─────────────────────────────────────────────────────────────────────

void BlurSystem::buildPaimonBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::function<void(CCSprite*)> onReady
) {
    if (!onReady) return;
    if (!source || targetSize.width <= 0.f || targetSize.height <= 0.f) {
        onReady(nullptr);
        return;
    }

    BlurKey key = makeBlurKey(source, targetSize, intensity);

    // Hit: devolver sprite del texture cacheado en el MISMO frame.
    // Asi un re-entry a LevelBrowserLayer no vuelve a pagar el costo del blur.
    if (auto* tex = lookupBlur(key)) {
        onReady(spriteFromCachedTexture(tex));
        return;
    }

    // Ya hay un job en vuelo para la misma key — apilar el callback.
    auto inFlightIt = m_inFlight.find(key);
    if (inFlightIt != m_inFlight.end()) {
        inFlightIt->second.push_back(std::move(onReady));
        return;
    }

    // Nuevo job — arrancar ProgressiveBlurJob.
    m_inFlight[key].push_back(std::move(onReady));

    auto* job = Shaders::ProgressiveBlurJob::createPaimonBlur(
        source, targetSize, intensity,
        [this, key](CCSprite* result) {
            auto it = m_inFlight.find(key);
            if (it == m_inFlight.end()) return;
            auto callbacks = std::move(it->second);
            m_inFlight.erase(it);

            CCTexture2D* cachedTex = nullptr;
            if (result) {
                cachedTex = result->getTexture();
                if (cachedTex) insertBlur(key, cachedTex);
            }

            // Cada callback recibe su propio sprite (no se pueden compartir —
            // un CCSprite solo puede tener un parent).
            for (auto& cb : callbacks) {
                if (!cb) continue;
                if (cachedTex) {
                    cb(spriteFromCachedTexture(cachedTex));
                } else {
                    cb(nullptr);
                }
            }
        });

    if (!job) {
        auto it = m_inFlight.find(key);
        if (it != m_inFlight.end()) {
            auto callbacks = std::move(it->second);
            m_inFlight.erase(it);
            for (auto& cb : callbacks) {
                if (cb) cb(nullptr);
            }
        }
        return;
    }
    job->start();
}

// ─────────────────────────────────────────────────────────────────────
// Async Gaussian con cache (misma estrategia que buildPaimonBlurAsync)
// ─────────────────────────────────────────────────────────────────────

void BlurSystem::buildGaussianBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::function<void(CCSprite*)> onReady
) {
    if (!onReady) return;
    if (!source || targetSize.width <= 0.f || targetSize.height <= 0.f) {
        onReady(nullptr);
        return;
    }

    // Usamos la misma key de cache — los gaussian y dual-kawase comparten cache
    // pero con un prefijo en el intensity bucket para no colisionar.
    // Reservamos bucket 1000+ para gaussian.
    BlurKey key = makeBlurKey(source, targetSize, intensity);
    key.intensityBucket += 1000;

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

    auto* job = Shaders::ProgressiveBlurJob::createGaussian(
        source, targetSize, intensity,
        [this, key](CCSprite* result) {
            auto it = m_inFlight.find(key);
            if (it == m_inFlight.end()) return;
            auto callbacks = std::move(it->second);
            m_inFlight.erase(it);

            CCTexture2D* cachedTex = nullptr;
            if (result) {
                cachedTex = result->getTexture();
                if (cachedTex) insertBlur(key, cachedTex);
            }

            for (auto& cb : callbacks) {
                if (!cb) continue;
                if (cachedTex) {
                    cb(spriteFromCachedTexture(cachedTex));
                } else {
                    cb(nullptr);
                }
            }
        });

    if (!job) {
        auto it = m_inFlight.find(key);
        if (it != m_inFlight.end()) {
            auto callbacks = std::move(it->second);
            m_inFlight.erase(it);
            for (auto& cb : callbacks) {
                if (cb) cb(nullptr);
            }
        }
        return;
    }
    job->start();
}
