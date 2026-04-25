#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/function.hpp>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <string>
#include <mutex>
#include <atomic>
#include <future>
#include <vector>
#include <functional>

#include <unordered_set>

struct ProfileConfig {
    std::string backgroundType = "gradient";
    float blurIntensity = 3.0f;
    float darkness = 0.2f;
    bool useGradient = false;
    cocos2d::ccColor3B colorA = {255,255,255};
    cocos2d::ccColor3B colorB = {255,255,255};
    cocos2d::ccColor3B separatorColor = {0,0,0};
    int separatorOpacity = 50;
    float widthFactor = 0.60f;
    bool hasConfig = false;
    std::string gifKey = ""; // gifKey anadido para referencia en cache local

    // Comment cell background settings
    std::string commentBgType = "none"; // "none", "thumbnail", "banner", "solid"
    std::string commentBgThumbnailId = ""; // level ID for thumbnail source
    int commentBgThumbnailPos = 1; // thumbnail position (1-based) for levels with multiple thumbnails
    std::string commentBgBannerMode = "background"; // "background" or "image" for banner sub-type
    cocos2d::ccColor3B commentBgSolidColor = {30, 30, 30};
    int commentBgSolidOpacity = 128;
    std::string commentBgBlurType = "gaussian"; // "gaussian" or "paimon"
    float commentBgBlur = 5.0f;
    float commentBgDarkness = 0.35f;
};

struct ProfileCacheEntry {
    geode::Ref<cocos2d::CCTexture2D> texture;
    std::string gifKey;
    cocos2d::ccColor3B colorA;
    cocos2d::ccColor3B colorB;
    float widthFactor;
    std::chrono::steady_clock::time_point timestamp;
    ProfileConfig config;
    
    ProfileCacheEntry() : texture(nullptr), gifKey(""), colorA({255,255,255}), colorB({255,255,255}), 
                         widthFactor(0.5f), timestamp(std::chrono::steady_clock::now()) {}
    
    ProfileCacheEntry(cocos2d::CCTexture2D* tex, cocos2d::ccColor3B ca, cocos2d::ccColor3B cb, float w) 
        : texture(tex), gifKey(""), colorA(ca), colorB(cb), widthFactor(w), 
          timestamp(std::chrono::steady_clock::now()) {}

    ProfileCacheEntry(std::string const& key, cocos2d::ccColor3B ca, cocos2d::ccColor3B cb, float w) 
        : texture(nullptr), gifKey(key), colorA(ca), colorB(cb), widthFactor(w), 
          timestamp(std::chrono::steady_clock::now()) {}
};

class ProfileThumbs {
public:
    static ProfileThumbs& get();
    
    // Evita liberar objetos cocos durante shutdown
    static inline std::atomic<bool> s_shutdownMode{false};

    ~ProfileThumbs() {
        if (s_shutdownMode.load(std::memory_order_acquire)) {
            m_pendingCallbacks.clear();
            m_downloadQueue.clear();
            for (auto& [id, entry] : m_profileCache) {
                (void)entry.texture.take();
            }
            m_lruOrder.clear();
            m_lruMap.clear();
        }
    }

    bool saveRGB(int accountID, const uint8_t* rgb, int width, int height);
    bool has(int accountID) const;
    void deleteProfile(int accountID); // Borra local y cache
    cocos2d::CCTexture2D* loadTexture(int accountID);
    bool loadRGB(int accountID, std::vector<uint8_t>& out, int& w, int& h);

    // Cache en memoria de texturas de perfil
    void cacheProfile(int accountID, cocos2d::CCTexture2D* texture, 
                     cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor);
    
    void cacheProfileGIF(int accountID, std::string const& gifKey, 
                     cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor);
    
    void cacheProfileConfig(int accountID, ProfileConfig const& config);
    ProfileConfig getProfileConfig(int accountID);

    ProfileCacheEntry* getCachedProfile(int accountID);
    void clearCache(int accountID); // elimina entrada especifica
    void clearOldCache(); // elimina entradas mas viejas de 14 dias
    void clearAllCache(); // limpia todas las entradas cacheadas

    // crea un nodo con fondo + imagen de perfil
    cocos2d::CCNode* createProfileNode(cocos2d::CCTexture2D* texture, ProfileConfig const& config, cocos2d::CCSize size, bool onlyBackground = false);

    // Encola la descarga de una miniatura de perfil
    void queueLoad(int accountID, std::string const& username, geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)> callback);

    // Marca el perfil como visible (aumenta prioridad de descarga)
    void notifyVisible(int accountID);

    // Cache de usuarios sin miniatura
    void markNoProfile(int accountID);
    void removeFromNoProfileCache(int accountID);
    bool isNoProfile(int accountID) const;
    void clearNoProfileCache();

    // Limpia descargas pendientes para shutdown seguro
    void clearPendingDownloads();
    void shutdown();

private:
    ProfileThumbs() = default;
    std::string makePath(int accountID) const;
    void processQueue();
    void processBinaryQueue(); // Descarga binarios tras verificacion batch
    
    std::unordered_map<int, ProfileCacheEntry> m_profileCache;
    // Estructura LRU para cache
    std::list<int> m_lruOrder;
    std::unordered_map<int, std::list<int>::iterator> m_lruMap;
    mutable std::mutex m_cacheMutex; // Protege el cache de acceso concurrente
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_visibilityMap; // Ultima vez que se vio cada perfil
    std::unordered_set<int> m_noProfileCache; // Usuarios sin miniatura (evita requests repetidos)
    std::unordered_map<int, std::string> m_usernameMap; // accountID -> username para descargas pendientes
    static constexpr auto CACHE_DURATION = std::chrono::hours(24 * 14); // 14 dias
    static constexpr size_t MAX_PROFILE_CACHE_SIZE = 100; // Maximo de entradas en cache
    static constexpr size_t MAX_NO_PROFILE_CACHE_SIZE = 1024;
    int m_insertsSinceCleanup = 0;
    static constexpr int CLEANUP_INTERVAL = 20; // cada N inserciones revisar expiradas

    // sistema de cola
    std::deque<int> m_downloadQueue;
    std::unordered_map<int, std::vector<geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)>>> m_pendingCallbacks;
    int m_activeDownloads = 0;
    const int MAX_CONCURRENT_DOWNLOADS = 10;
    bool m_batchInFlight = false; // Batch check en progreso
    std::deque<int> m_binaryQueue; // IDs confirmados por batch, pendientes de descarga binaria
    std::unordered_map<int, ProfileConfig> m_batchConfigs; // configs que vinieron en el batch

    void spawnBackground(std::function<void()> job);
    void pruneFinishedWorkers();
    void waitBackgroundWorkers();
    std::vector<std::future<void>> m_backgroundWorkers;
    std::mutex m_workerMutex;
};

