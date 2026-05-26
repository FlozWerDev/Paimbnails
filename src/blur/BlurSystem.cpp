#include "BlurSystem.hpp"
#include "BlurDiskCache.hpp"

#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace cocos2d;

// ─────────────────────────────────────────────────────────────────────
// Cache helpers
// ─────────────────────────────────────────────────────────────────────

BlurSystem::BlurKey BlurSystem::makeBlurKey(CCTexture2D* source, CCSize const& targetSize, float intensity, std::string const& cacheKey) {
    // Bucket de intensidad en pasos de 0.5 — evita perder cache por deltas
    // pequeños cuando el usuario mueve el slider, pero mantiene la clave estable
    // para valores típicos (2.0, 3.5, 5.0, 7.5, etc).
    int intensityBucket = std::clamp(static_cast<int>(std::round(intensity * 2.0f)), 0, 20);
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

// Genera una key para disk cache a partir del BlurKey. Solo retorna algo no-vacio
// si la sourceKey es persistente (no un pointer "tex:"). Los pointers no sobreviven
// entre sesiones del juego.
static std::string makeDiskKey(BlurSystem::BlurKey const& k, BlurSystem::BlurFlavor flavor) {
    if (k.sourceKey.empty() || k.sourceKey.rfind("tex:", 0) == 0) {
        return {};
    }
    // Sanitiza caracteres problematicos en nombres de archivo. El separador
    // ':' del sourceKey se convierte en '_', ya que es fs-unsafe en Windows.
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
// Disk cache integration
// ─────────────────────────────────────────────────────────────────────

// Intenta cargar el blur desde disk cache. Si existe, lo sube a GPU y notifica
// a todos los callbacks en vuelo. Devuelve true si se despacho el lookup (no
// significa que haya hit — el callback decide eso).
bool BlurSystem::tryDispatchFromDisk(BlurKey const& key, BlurFlavor flavor, QueuedJob const& fallbackJob) {
    std::string diskKey = makeDiskKey(key, flavor);
    if (diskKey.empty()) return false;
    if (!paimon::blur::BlurDiskCache::get().hasEntry(diskKey)) return false;

    // Retain texture source por si el fallback GPU se dispara
    paimon::blur::BlurDiskCache::get().lookupAsync(diskKey,
        [this, key, fallbackJob](CCTexture2D* diskTex) {
            if (diskTex) {
                // Hit confirmado en disco — cachear en RAM y notificar
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
                // Disk fallo (archivo corrupto o race). Fallback: arrancar GPU job.
                if (m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS) {
                    dispatchJob(fallbackJob);
                } else {
                    m_pendingJobs.push_back(fallbackJob);
                }
            }
        });
    return true;
}

void BlurSystem::clearDiskCache() {
    paimon::blur::BlurDiskCache::get().clear();
    clearBlurCache();
}

// ─────────────────────────────────────────────────────────────────────
// Job dispatch con concurrency limit
// ─────────────────────────────────────────────────────────────────────

void BlurSystem::dispatchJob(QueuedJob const& jobDesc) {
    auto* src = jobDesc.source.data();
    if (!src) {
        onJobCompleted(jobDesc.key, nullptr);
        return;
    }

    auto completionCb = [this, key = jobDesc.key](CCSprite* result) {
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
    ++m_activeJobCount;
    job->setFastMode(jobDesc.fastMode);
    job->start();
}

void BlurSystem::onJobCompleted(BlurKey const& key, CCSprite* result) {
    if (m_activeJobCount > 0) --m_activeJobCount;

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

            // Persiste a disco (background, fire-and-forget). Solo para keys
            // persistentes — cuando el caller paso una cacheKey estable.
            // El flavor no importa aqui para storage (usamos la misma key),
            // pero el makeDiskKey detecta paimon vs gauss via intensityBucket.
            BlurFlavor flavor = (key.intensityBucket >= 1000) ? BlurFlavor::Gaussian : BlurFlavor::Paimon;
            std::string diskKey = makeDiskKey(key, flavor);
            if (!diskKey.empty() && !paimon::blur::BlurDiskCache::get().hasEntry(diskKey)) {
                paimon::blur::BlurDiskCache::get().storeFromTextureAsync(
                    diskKey, cachedTex, key.w, key.h);
            }
        }
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

    drainPendingJobs();
}

void BlurSystem::drainPendingJobs() {
    while (m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS && !m_pendingJobs.empty()) {
        QueuedJob next = std::move(m_pendingJobs.front());
        m_pendingJobs.pop_front();
        dispatchJob(next);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Async Dual Kawase con cache
// ─────────────────────────────────────────────────────────────────────

void BlurSystem::buildPaimonBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    if (!onReady) return;
    if (!source || targetSize.width <= 0.f || targetSize.height <= 0.f) {
        onReady(nullptr);
        return;
    }

    BlurKey key = makeBlurKey(source, targetSize, intensity, cacheKey);

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

    m_inFlight[key].push_back(std::move(onReady));

    QueuedJob job{key, geode::Ref<CCTexture2D>(source), targetSize, intensity, BlurFlavor::Paimon};

    // Intenta disk cache primero — evita TODO el trabajo GPU si la imagen
    // ya fue calculada en una sesion anterior.
    if (tryDispatchFromDisk(key, BlurFlavor::Paimon, job)) return;

    if (m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS) {
        dispatchJob(job);
    } else {
        // Encolar — se despachara cuando un slot quede libre.
        m_pendingJobs.push_back(std::move(job));
    }
}

void BlurSystem::buildPaimonBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::function<void(CCSprite*)> onReady
) {
    buildPaimonBlurAsync(source, targetSize, intensity, {}, std::move(onReady));
}

void BlurSystem::buildPaimonBlurPriority(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    if (!onReady) return;
    if (!source || targetSize.width <= 0.f || targetSize.height <= 0.f) {
        onReady(nullptr);
        return;
    }

    BlurKey key = makeBlurKey(source, targetSize, intensity, cacheKey);

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
    QueuedJob job{key, geode::Ref<CCTexture2D>(source), targetSize, intensity, BlurFlavor::Paimon, /*fastMode*/true};

    // Disk cache primero: incluso en priority path, un disk hit es mas rapido
    // que correr el GPU job.
    if (tryDispatchFromDisk(key, BlurFlavor::Paimon, job)) return;

    // Priority: bypass el limite, despachar inmediatamente
    dispatchJob(job);
}

// ─────────────────────────────────────────────────────────────────────
// Async Gaussian con cache (misma estrategia que buildPaimonBlurAsync)
// ─────────────────────────────────────────────────────────────────────

void BlurSystem::buildGaussianBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
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
    BlurKey key = makeBlurKey(source, targetSize, intensity, cacheKey);
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

    QueuedJob job{key, geode::Ref<CCTexture2D>(source), targetSize, intensity, BlurFlavor::Gaussian};

    if (tryDispatchFromDisk(key, BlurFlavor::Gaussian, job)) return;

    if (m_activeJobCount < MAX_CONCURRENT_BLUR_JOBS) {
        dispatchJob(job);
    } else {
        m_pendingJobs.push_back(std::move(job));
    }
}

void BlurSystem::buildGaussianBlurAsync(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::function<void(CCSprite*)> onReady
) {
    buildGaussianBlurAsync(source, targetSize, intensity, {}, std::move(onReady));
}

void BlurSystem::buildGaussianBlurPriority(
    CCTexture2D* source,
    CCSize const& targetSize,
    float intensity,
    std::string cacheKey,
    std::function<void(CCSprite*)> onReady
) {
    if (!onReady) return;
    if (!source || targetSize.width <= 0.f || targetSize.height <= 0.f) {
        onReady(nullptr);
        return;
    }

    BlurKey key = makeBlurKey(source, targetSize, intensity, cacheKey);
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
    QueuedJob job{key, geode::Ref<CCTexture2D>(source), targetSize, intensity, BlurFlavor::Gaussian, /*fastMode*/true};

    if (tryDispatchFromDisk(key, BlurFlavor::Gaussian, job)) return;

    dispatchJob(job);
}
