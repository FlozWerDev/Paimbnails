#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/function.hpp>
#include <string>
#include <deque>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include "../../../utils/FormatDetect.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../utils/ThreadPool.hpp"
#include "CacheModels.hpp"
#include "ThumbnailCache.hpp"

/**
 * cargador de thumbnails optimizado:
 * - limite de concurrencia
 * - cola por prioridad
 * - cache automatico con manifest persistente
 * - claves canonicas (tipo + levelID/URL + calidad)
 * - cache compartido de URLs de galeria
 * - instrumentacion de hits/misses/evictions
 * - evita lag
 */
class ThumbnailLoader {
public:
    using LoadCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* texture, bool success)>;
    using InvalidationCallback = geode::CopyableFunction<void(int levelID)>;

    static constexpr int PriorityBootstrap = 1;
    static constexpr int PriorityPredictivePrefetch = 2;
    static constexpr int PriorityVisiblePrefetch = 3;
    static constexpr int PriorityHero = 15;
    static constexpr int PriorityVisibleCell = 10;

    static ThumbnailLoader& get();

    // Quality levels — server delivers different sizes
    enum class Quality {
        Small,   // LevelCell: ~256px max, fastest download
        High      // LevelInfoLayer/popup: full resolution
    };

    // pedir carga de thumbnail. mayor valor = mas prioridad
    void requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority = 0, bool isGif = false, Quality quality = Quality::Small);
    void prefetchLevelAssets(int levelID, int priority = 0);
    void prefetchLevels(std::vector<int> const& levelIDs, int priority = 0);
    
    // carga por URL (para gallery thumbnails compartidos entre vistas)
    void requestUrlLoad(std::string const& url, LoadCallback callback, int priority = 0);
    void requestUrlBatchLoad(std::vector<std::string> const& urls, LoadCallback perUrlCallback, int priority = 0);
    bool isUrlLoaded(std::string const& url) const;
    void cancelUrlLoad(std::string const& url);

    // cancelar carga pendiente
    void cancelLoad(int levelID, bool isGif = false);
    
    // cache
    bool isLoaded(int levelID, bool isGif = false) const;
    bool isPending(int levelID, bool isGif = false) const;
    bool isFailed(int levelID, bool isGif = false) const;
    bool isNotFound(int levelID, bool isGif = false) const;

    // Fast path sincronico para hero thumbnails: si la textura ya esta en
    // RAM, la devuelve inmediatamente. Si no, devuelve nullptr y el caller
    // debe hacer requestLoad normal. NO toca disk LRU ni encola callbacks
    // — es 100% lectura del RAM cache (zero overhead).
    cocos2d::CCTexture2D* tryGetCachedTexture(int levelID, bool isGif = false);

    void clearCache();
    void clearFailedCache();
    void invalidateLevel(int levelID, bool isGif = false);

    // revision remota: actualiza el token de revision conocido para un level
    // si es distinto al actual, invalida la cache automaticamente
    void updateRemoteRevision(int levelID, std::string const& revisionToken);

    // version de invalidacion: se incrementa cada vez que se invalida un level
    // los consumidores (LevelCell, etc) guardan la version cuando cargan
    // y la comparan pa saber si deben recargar
    int getInvalidationVersion(int levelID) const;
    int addInvalidationListener(InvalidationCallback callback);
    void removeInvalidationListener(int listenerId);

    // config
    void setMaxConcurrentTasks(int max);
    void setBatchMode(bool enabled) { m_batchMode = enabled; }

    int getActiveTaskCount() const { return m_activeTaskCount; }

    // helpers
    static bool isTextureSane(cocos2d::CCTexture2D* tex);
    std::filesystem::path getCachePath(int levelID, bool isGif = false);

    // normaliza URL para cache key (strip _pv, _cb, ts, v, t params)
    static std::string normalizeUrlKey(std::string const& url);
    
    // compatibilidad
    void updateSessionCache(int levelID, cocos2d::CCTexture2D* texture);
    bool hasGIFData(int levelID) const;
    void cleanup();
    void clearDiskCache();
    void clearPendingQueue();

    // persistence
    void flushManifest();

    // instrumentacion
    paimon::cache::CacheStats& stats() { return paimon::cache::ThumbnailCache::get().stats(); }
    paimon::cache::CacheStats const& stats() const { return paimon::cache::ThumbnailCache::get().stats(); }



private:
    ThumbnailLoader();
    ~ThumbnailLoader();

    struct Task {
        int levelID;
        std::string fileName;
        std::string url; // para tareas URL-based (gallery) — URL real de descarga
        std::string urlCacheKey; // key normalizada para RAM/failed cache (strip _pv, etc.)
        int priority;
        std::vector<LoadCallback> callbacks;
        bool running = false;
        bool cancelled = false;
        bool isUrlTask = false; // true si es carga por URL (gallery cache compartido)
        bool wasNotFound = false; // true si el servidor respondio 404 (thumbnail no existe)
        std::chrono::steady_clock::time_point startedAt{}; // para medir tiempo total del pipeline
    };

    // manejo de cola — int key para level tasks, string key para url tasks
    std::unordered_map<int, std::shared_ptr<Task>> m_tasks; // id -> tarea (pendiente y corriendo)
    std::unordered_map<std::string, std::shared_ptr<Task>> m_urlTasks; // normalized URL cache key -> gallery task
    std::multimap<int, int, std::greater<int>> m_priorityQueue; // prioridad (desc) -> levelID
    std::atomic<int> m_activeTaskCount{0};
    std::atomic<int> m_activeUrlTaskCount{0};
    int m_maxConcurrentTasks = 8;
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    int m_maxConcurrentUrlTasks = 4;
#else
    int m_maxConcurrentUrlTasks = 8;
#endif
    mutable std::recursive_mutex m_queueMutex;

    // cache gifs (tracking which levels have GIF data) — shared_mutex para evitar
    // contender con m_queueMutex en hot path (LevelCell::hasGIFData lo consulta
    // 4 veces durante scroll). Lectura concurrente sin bloqueo entre cells.
    std::unordered_set<int> m_gifLevels;
    mutable std::shared_mutex m_gifLevelsMutex;

    // remote revision tokens por level (thumbnailId o fallback)
    std::unordered_map<int, std::string> m_remoteRevisions;

    std::unordered_map<int, InvalidationCallback> m_invalidationListeners;
    int m_nextInvalidationListenerId = 1;

    // refresco proactivo: niveles que ya se comprobaron recientemente
    std::unordered_set<int> m_revisionCheckedThisSession;
    void triggerBackgroundRevisionCheck(int levelID);

    bool m_batchMode = false;

    // global download cooldown: cuando muchas descargas fallan en poco tiempo,
    // se activa un cooldown para no seguir martillando el servidor/CDN.
    // Esto evita el escenario donde el rate-limit del servidor causa que
    // TODOS los thumbnails se marquen como fallidos y el usuario tenga que esperar.
    std::atomic<int> m_recentFailureCount{0};
    std::chrono::steady_clock::time_point m_failureWindowStart{};
    std::chrono::steady_clock::time_point m_globalCooldownUntil{};
    std::mutex m_cooldownMutex;
    static constexpr int FAILURE_THRESHOLD = 8;          // fallos en la ventana para activar cooldown
    static constexpr int FAILURE_WINDOW_SECONDS = 6;     // ventana de tiempo para contar fallos
    static constexpr int COOLDOWN_SECONDS = 3;           // pausa global tras detectar rate-limit
    void recordDownloadFailure();
    bool isGlobalCooldownActive() const;

    // flag de shutdown
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_cleanupStarted{false};
    std::atomic<bool> m_cleanupFinished{false};

    // callback batching: cache hits y worker completions se encolan aqui
    // y se drenan max N por frame para no trabar la UI al scrollear rapido
    struct PendingCallback {
        LoadCallback callback;
        geode::Ref<cocos2d::CCTexture2D> texture;
        bool success;
        int levelID = 0;        // para verificar invalidation version
        int capturedVersion = 0; // version al momento de encolar
    };
    std::vector<PendingCallback> m_pendingCallbacks;
    std::mutex m_pendingMutex;
    std::atomic<bool> m_drainScheduled{false};
    // microsegundos desde steady_clock epoch del momento en que m_drainScheduled
    // se puso true. Si pasaron >100ms y nadie limpio el flag (queueInMainThread
    // no llego a ejecutar — shutdown intermedio, scheduler pausado, etc.) se
    // considera stale y enqueuePendingCallback puede re-armar el drain.
    std::atomic<int64_t> m_drainScheduledAtUs{0};
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_CALLBACKS_PER_FRAME = 3;
#else
    // Desktop @ 360fps: cada frame es ~2.78ms. Un callback de LevelCell dispara
    // cleanPaimonNodes + setupClippingAndSeparator + setupGradient (~0.3-0.6ms).
    // Con 4 callbacks = ~1.2-2.4ms, dejando ~0.4-1.6ms para render + game logic.
    // A 60fps esto seria ~24 callbacks/frame equivalentes; a 360fps necesitamos
    // mucho menos por frame pero mas frames por segundo para la misma tasa neta.
    // Aumentado a 4 para reducir latencia de batching en listas de 10+ celdas
    // y evitar que la ultima celda se quede esperando demasiados frames.
    // El rate-limiting mejorado en LevelCell (que ahora NUNCA bloquea la primera
    // aplicacion) garantiza que el trabajo extra no se acumule.
    static constexpr int MAX_CALLBACKS_PER_FRAME = 4;
#endif
    void enqueuePendingCallback(LoadCallback cb, cocos2d::CCTexture2D* tex, bool success, int levelID = 0);
    void drainPendingCallbacks();
    void scheduleDrain();

    // texture upload batching: decoded data se encola aqui
    // y se sube a GPU max N por frame para no trabar el render
    // Two modes:
    //   Mode 1 (image != nullptr): CCImage from encoded data — use initWithImage
    //   Mode 2 (pixels not empty): Raw RGBA from .rgb conversion — use initWithData RGBA8888
    struct PendingUpload {
        std::shared_ptr<Task> task;
        cocos2d::CCImage* image = nullptr;      // Mode 1: owned, deleted after upload
        std::vector<uint8_t> pixels;            // Mode 2: raw RGBA8888 pixels
        int width = 0;                          // Mode 2: pixel width
        int height = 0;                         // Mode 2: pixel height
        int realID = 0;
        bool fallbackToDownload = false;
        int originalWidth = 0;
        int originalHeight = 0;
    };
    std::vector<PendingUpload> m_pendingUploads;
    std::mutex m_uploadMutex;
    std::atomic<bool> m_uploadDrainScheduled{false};
    // Presupuesto adaptativo de GPU upload: en vez de un limite fijo,
    // medimos cuanto tarda cada upload y paramos cuando consumimos
    // demasiado tiempo del frame. Asi en PCs rapidos subimos mas,
    // y en moviles lentos subimos menos — sin bajar FPS.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_UPLOADS_PER_FRAME = 3;          // tope absoluto de seguridad
    static constexpr int64_t UPLOAD_FRAME_BUDGET_US = 1500;   // 1.5ms max por frame en movil (360fps ~2.78ms total)
#else
    // Desktop @ 360fps: frame budget = ~2778us. Dejar headroom para game logic
    // y render. A 60fps equivalente seria ~16 uploads/frame; a 360fps limitamos
    // a 4 uploads por frame para no consumir mas de ~1.5ms del budget.
    static constexpr int MAX_UPLOADS_PER_FRAME = 4;           // tope absoluto de seguridad
    static constexpr int64_t UPLOAD_FRAME_BUDGET_US = 1500;  // 1.5ms max por frame en desktop
#endif
    // Maximum dimension for RAM-cached thumbnails. Images larger than this
    // are downsampled before GPU upload to reduce RAM usage and upload time.
    // LevelCell displays thumbnails at ~90-180px, but LevelInfoLayer popup shows
    // them much larger. Previous value of 1024 caused 1080p captures to appear
    // at ~720p (1024x576). Use 1920 to preserve full 1080p resolution.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int RAM_CACHE_MAX_DIM = 512;   // larger on mobile for better popup quality
    static constexpr int URL_CACHE_MAX_DIM = 512;
#else
    static constexpr int RAM_CACHE_MAX_DIM = 1920;   // preserve 1080p from capturer
    static constexpr int URL_CACHE_MAX_DIM = 1920;
#endif
    void enqueuePendingUpload(PendingUpload upload);
    void drainPendingUploads();
    void scheduleUploadDrain();

    // metodos
    bool beginCleanup(char const* reason);
    void logShutdownSnapshot(char const* reason);
    void processQueue();
    void startTask(std::shared_ptr<Task> task);
    void finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success, int origW = 0, int origH = 0);
    
    void initDiskCache();
    
    // Worker methods
    void workerLoadFromDisk(std::shared_ptr<Task> task);
    void workerDownload(std::shared_ptr<Task> task);
    void processDownloadedData(std::shared_ptr<Task> task, std::vector<uint8_t> data, int realID);
    void workerUrlDownload(std::shared_ptr<Task> task);

    // ── Batch download coalescing ────────────────────────────────────
    // Cuando varias tasks entran a workerDownload casi al mismo tiempo
    // (scroll de un LevelList con 10-30 celdas visibles), las acumulamos en
    // un buffer y disparamos una sola request /api/thumbnails/batch en lugar
    // de N requests /t/{id}. Esto reduce 30 round-trips a 1.
    struct BatchPending {
        std::shared_ptr<Task> task;
        std::shared_ptr<std::atomic<int>> retryCount;
    };
    std::vector<BatchPending> m_batchPendingDownloads;
    std::mutex m_batchPendingMutex;
    std::atomic<bool> m_batchFlushScheduled{false};
    static constexpr int BATCH_FLUSH_THRESHOLD = 40;   // cap del server
    static constexpr int BATCH_FLUSH_DELAY_MS = 50;    // ventana de coalescing
    void scheduleBatchFlush();
    void flushBatchDownloads();
    void enqueueBatchDownload(std::shared_ptr<Task> task, std::shared_ptr<std::atomic<int>> retryCount);

    void processUrlQueue();
    void spawnDisk(std::function<void()> job);  // encola en pool de I/O de disco (2 threads)
    void spawnCpu(std::function<void()> job);   // encola en pool de CPU (decode, 4/2 threads)
    void waitBackgroundWorkers();

    // decode helper: decodifica a CCImage fuera del main thread
    struct DecodeResult {
        // Preferido: pixels RGBA listos para subir via initWithData (mode 2 en PendingUpload).
        // Evita la copia intermedia que hace CCImage::initWithImageData(kFmtRawData, ..., true).
        std::vector<uint8_t> pixels;
        // Fallback: solo se usa cuando stb_image no pudo decodificar y caemos al
        // CCImage::initWithImageData nativo con el archivo PNG/JPG crudo.
        cocos2d::CCImage* image = nullptr;  // owned by caller, must be deleted if not used
        int width = 0;
        int height = 0;
        bool isGif = false;
        bool success = false;
        int64_t decodeTimeUs = 0;
    };
    DecodeResult decodeImageData(std::vector<uint8_t> const& data, int realID, int maxDim = 0);

    // Thread pools de tamano fijo — reemplazan std::async sin limite.
    // Disk pool: serializa I/O de disco (2 threads)
    // CPU pool: decode de imagenes + color extraction
    std::unique_ptr<paimon::ThreadPool> m_diskPool;
    std::unique_ptr<paimon::ThreadPool> m_cpuPool;

    // Mutex para serializar lecturas de disco.
    // Aunque el disk pool tiene 2 threads, solo 1 lee a la vez
    // para evitar I/O thrash. El segundo thread puede escribir.
    std::mutex m_diskReadMutex;

    static constexpr auto MAX_DISK_CACHE_AGE = std::chrono::hours(24 * 21);
    static constexpr auto FAILED_CACHE_TTL = std::chrono::minutes(10);
};
