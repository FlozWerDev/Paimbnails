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
#include <atomic>
#include <chrono>
#include <functional>
#include <prevter.imageplus/include/events.hpp>
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
    static constexpr int PriorityHero = 5;
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
    std::unordered_map<std::string, std::shared_ptr<Task>> m_urlTasks; // url -> tarea gallery
    std::multimap<int, int, std::greater<int>> m_priorityQueue; // prioridad (desc) -> levelID
    std::atomic<int> m_activeTaskCount{0};
    std::atomic<int> m_activeUrlTaskCount{0};
    int m_maxConcurrentTasks = 8;
    int m_maxConcurrentUrlTasks = 6;
    mutable std::recursive_mutex m_queueMutex;

    // cache gifs (tracking which levels have GIF data)
    std::unordered_set<int> m_gifLevels;

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
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_CALLBACKS_PER_FRAME = 24;
#else
    static constexpr int MAX_CALLBACKS_PER_FRAME = 128;
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
    static constexpr int MAX_UPLOADS_PER_FRAME = 6;          // tope absoluto de seguridad
    static constexpr int64_t UPLOAD_FRAME_BUDGET_US = 3000;   // 3ms max por frame en movil
#else
    // Desktop: presupuesto agresivo — en GPUs modernas el upload de texturas
    // pequeñas/medianas cuesta <0.5ms cada una; dejamos que se llene hasta
    // ~12ms del frame (~72% de un frame a 60Hz). drainPendingUploads se
    // yielda antes de pasar el budget, asi no rompe FPS aunque se llene.
    static constexpr int MAX_UPLOADS_PER_FRAME = 32;          // tope absoluto de seguridad
    static constexpr int64_t UPLOAD_FRAME_BUDGET_US = 12000;  // 12ms max por frame en desktop
#endif
    // Maximum dimension for RAM-cached thumbnails. Images larger than this
    // are downsampled before GPU upload to reduce RAM usage and upload time.
    // LevelCell displays thumbnails at ~90-180px, asi que no tiene sentido
    // guardar 2048x2048 en RAM. Desktop usa 1024 para tener headroom para
    // blur de alta intensidad sin perdida de detalle.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int RAM_CACHE_MAX_DIM = 256;  // smaller on mobile to save RAM
#else
    static constexpr int RAM_CACHE_MAX_DIM = 1024;
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
    void processUrlQueue();
    void spawnDisk(std::function<void()> job);  // encola en pool de I/O de disco (2 threads)
    void spawnCpu(std::function<void()> job);   // encola en pool de CPU (decode, 4/2 threads)
    void waitBackgroundWorkers();

    // decode helper: decodifica a CCImage fuera del main thread
    struct DecodeResult {
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
