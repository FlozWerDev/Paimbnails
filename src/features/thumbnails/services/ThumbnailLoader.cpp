#include "ThumbnailLoader.hpp"
#include "ThumbnailTransportClient.hpp"
#include "LocalThumbs.hpp"
#include "LevelColors.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/Constants.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/DominantColors.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/Debug.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <cmath>
#include <algorithm>
#include <tuple>
#include <chrono>
#include <string_view>

using namespace geode::prelude;

// ── helpers ─────────────────────────────────────────────────────────

static bool isVolatileParam(std::string_view key) {
    return key == "_pv" || key == "_cb" || key == "ts" || key == "v" || key == "t";
}

std::string ThumbnailLoader::normalizeUrlKey(std::string const& url) {
    size_t q = url.find('?');
    if (q == std::string::npos) return url;

    std::string_view base(url.data(), q);
    std::string_view query(url.data() + q + 1, url.size() - q - 1);

    std::string out;
    out.reserve(url.size());
    out.append(base);

    bool first = true;
    size_t start = 0;
    while (start < query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string_view::npos) end = query.size();

        std::string_view pair(query.data() + start, end - start);
        size_t eq = pair.find('=');
        std::string_view key = (eq == std::string_view::npos) ? pair : std::string_view(pair.data(), eq);
        if (!isVolatileParam(key)) {
            if (first) { out.push_back('?'); first = false; }
            else { out.push_back('&'); }
            out.append(pair);
        }
        start = end + 1;
    }
    return out;
}

ThumbnailLoader& ThumbnailLoader::get() {
    static ThumbnailLoader instance;
    return instance;
}

ThumbnailLoader::ThumbnailLoader() {
    // limite por plataforma para balancear throughput y uso de memoria
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_maxConcurrentTasks = 4;
    m_diskPool = std::make_unique<paimon::ThreadPool>(2, "PaimonDiskIO");
    m_cpuPool  = std::make_unique<paimon::ThreadPool>(2, "PaimonCPU");
#else
    // Desktop: escalar pool de CPU al numero de cores disponibles.
    // El decode de thumbnails (stb_image/ImagePlus) es CPU-bound puro,
    // asi que mas threads = throughput lineal hasta saturar decodes concurrentes.
    // Tope en 16 para evitar contention del scheduler y cache thrash.
    unsigned hw = std::thread::hardware_concurrency();
    int cpuThreads  = static_cast<int>(std::clamp<unsigned>(hw ? hw : 4u, 4u, 16u));
    int diskThreads = static_cast<int>(std::clamp<unsigned>(hw ? hw / 4u : 2u, 2u, 4u));
    // Aumentado a 6 para mejor throughput en desktop - el servidor CDN puede manejar
    // más conexiones concurrentes y el circuit breaker protege contra rate-limiting.
    m_maxConcurrentTasks = std::min(6, std::max(2, cpuThreads / 2));
    m_diskPool = std::make_unique<paimon::ThreadPool>(diskThreads, "PaimonDiskIO");
    m_cpuPool  = std::make_unique<paimon::ThreadPool>(cpuThreads,  "PaimonCPU");
#endif
    log::info("[ThumbnailLoader] constructor: maxConcurrent={}", m_maxConcurrentTasks);
    initDiskCache();
}

ThumbnailLoader::~ThumbnailLoader() {
    log::info("[ThumbnailLoader] destructor: shutting down");
    if (m_cleanupFinished.load(std::memory_order_acquire)) {
        log::info("[ThumbnailLoader] cleanup already completed during runtime shutdown");
        return;
    }
    // fallback si no se llamo cleanup() antes (e.g. pruebas)
    m_shuttingDown = true;
    waitBackgroundWorkers();

    if (paimon::cache::ThumbnailCache::isAlive()) {
        paimon::cache::ThumbnailCache::get().takeAllTextures();
    }

    // Limpiar tasks y sus callbacks ANTES de que la destruccion implicita de miembros
    // los destruya. Las callbacks capturan WeakRef<PaimonLevelCell> cuyo destructor
    // interactua con CCPoolManager — que puede ya estar muerto a estas alturas.
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.clear();
    }
    for (auto& [id, task] : m_tasks) {
        if (task) task->callbacks.clear();
    }
    m_tasks.clear();
    for (auto& [url, task] : m_urlTasks) {
        if (task) task->callbacks.clear();
    }
    m_urlTasks.clear();
    m_invalidationListeners.clear();
}

bool ThumbnailLoader::beginCleanup(char const* reason) {
    bool expected = false;
    if (!m_cleanupStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        log::info("[ThumbnailLoader] beginCleanup: already started ({})", reason);
        return false;
    }
    return true;
}

void ThumbnailLoader::logShutdownSnapshot(char const* reason) {
    auto& cache = paimon::cache::ThumbnailCache::get();
    log::info("[ThumbnailLoader] shutdown snapshot ({}): ram={} disk={} tasks={} urlTasks={} active={}",
        reason, cache.ramEntryCount(), cache.diskEntryCount(),
        m_tasks.size(), m_urlTasks.size(), m_activeTaskCount.load());
}

void ThumbnailLoader::initDiskCache() {
    // hilo de I/O de disco — no migrable a WebTask (no es peticion web)
    spawnDisk([this]() {
        // --- legacy migration: rename old "cache/" to quality dir if needed ---
        paimon::quality::migrateLegacyCache();

        auto path = paimon::quality::cacheDir();
        PaimonDebug::log("[ThumbnailLoader] iniciando cache de disco en: {}", geode::utils::string::pathToString(path));
        
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            std::filesystem::create_directories(path, ec);
            PaimonDebug::log("[ThumbnailLoader] carpeta de cache creada");
        }

        // compruebo si estamos en shutdown
        if (m_shuttingDown.load(std::memory_order_acquire)) {
            PaimonDebug::log("[ThumbnailLoader] shutdown durante init cache, abortando");
            return;
        }

        auto& cache = paimon::cache::ThumbnailCache::get();
        cache.loadDiskIndex();

        PaimonDebug::log("[ThumbnailLoader] cache de disco lista. entradas: {}",
            cache.diskEntryCount());
    });
}

// ── Callback batching ───────────────────────────────────────────────

void ThumbnailLoader::enqueuePendingCallback(LoadCallback cb, cocos2d::CCTexture2D* tex, bool success, int levelID) {
    int version = 0;
    if (levelID > 0) {
        version = paimon::cache::ThumbnailCache::get().getInvalidationVersion(levelID);
    }
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.push_back({std::move(cb), tex, success, levelID, version});
    }
    scheduleDrain();
}

void ThumbnailLoader::scheduleDrain() {
    bool expected = false;
    if (!m_drainScheduled.compare_exchange_strong(expected, true)) return;
    Loader::get()->queueInMainThread([this]() {
        drainPendingCallbacks();
    });
}

void ThumbnailLoader::drainPendingCallbacks() {
    m_drainScheduled.store(false, std::memory_order_release);
    if (m_shuttingDown.load(std::memory_order_acquire)) return;

    std::vector<PendingCallback> batch;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        int count = std::min(MAX_CALLBACKS_PER_FRAME, static_cast<int>(m_pendingCallbacks.size()));
        batch.assign(
            std::make_move_iterator(m_pendingCallbacks.begin()),
            std::make_move_iterator(m_pendingCallbacks.begin() + count)
        );
        m_pendingCallbacks.erase(m_pendingCallbacks.begin(), m_pendingCallbacks.begin() + count);
    }

    for (auto& pc : batch) {
        // Descartar callbacks cuya version de invalidacion cambio
        // (el thumbnail fue actualizado entre encolar y drenar)
        if (pc.levelID > 0 && pc.capturedVersion != paimon::cache::ThumbnailCache::get().getInvalidationVersion(pc.levelID)) {
            log::debug("[ThumbnailLoader] drainPendingCallbacks: discarding stale callback for level {} (version {} != {})",
                pc.levelID, pc.capturedVersion, paimon::cache::ThumbnailCache::get().getInvalidationVersion(pc.levelID));
            continue;
        }
        if (pc.callback) pc.callback(pc.texture, pc.success);
    }

    // si quedan mas, programar otro drain en el siguiente frame
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pendingCallbacks.empty()) {
            scheduleDrain();
        }
    }

    // Purgar texturas no usadas periodicamente (throttled a cada 2s internamente)
    paimon::cache::ThumbnailCache::get().purgeUnusedTextures();

    // Purgar entradas expiradas del failed cache para permitir reintentos
    // sin necesidad de que el usuario navegue a otra pagina
    paimon::cache::ThumbnailCache::get().purgeExpiredFailed();
}

// ── Texture upload batching ─────────────────────────────────────────

void ThumbnailLoader::enqueuePendingUpload(PendingUpload upload) {
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.push_back(std::move(upload));
    }
    scheduleUploadDrain();
}

void ThumbnailLoader::scheduleUploadDrain() {
    bool expected = false;
    if (!m_uploadDrainScheduled.compare_exchange_strong(expected, true)) return;
    Loader::get()->queueInMainThread([this]() {
        drainPendingUploads();
    });
}

void ThumbnailLoader::drainPendingUploads() {
    m_uploadDrainScheduled.store(false, std::memory_order_release);
    if (m_shuttingDown.load(std::memory_order_acquire)) return;

    std::vector<PendingUpload> batch;
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        // Priorizar uploads de celdas visibles (mayor priority) sobre prefetches
        // para que el usuario vea los thumbs mas rapido
        std::sort(m_pendingUploads.begin(), m_pendingUploads.end(),
            [](PendingUpload const& a, PendingUpload const& b) {
                int pa = a.task ? a.task->priority : 0;
                int pb = b.task ? b.task->priority : 0;
                return pa > pb; // mayor prioridad primero
            });
        // Tomamos hasta MAX_UPLOADS_PER_FRAME candidatos; el budget de tiempo
        // decide cuantos realmente subimos a GPU
        int count = std::min(MAX_UPLOADS_PER_FRAME, static_cast<int>(m_pendingUploads.size()));
        batch.assign(
            std::make_move_iterator(m_pendingUploads.begin()),
            std::make_move_iterator(m_pendingUploads.begin() + count)
        );
        m_pendingUploads.erase(m_pendingUploads.begin(), m_pendingUploads.begin() + count);
    }

    auto frameStart = std::chrono::steady_clock::now();
    int uploaded = 0;
    std::vector<PendingUpload> deferred; // los que no cupieron en el budget

    for (auto& pu : batch) {
        if (!pu.task || pu.task->cancelled) {
            finishTask(pu.task, nullptr, false);
            continue;
        }
        if (!pu.image && pu.pixels.empty()) {
            if (pu.fallbackToDownload) {
                workerDownload(pu.task);
            } else {
                finishTask(pu.task, nullptr, false);
            }
            continue;
        }

        // Presupuesto adaptativo: si ya consumimos demasiado tiempo del frame,
        // devolver los restantes a la cola para el proximo frame.
        // Siempre subimos al menos 1 (el de mayor prioridad) para garantizar progreso.
        // Bypass para celdas visibles: si la tarea es visible (>= PriorityVisibleCell),
        // se sube inmediatamente sin importar el budget — el usuario debe ver el thumb
        // de la celda bajo el cursor en el frame actual.
        bool isVisibleCell = pu.task && pu.task->priority >= PriorityVisibleCell;
        if (uploaded > 0 && !isVisibleCell) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameStart).count();
            if (elapsed >= UPLOAD_FRAME_BUDGET_US) {
                deferred.push_back(std::move(pu));
                continue;
            }
        }

        // Mode 1: CCImage from encoded data — initWithImage handles format conversion
        // Mode 2: Raw RGBA pixels from .rgb — initWithData with RGBA8888
        auto tex = new CCTexture2D();
        bool ok = false;

        if (pu.image) {
            ok = tex->initWithImage(pu.image);
            delete pu.image;
            pu.image = nullptr;
        } else if (!pu.pixels.empty() && pu.width > 0 && pu.height > 0) {
            ok = tex->initWithData(pu.pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
                                   pu.width, pu.height, CCSize((float)pu.width, (float)pu.height));
        }

        if (ok) {
            tex->autorelease();
            finishTask(pu.task, tex, true, pu.originalWidth, pu.originalHeight);
            uploaded++;
        } else {
            tex->release();
            if (pu.fallbackToDownload) {
                workerDownload(pu.task);
            } else {
                finishTask(pu.task, nullptr, false);
            }
        }
    }

    // Devolver uploads que no cupieron en el budget al frente de la cola
    if (!deferred.empty()) {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.insert(m_pendingUploads.begin(),
            std::make_move_iterator(deferred.begin()),
            std::make_move_iterator(deferred.end()));
    }

    if (uploaded > 0) {
        auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frameStart).count();
        log::debug("[ThumbnailLoader] GPU upload: {} textures in {}us (budget={}us, deferred={})",
            uploaded, totalUs, UPLOAD_FRAME_BUDGET_US, deferred.size());
    }

    // si quedan mas, programar otro drain
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        if (!m_pendingUploads.empty()) {
            scheduleUploadDrain();
        }
    }
}

void ThumbnailLoader::recordDownloadFailure() {
    std::lock_guard<std::mutex> lock(m_cooldownMutex);
    auto now = std::chrono::steady_clock::now();

    // si la ventana ya expiro, reiniciar el contador
    auto windowElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_failureWindowStart).count();
    if (windowElapsed >= FAILURE_WINDOW_SECONDS) {
        m_recentFailureCount.store(0, std::memory_order_relaxed);
        m_failureWindowStart = now;
    }

    int count = m_recentFailureCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count >= FAILURE_THRESHOLD) {
        m_globalCooldownUntil = now + std::chrono::seconds(COOLDOWN_SECONDS);
        m_recentFailureCount.store(0, std::memory_order_relaxed);
        m_failureWindowStart = now;
        log::warn("[ThumbnailLoader] {} fallos en {}s — cooldown global de {}s activado",
            count, FAILURE_WINDOW_SECONDS, COOLDOWN_SECONDS);
    }
}

bool ThumbnailLoader::isGlobalCooldownActive() const {
    // lectura sin lock para evitar contention en hot path
    auto now = std::chrono::steady_clock::now();
    return now < m_globalCooldownUntil;
}

void ThumbnailLoader::triggerBackgroundRevisionCheck(int levelID) {
    // Deprecated: per-level revision checks via /api/thumbnails/list are no
    // longer needed. Staleness detection is now handled by the manifest
    // revisionToken comparison in HttpClient::updateManifestFromJson().
    // This method is kept as a no-op for backward compatibility.
    (void)levelID;
}

void ThumbnailLoader::setMaxConcurrentTasks(int max) {
    // evitar sobrecargar memoria/CPU en movil
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_maxConcurrentTasks = std::max(1, std::min(24, max));
#else
    m_maxConcurrentTasks = std::max(1, std::min(64, max));
#endif
}

bool ThumbnailLoader::isLoaded(int levelID, bool isGif) const {
    return paimon::cache::ThumbnailCache::get().getFromRam(levelID, isGif).has_value();
}

bool ThumbnailLoader::isPending(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    return m_tasks.find(key) != m_tasks.end();
}

bool ThumbnailLoader::isFailed(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::string keyStr = paimon::cache::CacheKey::fromLegacy(key).toString();
    return paimon::cache::ThumbnailCache::get().isFailed(keyStr);
}

bool ThumbnailLoader::isNotFound(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::string keyStr = paimon::cache::CacheKey::fromLegacy(key).toString();
    return paimon::cache::ThumbnailCache::get().isNotFound(keyStr);
}

bool ThumbnailLoader::hasGIFData(int levelID) const {
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    return m_gifLevels.find(levelID) != m_gifLevels.end();
}

std::filesystem::path ThumbnailLoader::getCachePath(int levelID, bool isGif) {
    return paimon::quality::thumbCachePath(levelID, isGif);
}

void ThumbnailLoader::requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority, bool isGif, Quality quality) {
    int key = isGif ? -levelID : levelID;
    log::debug("[ThumbnailLoader] requestLoad: levelID={} key={} priority={} isGif={}", levelID, key, priority, isGif);

    auto& cache = paimon::cache::ThumbnailCache::get();

    // 1. reviso cache en RAM (sin tomar m_queueMutex — getFromRam usa su propio shared_mutex)
    auto ramTex = cache.getFromRam(levelID, isGif);
    if (ramTex.has_value()) {
        cache.stats().ramHits.fetch_add(1, std::memory_order_relaxed);

        // touch en disco
        cache.touchDiskAccess(std::abs(key), isGif);

        // fuerzo callback asincrono para no trabar la UI
        log::debug("[ThumbnailLoader] requestLoad: RAM cache hit for key={}", key);
        auto tex = ramTex.value();
        enqueuePendingCallback(callback, tex, true, std::abs(key));
        return;
    }
    cache.stats().ramMisses.fetch_add(1, std::memory_order_relaxed);

    // 2. miro el cache de "not found" (persistente por sesion — no se limpia al navegar)
    std::string keyStr = paimon::cache::CacheKey::fromLegacy(key).toString();
    if (cache.isNotFound(keyStr)) {
        log::debug("[ThumbnailLoader] requestLoad: not-found cache hit for key={}", key);
        Loader::get()->queueInMainThread([callback]() {
            if (callback) callback(nullptr, false);
        });
        return;
    }

    // 2.5. miro el cache de fallos transitorios (con backoff exponencial)
    if (cache.isFailed(keyStr)) {
        log::debug("[ThumbnailLoader] requestLoad: failed cache hit for key={}", key);
        Loader::get()->queueInMainThread([callback]() {
            if (callback) callback(nullptr, false);
        });
        return;
    }

    // lock protege m_tasks de carreras con finishTask
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);

    // 3. reviso si ya hay una tarea en cola
    auto taskIt = m_tasks.find(key);
    if (taskIt != m_tasks.end()) {
        // Si la tarea fue cancelada (celda salio y volvio), la reactivo
        // para que el callback nuevo reciba el resultado.
        if (taskIt->second->cancelled) {
            taskIt->second->cancelled = false;
            log::debug("[ThumbnailLoader] requestLoad: un-cancelling task for key={}", key);
            if (!taskIt->second->running) {
                m_priorityQueue.emplace(priority, key);
            }
        }
        log::debug("[ThumbnailLoader] requestLoad: task already queued for key={}, appending callback", key);
        // solo agrego el callback a la tarea existente
        if (callback) taskIt->second->callbacks.push_back(callback);
        // si viene con mas prioridad se la subo
        if (priority > taskIt->second->priority) {
            taskIt->second->priority = priority;
            if (!taskIt->second->running) {
                m_priorityQueue.emplace(priority, key);
            }
        }
        return;
    }

    // 3.5. cooldown global: si muchas descargas fallaron recientemente,
    //       rechazar solo prefetches de baja prioridad para dar respiro al servidor.
    //       Las celdas visibles (PriorityVisibleCell) pasan siempre para que
    //       el usuario no vea celdas vacias al scrollear.
    if (isGlobalCooldownActive() && priority < PriorityVisiblePrefetch) {
        log::debug("[ThumbnailLoader] requestLoad: global cooldown active, deferring key={}", key);
        if (callback) {
            Loader::get()->queueInMainThread([callback]() {
                callback(nullptr, false);
            });
        }
        return;
    }

    // 3.6. si la cola ya esta muy grande y la prioridad es baja (prefetch),
    //       rechazo para no saturar memoria con tareas pendientes
    constexpr int MAX_PENDING_LOW_PRIORITY = 80;
    int pendingCount = static_cast<int>(m_tasks.size()) - m_activeTaskCount.load(std::memory_order_relaxed);
    if (priority <= PriorityVisiblePrefetch && pendingCount >= MAX_PENDING_LOW_PRIORITY) {
        log::debug("[ThumbnailLoader] requestLoad: queue saturated ({} pending), dropping low-priority key={}", pendingCount, key);
        if (callback) {
            Loader::get()->queueInMainThread([callback]() {
                callback(nullptr, false);
            });
        }
        return;
    }

    // 4. creo una tarea nueva
    log::debug("[ThumbnailLoader] requestLoad: creating new task for key={} priority={}", key, priority);
    auto task = std::make_shared<Task>();
    task->levelID = key;
    task->fileName = fileName;
    task->priority = priority;
    if (callback) task->callbacks.push_back(callback);

    m_tasks[key] = task;
    m_priorityQueue.emplace(priority, key);
    
    processQueue();
}

void ThumbnailLoader::prefetchLevelAssets(int levelID, int priority) {
    if (levelID <= 0) {
        return;
    }

    // skip if already loaded, pending, or known-failed/not-found — avoids queue bloat
    if (isLoaded(levelID) || isPending(levelID) || isFailed(levelID) || isNotFound(levelID)) {
        return;
    }

    this->requestLoad(levelID, fmt::format("{}.png", levelID), nullptr, priority);
}

void ThumbnailLoader::prefetchLevels(std::vector<int> const& levelIDs, int priority) {
    if (levelIDs.empty()) {
        return;
    }

    // limitar prefetch para no inundar la cola ni saturar el servidor
    // niveles visibles (priority >= PriorityVisiblePrefetch) hasta 8,
    // predictivos (priority < PriorityVisiblePrefetch) hasta 4
    int maxPrefetch = (priority >= PriorityVisiblePrefetch) ? 8 : 4;

    std::unordered_set<int> seen;
    seen.reserve(levelIDs.size());
    std::vector<int> manifestIds;
    manifestIds.reserve(std::min(levelIDs.size(), static_cast<size_t>(maxPrefetch)));
    int queued = 0;

    for (int levelID : levelIDs) {
        if (queued >= maxPrefetch) break;
        if (levelID <= 0 || !seen.insert(levelID).second) {
            continue;
        }
        // No pedir manifest si ya esta cacheado — evita fetchManifest redundante
        if (!HttpClient::get().getManifestEntry(levelID).has_value()) {
            manifestIds.push_back(levelID);
        }
        this->prefetchLevelAssets(levelID, priority);
        ++queued;
    }

    // Batch manifest prefetch: populate CDN URLs for all queued levels in a single request.
    // This prevents each individual downloadThumbnail from doing manifest-miss → CDN probe → Worker fallback.
    // Skip if server is already rate-limiting manifest fetches to avoid making it worse.
    if (manifestIds.size() > 1) {
        // fire-and-forget: no importa el resultado, solo precargar URLs
        HttpClient::get().fetchManifest(manifestIds, [](bool) { });
    }
}

void ThumbnailLoader::cancelLoad(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    auto it = m_tasks.find(key);
    if (it != m_tasks.end()) {
        log::debug("[ThumbnailLoader] cancelLoad: key={}", key);
        it->second->cancelled = true;
        // si ya va corriendo no la paro, solo ignoro el resultado
        // si esta en cola con marcarla cancelada me sobra
    }
}

void ThumbnailLoader::processQueue() {
    // debe llamarse con m_queueMutex pillado
    if (m_shuttingDown.load(std::memory_order_acquire)) return;

    // modo batch: si esta activo no arranco nada nuevo si ya hay algo corriendo
    // asi termino el batch actual antes de empezar el siguiente
    if (m_batchMode && m_activeTaskCount > 0) {
        return;
    }

    while (m_activeTaskCount < m_maxConcurrentTasks && !m_priorityQueue.empty()) {
        // Purgar entradas invalidas del frente de la cola:
        // huerfanas (ya eliminadas de m_tasks), ya-corriendo, o duplicados
        auto topIt = m_priorityQueue.begin();
        int levelID = topIt->second;
        int entryPriority = topIt->first;
        auto taskIt = m_tasks.find(levelID);

        if (taskIt == m_tasks.end() || !taskIt->second || taskIt->second->running) {
            m_priorityQueue.erase(topIt);
            continue;
        }

        // Descartar entrada duplicada de prioridad obsoleta:
        // si la task tiene prioridad mayor que esta entrada, es un duplicado viejo
        if (entryPriority < taskIt->second->priority) {
            m_priorityQueue.erase(topIt);
            continue;
        }

        // Preferir tareas no-canceladas. Si la cabeza está cancelada,
        // buscar si hay alguna no-cancelada justo detras con la misma prioridad.
        // Si no, usar la cancelada (para poblar disk cache).
        if (taskIt->second->cancelled) {
            // Scan limitado: solo revisar las siguientes entradas con misma prioridad
            bool foundNonCancelled = false;
            auto searchIt = std::next(topIt);
            while (searchIt != m_priorityQueue.end() && searchIt->first == entryPriority) {
                auto sTaskIt = m_tasks.find(searchIt->second);
                if (sTaskIt != m_tasks.end() && sTaskIt->second && !sTaskIt->second->running && !sTaskIt->second->cancelled) {
                    // Encontramos una no-cancelada con la misma prioridad — usar esa
                    levelID = searchIt->second;
                    taskIt = sTaskIt;
                    m_priorityQueue.erase(searchIt);
                    foundNonCancelled = true;
                    break;
                }
                ++searchIt;
            }
            if (!foundNonCancelled) {
                // Usar la cancelada para poblar disk cache
                m_priorityQueue.erase(topIt);
            }
        } else {
            m_priorityQueue.erase(topIt);
        }

        startTask(taskIt->second);
    }
}

void ThumbnailLoader::startTask(std::shared_ptr<Task> task) {
    task->running = true;
    task->startedAt = std::chrono::steady_clock::now();
    m_activeTaskCount.fetch_add(1, std::memory_order_relaxed);
    log::debug("[ThumbnailLoader] startTask: key={} active={}", task->levelID, m_activeTaskCount.load());

    // siempre tiro de disco primero para evitar carreras con initDiskCache
    // workerLoadFromDisk mira el FS y si no encuentra descarga
    // hilo de I/O de disco + decodificacion CPU — no migrable a WebTask
    spawnDisk([this, task]() {
        if (m_shuttingDown.load(std::memory_order_acquire)) {
            Loader::get()->queueInMainThread([this, task]() {
                finishTask(task, nullptr, false);
            });
            return;
        }
        workerLoadFromDisk(task);
    });
}

void ThumbnailLoader::workerLoadFromDisk(std::shared_ptr<Task> task) {
    log::debug("[ThumbnailLoader] workerLoadFromDisk: key={} cancelled={}", task->levelID, task->cancelled);

    // Early cancellation: si la celda ya no esta visible y el disk cache esta
    // habilitado, no vale la pena hacer I/O solo para un touch de disco.
    // El thumbnail seguira en disco para el proximo request.
    if (task->cancelled && paimon::settings::general::enableDiskCache()) {
        Loader::get()->queueInMainThread([this, task]() {
            finishTask(task, nullptr, false);
        });
        return;
    }

    // Si el disk cache esta deshabilitado, ir directo a descarga
    if (!paimon::settings::general::enableDiskCache()) {
        workerDownload(task);
        return;
    }

    // No bailing on cancelled tasks: si hay disk hit se termina rapido,
    // si hay disk miss se sigue a workerDownload para poblar el cache.

    bool isGif = task->levelID < 0;
    int realID = std::abs(task->levelID);
    auto& cache = paimon::cache::ThumbnailCache::get();

    // ── Paso 1: buscar en disk index / filesystem ───────────────────
    std::filesystem::path diskPath;
    bool wasInManifest = false;
    {
        // buscar en el indice: primero el formato solicitado, luego el alternativo
        bool inIndex = cache.hasDiskEntry(realID, isGif);
        if (inIndex) {
            diskPath = getCachePath(realID, isGif);
            wasInManifest = true;
        } else if (!isGif && cache.hasDiskEntry(realID, true)) {
            diskPath = getCachePath(realID, true);
            wasInManifest = true;
        }

        // si el indice no lo tiene, verificar FS (race con initDiskCache)
        if (diskPath.empty()) {
            std::error_code ec;
            auto tryPath = getCachePath(realID, isGif);
            if (std::filesystem::exists(tryPath, ec)) {
                diskPath = tryPath;
            } else if (!isGif) {
                auto gifPath = getCachePath(realID, true);
                if (std::filesystem::exists(gifPath, ec)) {
                    diskPath = gifPath;
                }
            }
        }
    }

    // leer bytes del disco si encontramos un path
    // En desktop (SSD comun), permitir lecturas paralelas para mayor throughput.
    // En movil mantener serializado para evitar I/O thrash en almacenamiento lento.
    std::vector<uint8_t> data;
    if (!diskPath.empty()) {
        auto readStart = std::chrono::steady_clock::now();
        {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
            std::lock_guard<std::mutex> diskLock(m_diskReadMutex);
#endif
            std::error_code fsEc;
            auto fileSize = std::filesystem::file_size(diskPath, fsEc);
            if (!fsEc && fileSize > 0 && fileSize <= 64 * 1024 * 1024) {
                std::ifstream file(diskPath, std::ios::binary);
                if (file.is_open()) {
                    data.resize(static_cast<size_t>(fileSize));
                    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
                    if (!file) data.clear();
                }
            }
        }
        auto readEnd = std::chrono::steady_clock::now();
        auto readUs = std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart).count();
        cache.stats().diskReadTimeUsTotal.fetch_add(static_cast<uint64_t>(readUs), std::memory_order_relaxed);
        if (!data.empty()) {
            log::debug("[ThumbnailLoader] disk read: {} bytes in {}us for level {}", data.size(), readUs, realID);
        }
    }

    // ── Paso 2: fallback a LocalThumbs si disco fallo ───────────────
    bool fromLocalRgb = false;
    std::vector<uint8_t> localPixels;
    int localW = 0, localH = 0;

    if (data.empty()) {
        auto localResult = LocalThumbs::get().loadAsRGBA(realID);
        if (!localResult.pixels.empty()) {
            PaimonDebug::log("[ThumbnailLoader] fallback LocalThumbs pal nivel {}", realID);
            if (localResult.isRgb) {
                // .rgb comes as RGB888 raw — GPU will convert to RGBA on upload
                fromLocalRgb = true;
                localPixels = std::move(localResult.pixels);
                localW = localResult.width;
                localH = localResult.height;
            } else {
                // archivo estandar (png/jpg/webp) — bytes crudos para decode
                data = std::move(localResult.pixels);
            }
        }
    }

    // ── Paso 2.5: .rgb directo a upload (no necesita decode) ────────
    if (fromLocalRgb && !localPixels.empty()) {
        // Convert RGB→RGBA on the worker thread
        size_t pixelCount = static_cast<size_t>(localW) * localH;
        std::vector<uint8_t> rgbaBuf(pixelCount * 4);
        ImageConverter::rgbToRgbaFast(localPixels.data(), rgbaBuf.data(), pixelCount);

        // Extract dominant colors from RGBA data
        if (!LevelColors::get().getPair(realID)) {
            LevelColors::get().extractFromRawData(realID, rgbaBuf.data(), localW, localH, true);
        }

        // Downsample on worker thread
        int origW = localW, origH = localH;
        int imgW = localW, imgH = localH;
        if (localW > RAM_CACHE_MAX_DIM || localH > RAM_CACHE_MAX_DIM) {
            auto ds = ImageLoadHelper::downsampleForCache(rgbaBuf.data(), localW, localH, RAM_CACHE_MAX_DIM);
            if (!ds.pixels.empty() && ds.width > 0 && ds.height > 0) {
                rgbaBuf = std::move(ds.pixels);
                imgW = ds.width;
                imgH = ds.height;
            }
        }

        // Mode 2: raw RGBA pixels for initWithData upload
        enqueuePendingUpload({task, nullptr, std::move(rgbaBuf), imgW, imgH, realID,
            true/*fallbackToDownload*/, origW, origH});
        return;
    }

    // ── Paso 3: si no hay datos de ningun lado, descargar ───────────
    if (data.empty()) {
        cache.stats().diskMisses.fetch_add(1, std::memory_order_relaxed);
        workerDownload(task);
        return;
    }

    // ── Paso 4: tenemos bytes — actualizar disk index ───────────────
    cache.stats().diskHits.fetch_add(1, std::memory_order_relaxed);
    {
        bool actualIsGif = (!diskPath.empty() &&
            geode::utils::string::toLower(
                geode::utils::string::pathToString(diskPath.extension())) == ".gif");
        if (!wasInManifest) {
            paimon::cache::ThumbnailCache::DiskEntry de;
            de.levelID = realID;
            de.format = actualIsGif ? "gif" : "png";
            de.byteSize = data.size();
            de.isGif = actualIsGif;
            de.touchAccess();
            de.touchValidated();
            cache.upsertDisk(std::move(de));
            log::debug("[ThumbnailLoader] workerLoadFromDisk: upserted FS-fallback entry for {} ({})",
                realID, actualIsGif ? "gif" : "png");
        } else {
            cache.touchDiskAccess(realID, actualIsGif);
        }
    }

    // ── Paso 5: si cancelada, solo necesitabamos el cache de disco ──
    if (task->cancelled) {
        log::debug("[ThumbnailLoader] workerLoadFromDisk: disk hit + cancelled — skip decode for {}", realID);
        Loader::get()->queueInMainThread([this, task]() {
            finishTask(task, nullptr, false);
        });
        return;
    }

    // ── Paso 6: decodificar en CPU pool (libera el disk pool para mas I/O) ─
    spawnCpu([this, task, data = std::move(data), realID, isGif, diskPath]() {
        auto decoded = decodeImageData(data, realID, RAM_CACHE_MAX_DIM);

        if (decoded.success && decoded.image) {
            if (decoded.isGif) {
                { std::lock_guard<std::recursive_mutex> lock(m_queueMutex); m_gifLevels.insert(realID); }
            }
            enqueuePendingUpload({task, decoded.image, {}, 0, 0, realID,
                true/*fallbackToDownload*/, decoded.width, decoded.height});
        } else {
            PaimonDebug::warn("[ThumbnailLoader] fallo decode pal nivel {} — purgando entrada corrupta del disco", realID);
            // Purgar archivo corrupto para que el siguiente intento no lo vuelva a leer
            spawnDisk([this, task, realID, isGif, diskPath]() {
                std::error_code ec;
                if (!diskPath.empty()) std::filesystem::remove(diskPath, ec);
                auto& cache = paimon::cache::ThumbnailCache::get();
                cache.removeDisk(realID, isGif);
                if (!isGif) cache.removeDisk(realID, true);
                workerDownload(task);
            });
        }
    });
}

void ThumbnailLoader::workerDownload(std::shared_ptr<Task> task) {
    // No bail on cancelled: siempre descargar para poblar el disk cache.
    // El callback ya maneja cancelled → guarda en disco, salta decode/upload.

    int realID = std::abs(task->levelID);
    bool isGif = task->levelID < 0;
    log::info("[ThumbnailLoader] workerDownload: levelID={} isGif={} cancelled={}", realID, isGif, task->cancelled);
    paimon::cache::ThumbnailCache::get().stats().downloads.fetch_add(1, std::memory_order_relaxed);

    auto retryCount = std::make_shared<std::atomic<int>>(0);
    static constexpr int MAX_DOWNLOAD_RETRIES = 1;

    auto self = this;

    auto onDownloadResult = [self, task, realID, isGif, retryCount](bool success, std::vector<uint8_t> const& data, int, int) {
        if (self->m_shuttingDown.load(std::memory_order_acquire)) {
            log::info("[ThumbnailLoader] workerDownload callback ignored during shutdown");
            self->finishTask(task, nullptr, false);
            return;
        }

        if (success && !data.empty()) {
            self->processDownloadedData(task, data, realID);
            return;
        }

        // Descarga fallida — reintentar 1 vez antes de marcar como fallo,
        // pero NO reintentar si el global cooldown está activo (servidor ya dijo "basta")
        int attempt = retryCount->fetch_add(1);
        if (attempt < MAX_DOWNLOAD_RETRIES && !self->isGlobalCooldownActive()) {
            log::warn("[ThumbnailLoader] workerDownload: download failed for level {} (attempt {}), retrying...", realID, attempt + 1);
            paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);

            // Reintentar
            HttpClient::get().downloadThumbnail(realID, isGif,
                [self, task, realID](bool retrySuccess, std::vector<uint8_t> const& retryData, int, int) {
                    if (self->m_shuttingDown.load(std::memory_order_acquire)) {
                        self->finishTask(task, nullptr, false);
                        return;
                    }
                    if (retrySuccess && !retryData.empty()) {
                        self->processDownloadedData(task, retryData, realID);
                    } else {
                        log::warn("[ThumbnailLoader] workerDownload: retry also failed for level {}", realID);
                        paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                        self->finishTask(task, nullptr, false);
                    }
                }
            );
        } else {
            log::warn("[ThumbnailLoader] workerDownload: download failed for level {} after retries", realID);
            paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
            self->finishTask(task, nullptr, false);
        }
    };

    // Lanzar descarga directamente — WebHelper::dispatch delega a geode::async::spawn,
    // que es thread-safe y maneja el callback en el main thread internamente.
    // Evitamos el delay de ~8-16ms de queueInMainThread.
    HttpClient::get().downloadThumbnail(realID, isGif, onDownloadResult);
}

// ── processDownloadedData: logica compartida entre primer intento y retry ──
void ThumbnailLoader::processDownloadedData(std::shared_ptr<Task> task, std::vector<uint8_t> data, int realID) {
    bool taskCancelled = task->cancelled;
    auto self = this;

    spawnDisk([self, task, data = std::move(data), realID, taskCancelled]() {
        // 1. guardar en disco con nombre segun formato real
        bool dataIsGif = imgp::formats::isGif(data.data(), data.size());
        bool diskCacheEnabled = paimon::settings::general::enableDiskCache();

        if (diskCacheEnabled) {
            auto path = self->getCachePath(realID, dataIsGif);
            std::error_code dirEc;
            std::filesystem::create_directories(path.parent_path(), dirEc);
            if (dirEc) {
                log::error("[ThumbnailLoader] create_directories fallo para {}: {}",
                    geode::utils::string::pathToString(path.parent_path()), dirEc.message());
            }

            // escritura atomica: tmp → rename para evitar
            // archivos corruptos si el proceso muere a mitad de I/O
            auto tmpPath = path;
            tmpPath += ".tmp";
            bool writeOk = false;
            {
                std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
                if (file) {
                    file.write(reinterpret_cast<char const*>(data.data()), data.size());
                    writeOk = file.good();
                }
            }
            if (writeOk) {
                std::error_code renameEc;
                std::filesystem::remove(path, renameEc);
                renameEc.clear();
                std::filesystem::rename(tmpPath, path, renameEc);
                if (renameEc) {
                    log::error("[ThumbnailLoader] rename fallo {}: {}",
                        geode::utils::string::pathToString(tmpPath), renameEc.message());
                    std::filesystem::remove(tmpPath, renameEc);
                } else {
                    log::debug("[ThumbnailLoader] guardado en disco: {} ({} bytes)",
                        geode::utils::string::pathToString(path), data.size());

                    auto& cache = paimon::cache::ThumbnailCache::get();
                    paimon::cache::ThumbnailCache::DiskEntry de;
                    de.levelID = realID;
                    de.format = dataIsGif ? "gif" : "png";
                    de.byteSize = data.size();
                    de.isGif = dataIsGif;
                    de.touchAccess();
                    de.touchValidated();
                    cache.upsertDisk(std::move(de));
                }
            } else {
                log::error("[ThumbnailLoader] no se pudo escribir archivo temporal: {}",
                    geode::utils::string::pathToString(tmpPath));
                std::error_code rmEc;
                std::filesystem::remove(tmpPath, rmEc);
            }

            paimon::cache::ThumbnailCache::get().evictDiskIfNeeded(
                paimon::settings::quality::diskCacheBytes(),
                std::chrono::hours(24 * 30));

            // guardar indice de disco periodicamente (debounce 10s)
            {
                static std::atomic<int64_t> s_lastDiskSave{0};
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                auto last = s_lastDiskSave.load(std::memory_order_relaxed);
                if (now - last >= 10) {
                    if (s_lastDiskSave.compare_exchange_strong(last, now, std::memory_order_acq_rel)) {
                        paimon::cache::ThumbnailCache::get().saveDiskIndex();
                        geode::Loader::get()->queueInMainThread([]() {
                            (void)geode::Mod::get()->saveData();
                        });
                    }
                }
            }
        }

        // si la task fue cancelada, solo persistimos en disco
        if (taskCancelled || task->cancelled) {
            log::debug("[ThumbnailLoader] processDownloadedData: task cancelada para {} — disco guardado, skip decode/upload", realID);
            Loader::get()->queueInMainThread([self, task]() {
                self->finishTask(task, nullptr, false);
            });
            return;
        }

        // 2. decodifico en CPU pool (libera disk pool para mas I/O)
        self->spawnCpu([self, task, data, realID]() {
            auto decoded = self->decodeImageData(data, realID, RAM_CACHE_MAX_DIM);

            if (decoded.success && decoded.image) {
                if (decoded.isGif) {
                    { std::lock_guard<std::recursive_mutex> lock(self->m_queueMutex); self->m_gifLevels.insert(realID); }
                }
                // actualizar disk index con dimensiones decodificadas
                {
                    auto& cache = paimon::cache::ThumbnailCache::get();
                    auto existingEntry = cache.getDiskEntry(realID, decoded.isGif);
                    if (existingEntry.has_value()) {
                        auto updated = existingEntry.value();
                        updated.width = decoded.width;
                        updated.height = decoded.height;
                        cache.upsertDisk(std::move(updated));
                    }
                }

                self->enqueuePendingUpload({task, decoded.image, {}, 0, 0, realID,
                    false/*fallbackToDownload*/, decoded.width, decoded.height});
            } else {
                Loader::get()->queueInMainThread([self, task]() {
                    self->finishTask(task, nullptr, false);
                });
            }
        });
    });
}

void ThumbnailLoader::finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success, int origW, int origH) {
    // pipeline timing: cuanto tardo desde startTask hasta aqui
    if (task->startedAt.time_since_epoch().count() > 0) {
        auto pipelineUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - task->startedAt).count();
        log::info("[ThumbnailLoader] finishTask: key={} success={} cancelled={} hasTex={} pipeline={}us",
            task->levelID, success, task->cancelled, texture != nullptr, pipelineUs);
    } else {
        log::info("[ThumbnailLoader] finishTask: key={} url={} success={} cancelled={} hasTex={}",
            task->levelID, task->url, success, task->cancelled, texture != nullptr);
    }
    std::vector<LoadCallback> callbacks;
    bool shuttingDown = m_shuttingDown.load(std::memory_order_acquire);
    bool shouldNotify = !task->cancelled && !shuttingDown;

    auto& cache = paimon::cache::ThumbnailCache::get();

    // main thread: lock protege m_tasks
    {
        std::lock_guard<std::recursive_mutex> lock(m_queueMutex);

        if (task->isUrlTask) {
            // URL-based task (gallery shared cache) — usa pool separado
            if (!shuttingDown && success && texture) {
                cache.addUrlToRam(task->urlCacheKey, texture);
            } else if (!shuttingDown && !task->cancelled) {
                cache.markFailed("url_" + task->urlCacheKey);
            }
            if (shouldNotify) callbacks = task->callbacks;
            m_urlTasks.erase(task->urlCacheKey);

            m_activeUrlTaskCount.fetch_sub(1, std::memory_order_relaxed);

            // procesar cola de URL pendientes
            if (!shuttingDown) {
                processUrlQueue();
            }
        } else {
            // level-based task
            if (!shuttingDown && success && texture) {
                bool gf = task->levelID < 0;
                int rid = std::abs(task->levelID);
                cache.addToRam(rid, gf, texture, -1, origW, origH);
            } else if (!shuttingDown && !task->cancelled) {
                std::string keyStr = paimon::cache::CacheKey::fromLegacy(task->levelID).toString();
                if (task->wasNotFound) {
                    // Server confirmed 404 — thumbnail doesn't exist. Mark permanent
                    // so we don't keep requesting it until the user refreshes.
                    cache.markNotFound(keyStr);
                } else {
                    // Transient failure (timeout, rate limit, decode error, etc.)
                    // Use backoff (2s→4s→8s→15s) so the thumbnail is retried soon.
                    cache.markFailed(keyStr);
                }
                // registrar fallo para el cooldown global
                recordDownloadFailure();
            }
            if (shouldNotify) callbacks = task->callbacks;
            m_tasks.erase(task->levelID);

            m_activeTaskCount.fetch_sub(1, std::memory_order_relaxed);

            // proceso el siguiente level task solo si seguimos vivos
            if (!shuttingDown) {
                processQueue();
            }
        }
    }

    // callbacks fuera del lock — encolados para drenar max N por frame
    if (shouldNotify) {
        int realID = std::abs(task->levelID);
        for (auto& cb : callbacks) {
            if (cb) enqueuePendingCallback(std::move(cb), texture, success, realID);
        }
    }
}

void ThumbnailLoader::clearCache() {
    log::info("[ThumbnailLoader] clearCache: clearing RAM cache");
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    paimon::cache::ThumbnailCache::get().clearRam();
    m_gifLevels.clear();
}

void ThumbnailLoader::clearFailedCache() {
    log::info("[ThumbnailLoader] clearFailedCache: clearing all failed entries");
    paimon::cache::ThumbnailCache::get().clearAllFailed();
    // tambien resetear el cooldown global para permitir reintentos inmediatos
    {
        std::lock_guard<std::mutex> lock(m_cooldownMutex);
        m_recentFailureCount.store(0, std::memory_order_relaxed);
        m_globalCooldownUntil = std::chrono::steady_clock::time_point{};
    }
}

void ThumbnailLoader::invalidateLevel(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    log::info("[ThumbnailLoader] invalidateLevel: levelID={} key={}", levelID, key);

    auto& cache = paimon::cache::ThumbnailCache::get();
    std::vector<InvalidationCallback> listeners;

    {
        std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
        // incremento la version de invalidacion para que los consumidores sepan que hay cambio
        cache.incrementInvalidation(levelID);

        // quito AMBOS formatos de la RAM (PNG y GIF)
        cache.removeFromRam(levelID, false);
        cache.removeFromRam(levelID, true);

        // quito AMBOS del cache de fallos
        std::string pngKey = paimon::cache::CacheKey::fromLegacy(levelID).toString();
        std::string gifKey = paimon::cache::CacheKey::fromLegacy(-levelID).toString();
        cache.clearFailed(pngKey);
        cache.clearFailed(gifKey);
        // quito AMBOS del cache de "not found" para permitir reintento
        cache.clearNotFound(pngKey);
        cache.clearNotFound(gifKey);
        m_gifLevels.erase(levelID);

        // permitir que el proximo RAM hit vuelva a verificar revision con el servidor
        m_revisionCheckedThisSession.erase(levelID);

        listeners.reserve(m_invalidationListeners.size());
        for (auto const& [_, cb] : m_invalidationListeners) {
            if (cb) listeners.push_back(cb);
        }
    }

    // limpiar manifest cache para que la proxima descarga use datos frescos del servidor
    HttpClient::get().removeManifestEntry(levelID);

    // limpiar exists cache para que la proxima consulta vuelva a preguntar al servidor
    HttpClient::get().removeExistsEntry(levelID);

    // limpiar gallery metadata cache
    ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);

    // limpiar video cache para este nivel (temp files + download requests)
    VideoThumbnailSprite::removeForLevel(levelID);

    if (!listeners.empty()) {
        Loader::get()->queueInMainThread([listeners = std::move(listeners), levelID]() mutable {
            for (auto& cb : listeners) {
                if (cb) cb(levelID);
            }
        });
    }

    // limpiar AnimatedGIFSprite RAM cache para este nivel
    {
        auto gifPathStr = geode::utils::string::pathToString(getCachePath(levelID, true));
        AnimatedGIFSprite::remove(gifPathStr);
    }

    // borro ambos formatos en disco para no dejar huerfanos (solo si disk cache habilitado)
    if (paimon::settings::general::enableDiskCache()) {
        // hilo de I/O de disco - no migrable a WebTask
        spawnDisk([levelID, this]() {
            std::error_code ec;
            auto pngPath = getCachePath(levelID, false);
            auto gifPath = getCachePath(levelID, true);
            if (std::filesystem::exists(pngPath, ec)) {
                std::filesystem::remove(pngPath, ec);
            }
            if (std::filesystem::exists(gifPath, ec)) {
                std::filesystem::remove(gifPath, ec);
            }
            // limpiar AnimatedGIFSprite disk cache (archivo .bin en gifs/)
            auto gifPathStr = geode::utils::string::pathToString(gifPath);
            auto gifDiskCachePath = AnimatedGIFSprite::getCachePath(gifPathStr);
            std::filesystem::remove(gifDiskCachePath, ec);

            // actualizar disk index
            auto& cache = paimon::cache::ThumbnailCache::get();
            cache.removeDisk(levelID, false);
            cache.removeDisk(levelID, true);
        });
    }
}

int ThumbnailLoader::getInvalidationVersion(int levelID) const {
    return paimon::cache::ThumbnailCache::get().getInvalidationVersion(levelID);
}

int ThumbnailLoader::addInvalidationListener(InvalidationCallback callback) {
    if (!callback) return 0;
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    int id = m_nextInvalidationListenerId++;
    m_invalidationListeners[id] = std::move(callback);
    return id;
}

void ThumbnailLoader::removeInvalidationListener(int listenerId) {
    if (listenerId <= 0) return;
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    m_invalidationListeners.erase(listenerId);
}

void ThumbnailLoader::clearDiskCache() {
    log::info("[ThumbnailLoader] clearDiskCache: clearing disk cache");
    // hilo de I/O de disco — no migrable a WebTask
    spawnDisk([this]() {
        std::error_code ec;
        std::filesystem::remove_all(paimon::quality::cacheDir(), ec);
        if (ec) {
            log::error("[ThumbnailLoader] error limpiando cache de disco: {}", ec.message());
        }
        paimon::cache::ThumbnailCache::get().clearDisk();
        initDiskCache(); // vuelvo a crear la carpeta
    });
}

void ThumbnailLoader::clearPendingQueue() {
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    for (auto& [id, task] : m_tasks) {
        task->cancelled = true;
    }
    // no limpio el mapa aqui porque algunas siguen corriendo
    // con marcarlas canceladas me vale
}

void ThumbnailLoader::updateSessionCache(int levelID, cocos2d::CCTexture2D* texture) {
    if (!texture) return;
    bool isGif = levelID < 0;
    int realID = std::abs(levelID);
    paimon::cache::ThumbnailCache::get().addToRam(realID, isGif, texture);
}

void ThumbnailLoader::cleanup() {
    if (!beginCleanup("cleanup")) {
        return;
    }
    logShutdownSnapshot("cleanup begin");

    log::info("[ThumbnailLoader] cleanup: shutting down loader");
    m_shuttingDown.store(true, std::memory_order_release);
    clearPendingQueue();
    waitBackgroundWorkers();

    // Drenar pendientes sin ejecutarlos (shutdown)
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingCallbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_uploadMutex);
        m_pendingUploads.clear();
    }

    // Limpiar invalidation listeners ANTES de la destruccion estatica.
    // Los listeners capturan WeakRef<PaimonLevelCell> cuyo destructor
    // interactua con CCPoolManager — si se destruyen en el ~ThumbnailLoader
    // (destruccion estatica del DLL), CCPoolManager ya murio → crash.
    {
        std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
        m_invalidationListeners.clear();

        // Limpiar callbacks de tasks que capturan WeakRef<PaimonLevelCell>.
        // Despues de waitBackgroundWorkers() no hay threads activos,
        // asi que podemos limpiar las callbacks y los mapas de forma segura.
        for (auto& [id, task] : m_tasks) {
            if (task) task->callbacks.clear();
        }
        m_tasks.clear();
        for (auto& [url, task] : m_urlTasks) {
            if (task) task->callbacks.clear();
        }
        m_urlTasks.clear();
    }

    clearCache();

    logShutdownSnapshot("cleanup end");
    m_cleanupFinished.store(true, std::memory_order_release);
}

void ThumbnailLoader::spawnDisk(std::function<void()> job) {
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    if (m_diskPool) m_diskPool->enqueue(std::move(job));
}

void ThumbnailLoader::spawnCpu(std::function<void()> job) {
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    if (m_cpuPool) m_cpuPool->enqueue(std::move(job));
}

void ThumbnailLoader::waitBackgroundWorkers() {
    // Apagar ambos pools — bloquea hasta que terminen todas las tareas encoladas
    if (m_diskPool) m_diskPool->shutdown();
    if (m_cpuPool) m_cpuPool->shutdown();
}

bool ThumbnailLoader::isTextureSane(cocos2d::CCTexture2D* tex) {
    if (!tex) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(tex);
    if (addr < 0x10000) return false; // puntero nulo o muy bajo, no es valido

    auto sz = tex->getContentSize();
    if (std::isnan(sz.width) || std::isnan(sz.height)) return false;
    return sz.width > 0.f && sz.height > 0.f;
}

// ── Shared URL thumbnail cache (gallery) ────────────────────────────

void ThumbnailLoader::requestUrlLoad(std::string const& url, LoadCallback callback, int priority) {
    if (url.empty()) {
        if (callback) callback(nullptr, false);
        return;
    }

    auto& cache = paimon::cache::ThumbnailCache::get();
    std::string cacheKey = normalizeUrlKey(url);

    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);

    // 1. reviso cache RAM de URLs (con key normalizada para compartir entre _pv variants)
    auto urlTex = cache.getUrlFromRam(cacheKey);
    if (urlTex.has_value()) {
        cache.stats().ramHits.fetch_add(1, std::memory_order_relaxed);
        auto tex = urlTex.value();
        Loader::get()->queueInMainThread([callback, tex]() {
            if (callback) callback(tex, true);
        });
        return;
    }
    cache.stats().ramMisses.fetch_add(1, std::memory_order_relaxed);

    // 2. cache de fallos (tambien normalizada)
    std::string failKey = "url_" + cacheKey;
    if (cache.isFailed(failKey)) {
        Loader::get()->queueInMainThread([callback]() {
            if (callback) callback(nullptr, false);
        });
        return;
    }

    // 3. tarea existente (deduplicacion por key normalizada).
    // LevelInfoLayer puede pedir primero /thumb.png y luego /thumb.png?_pv=id;
    // ambas deben compartir el mismo download/decode si apuntan al mismo asset.
    auto taskIt = m_urlTasks.find(cacheKey);
    if (taskIt != m_urlTasks.end()) {
        if (callback) taskIt->second->callbacks.push_back(callback);
        if (priority > taskIt->second->priority) {
            taskIt->second->priority = priority;
        }
        return;
    }

    // 4. nueva tarea URL
    auto task = std::make_shared<Task>();
    task->levelID = 0;
    task->url = url;
    task->urlCacheKey = cacheKey;
    task->priority = priority;
    task->isUrlTask = true;
    if (callback) task->callbacks.push_back(callback);

    m_urlTasks[cacheKey] = task;

    // arranco directamente con pool separado de URLs (no compite con level tasks)
    if (m_activeUrlTaskCount < m_maxConcurrentUrlTasks) {
        task->running = true;
        m_activeUrlTaskCount.fetch_add(1, std::memory_order_relaxed);
        spawnDisk([this, task]() {
            if (m_shuttingDown.load(std::memory_order_acquire)) {
                Loader::get()->queueInMainThread([this, task]() {
                    finishTask(task, nullptr, false);
                });
                return;
            }
            workerUrlDownload(task);
        });
    }
    // else: queda encolada en m_urlTasks, processUrlQueue la arrancara cuando haya slot
}

void ThumbnailLoader::processUrlQueue() {
    // must be called with m_queueMutex held
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    while (m_activeUrlTaskCount < m_maxConcurrentUrlTasks) {
        std::shared_ptr<Task> bestTask = nullptr;
        for (auto& [_, task] : m_urlTasks) {
            if (!task || task->running || task->cancelled) continue;
            if (!bestTask || task->priority > bestTask->priority) {
                bestTask = task;
            }
        }
        if (!bestTask) break;

        bestTask->running = true;
        m_activeUrlTaskCount.fetch_add(1, std::memory_order_relaxed);
        spawnDisk([this, task = bestTask]() {
            if (m_shuttingDown.load(std::memory_order_acquire)) {
                Loader::get()->queueInMainThread([this, task]() {
                    finishTask(task, nullptr, false);
                });
                return;
            }
            workerUrlDownload(task);
        });
    }
}

void ThumbnailLoader::requestUrlBatchLoad(std::vector<std::string> const& urls, LoadCallback perUrlCallback, int priority) {
    for (auto const& url : urls) {
        requestUrlLoad(url, perUrlCallback, priority);
    }
}

bool ThumbnailLoader::isUrlLoaded(std::string const& url) const {
    return paimon::cache::ThumbnailCache::get().getUrlFromRam(normalizeUrlKey(url)).has_value();
}

void ThumbnailLoader::cancelUrlLoad(std::string const& url) {
    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    auto it = m_urlTasks.find(normalizeUrlKey(url));
    if (it != m_urlTasks.end()) {
        it->second->cancelled = true;
    }
}

void ThumbnailLoader::workerUrlDownload(std::shared_ptr<Task> task) {
    if (task->cancelled || m_shuttingDown.load(std::memory_order_acquire)) {
        Loader::get()->queueInMainThread([this, task]() { finishTask(task, nullptr, false); });
        return;
    }

    std::string url = task->url;
    paimon::cache::ThumbnailCache::get().stats().downloads.fetch_add(1, std::memory_order_relaxed);

    auto retryCount = std::make_shared<std::atomic<int>>(0);
    static constexpr int MAX_URL_DOWNLOAD_RETRIES = 1;

    // Descargar bytes crudos y decodificar en CPU pool (fuera del main thread).
    // downloadFromUrlData ya es asíncrono nativo (WebHelper::dispatch), no necesita
    // encolarse en main thread — eso introducía ~16ms de delay innecesario.
    auto attemptDownload = std::make_shared<std::function<void()>>(nullptr);
    *attemptDownload = [this, task, url, retryCount, attemptDownload]() {
        ThumbnailTransportClient::get().downloadFromUrlData(url,
            [this, task, url, retryCount, attemptDownload](bool success, std::vector<uint8_t> const& data) {
                if (task->cancelled || m_shuttingDown.load(std::memory_order_acquire)) {
                    finishTask(task, nullptr, false);
                    return;
                }
                if (success && !data.empty()) {
                    // Decodificar en CPU pool igual que level thumbnails
                    spawnCpu([this, task, data]() {
                        auto decoded = decodeImageData(data, 0, URL_CACHE_MAX_DIM);
                        if (decoded.success && decoded.image) {
                            enqueuePendingUpload({task, decoded.image, {}, 0, 0, 0,
                                false/*fallbackToDownload*/, decoded.width, decoded.height});
                        } else {
                            Loader::get()->queueInMainThread([this, task]() {
                                finishTask(task, nullptr, false);
                            });
                        }
                    });
                    return;
                }

                // Fix: CCTextureCache hit devuelve success=true con data vacío.
                // En ese caso la textura ya existe — usarla directamente.
                if (success && data.empty()) {
                    if (auto* tex = CCTextureCache::sharedTextureCache()->textureForKey(url.c_str())) {
                        finishTask(task, tex, true);
                        return;
                    }
                }

                // Fallo de descarga — reintentar 1 vez antes de marcar como fallo
                int attempt = retryCount->fetch_add(1);
                if (attempt < MAX_URL_DOWNLOAD_RETRIES) {
                    log::warn("[ThumbnailLoader] workerUrlDownload: download failed for {} (attempt {}), retrying...", url, attempt + 1);
                    paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                    (*attemptDownload)();
                } else {
                    log::warn("[ThumbnailLoader] workerUrlDownload: download failed for {} after retries", url);
                    paimon::cache::ThumbnailCache::get().stats().downloadErrors.fetch_add(1, std::memory_order_relaxed);
                    finishTask(task, nullptr, false);
                }
            }
        );
    };
    (*attemptDownload)();
}

// ── Decode pipeline (off main thread) ───────────────────────────────

ThumbnailLoader::DecodeResult ThumbnailLoader::decodeImageData(std::vector<uint8_t> const& data, int realID, int maxDim) {
    DecodeResult result;
    auto t0 = std::chrono::steady_clock::now();

    // Decode via ImagePlus (PNG, JPG, WebP, GIF, QOI, JPEG XL)
    // ImagePlus decodes to RGBA pixels. We then downsample if needed
    // and create a CCImage from the raw RGBA data for GPU upload via initWithImage.
    if (imgp::isAvailable()) {
        auto decRes = imgp::tryDecode(data.data(), data.size());
        if (decRes.isOk()) {
            auto& decoded = decRes.unwrap();

            int w = 0, h = 0;
            uint8_t* pixelData = nullptr;
            bool isAnim = false;

            if (auto* img = std::get_if<imgp::DecodedImage>(&decoded)) {
                if (*img && img->width > 0 && img->height > 0
                    && img->width <= 4096 && img->height <= 4096) {
                    w = img->width;
                    h = img->height;
                    pixelData = img->data.get();
                }
            } else if (auto* anim = std::get_if<imgp::DecodedAnimation>(&decoded)) {
                if (!anim->frames.empty() && anim->width > 0 && anim->height > 0
                    && anim->width <= 4096 && anim->height <= 4096) {
                    w = anim->width;
                    h = anim->height;
                    pixelData = anim->frames[0].data.get();
                    isAnim = true;
                }
            }

            if (pixelData && w > 0 && h > 0) {
                result.width = w;
                result.height = h;
                result.isGif = isAnim;

                // Extract dominant colors from RGBA data (before downsampling)
                if (realID > 0 && !LevelColors::get().getPair(realID)) {
                    LevelColors::get().extractFromRawData(realID, pixelData, w, h, true);
                }

                // Downsample if needed (on worker thread)
                if (maxDim > 0 && (w > maxDim || h > maxDim)) {
                    auto ds = ImageLoadHelper::downsampleForCache(pixelData, w, h, maxDim);
                    if (!ds.pixels.empty() && ds.width > 0 && ds.height > 0) {
                        // Create CCImage from downsampled RGBA pixels
                        auto* img = new CCImage();
                        if (img->initWithImageData(ds.pixels.data(), ds.pixels.size(),
                                cocos2d::CCImage::kFmtRawData,
                                ds.width, ds.height, 8, true)) {
                            result.image = img;
                            result.width = ds.width;
                            result.height = ds.height;
                            result.success = true;
                        } else {
                            delete img;
                        }
                    }
                }

                // No downsampling needed — create CCImage from original RGBA pixels
                if (!result.image) {
                    auto* img = new CCImage();
                    size_t byteCount = static_cast<size_t>(w) * h * 4;
                    if (img->initWithImageData(pixelData, byteCount,
                            cocos2d::CCImage::kFmtRawData,
                            w, h, 8, true)) {
                        result.image = img;
                        result.success = true;
                    } else {
                        delete img;
                    }
                }
            }
        }
    } else {
        log::warn("[ThumbnailLoader] ImagePlus no disponible — intentando CCImage nativo");
    }

    // Fallback: try CCImage::initWithImageData directly (handles PNG/JPG natively)
    if (!result.success && !data.empty()) {
        auto* img = new CCImage();
        if (img->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
            result.image = img;
            result.width = img->getWidth();
            result.height = img->getHeight();
            result.success = true;
        } else {
            delete img;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.decodeTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    paimon::cache::ThumbnailCache::get().stats().decodeTimeUsTotal.fetch_add(static_cast<uint64_t>(result.decodeTimeUs), std::memory_order_relaxed);

    return result;
}

// ── Remote revision ─────────────────────────────────────────────────

void ThumbnailLoader::updateRemoteRevision(int levelID, std::string const& revisionToken) {
    if (levelID <= 0 || revisionToken.empty()) return;

    std::lock_guard<std::recursive_mutex> lock(m_queueMutex);
    auto it = m_remoteRevisions.find(levelID);
    if (it != m_remoteRevisions.end() && it->second == revisionToken) {
        return; // sin cambios
    }

    bool hadPrevious = (it != m_remoteRevisions.end());
    m_remoteRevisions[levelID] = revisionToken;

    // si habia una revision anterior y cambio, invalidar la cache
    if (hadPrevious) {
        log::info("[ThumbnailLoader] remote revision changed for level {}, invalidating", levelID);
        // desbloqueo antes de llamar invalidateLevel (que toma el lock)
        // pero como invalidateLevel necesita queueMutex, lo hago en main thread
        Loader::get()->queueInMainThread([this, levelID]() {
            invalidateLevel(levelID, false);
            invalidateLevel(levelID, true);
        });
    }
}

// ── Manifest flush ──────────────────────────────────────────────────

void ThumbnailLoader::flushManifest() {
    spawnDisk([]() {
        paimon::cache::ThumbnailCache::get().saveDiskIndex();
    });
}
