#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <filesystem>
#include <vector>
#include <chrono>

namespace paimon::video {
    class VideoPlayer;
}

struct LayerBgConfig {
    std::string type = "default";   // default, custom, random, menu, id, video, shader
    std::string customPath;         // image/gif/video
    int levelId = 0;
    bool darkMode = false;
    float darkIntensity = 0.5f;
    std::string shader = "none";
};

struct LayerMusicConfig {
    std::string mode = "default";   // default, newgrounds, custom, dynamic
    int songID = 0;
    std::string customPath;
    float speed = 1.0f;
    bool randomStart = false;
    int startMs = 0;
    int endMs = 0;
    std::string filter = "none";
};

class LayerBackgroundManager {
public:
    static LayerBackgroundManager& get();

    // release gl-owned textures before gamemanager::reloadall recreates the context.
    void onGLContextReload();

    // call after super::init(); returns whether custom ui should be hidden.
    bool applyBackground(cocos2d::CCLayer* layer, std::string const& layerKey);

    void applyVanillaBackgroundTintFix(cocos2d::CCLayer* layer);

    bool hasCustomBackground(std::string const& layerKey) const;

    LayerBgConfig getConfig(std::string const& layerKey) const;

    void saveConfig(std::string const& layerKey, LayerBgConfig const& cfg);

    LayerBgConfig resolveConfig(std::string const& layerKey) const;

    LayerMusicConfig getMusicConfig(std::string const& layerKey) const;
    void saveMusicConfig(std::string const& layerKey, LayerMusicConfig const& cfg);

    void saveGlobalMusicConfig(LayerMusicConfig const& cfg);

    static inline std::vector<std::pair<std::string, std::string>> LAYER_OPTIONS = {
        {"menu",         "Menu"},
        {"levelinfo",    "Level Info"},
        {"levelselect",  "Level Select"},
        {"creator",      "Creator"},
        {"browser",      "Browser"},
        {"search",       "Search"},
        {"leaderboards", "Leaderboards"},
        {"profile",      "Profile"},
        {"garage",       "Garage"},
    };

    // migrate legacy background keys once.
    void migrateFromLegacy();

    // move external assets into managed storage.
    void migrateExternalAssetsToManagedStorage();

    void migrateToGlobalMusic();

    void applyVideoBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    bool applyProceduralShaderBg(cocos2d::CCLayer* layer, LayerBgConfig const& cfg);

    void clearAppliedBackground(cocos2d::CCLayer* layer, bool suppressAudioResume = false);

private:
    LayerBackgroundManager() = default;

    // cache entries are invalidated by saveconfig.
    mutable std::unordered_map<std::string, LayerBgConfig> m_configCache;
    mutable std::mutex m_configCacheMutex;

    void hideOriginalBg(cocos2d::CCLayer* layer);
    void showOriginalBg(cocos2d::CCLayer* layer);
    cocos2d::CCTexture2D* loadTextureForConfig(LayerBgConfig const& cfg);
    bool applyStaticBg(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* tex, LayerBgConfig const& cfg);
    void applyGifBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    // unreferenced players linger briefly; revisits reuse the decoder.
    static constexpr auto kSharedVideoTTL = std::chrono::seconds(10);

    struct SharedVideoEntry {
        std::shared_ptr<paimon::video::VideoPlayer> player;
        int refCount = 0;
        // unreferenced and awaiting eviction; revived on re-acquire.
        bool stale = false;
        std::chrono::steady_clock::time_point expiry =
            std::chrono::steady_clock::time_point::max();
        std::chrono::steady_clock::time_point lastUsed =
            std::chrono::steady_clock::now();
    };
    std::unordered_map<std::string, SharedVideoEntry> m_sharedVideos;
    std::unordered_map<std::string, int> m_pendingSharedVideoCreates;
    mutable std::mutex m_sharedVideosMutex;

    int activeVideoCount_locked() const;

    int adaptiveFPSForCount(int activeCount) const;

    void rebalanceAdaptiveFPS_locked();

    std::vector<std::shared_ptr<paimon::video::VideoPlayer>>
        evictLRUForBudget_locked(std::string const& reservedPath, int maxConcurrent);

public:
    std::shared_ptr<paimon::video::VideoPlayer> acquireSharedVideo(
        std::string const& path, bool requireCanonicalAudio);

    // reuse-only acquire never builds decoders; main-thread callers can't stall. null when empty.
    std::shared_ptr<paimon::video::VideoPlayer> acquireExistingSharedVideo(
        std::string const& path);

    void releaseSharedVideo(std::string const& path);

    void evictExpiredSharedVideos();

    // call during $on_game(exiting), before media foundation shuts down.
    void releaseAllSharedVideos();

    void forceReleaseSharedVideoByPath(std::string const& path);

    void forceEvictAllStaleVideos();

    // stop video audio without destroying players or visuals.
    void releaseAllVideoAudio();

    bool hasSharedVideo(std::string const& path) const;

    size_t getTotalVideoRAMBytes() const;

    void broadcastFPSUpdate(int newFPS);

    void broadcastRotationUpdate(int newRotationDegrees);

    void cleanupOldVideoCache(cocos2d::CCLayer* layer, std::string const& nextVideoPath);

    // first-frame preview cache for video backgrounds.
    static std::filesystem::path getVideoBgPreviewDir();

    static std::filesystem::path getVideoBgPreviewPath(std::string const& videoPath);

    // downscaled poster in ram; repeat entries skip disk and re-upload. null when none.
    static cocos2d::CCTexture2D* getVideoBgPreviewTexture(std::string const& videoPath);

    // true when a preview newer than the video is already cached on disk.
    static bool hasVideoBgPreview(std::string const& videoPath);

    // off-thread current-frame readback; no-op when fresh, so one gpu stall per video.
    static void saveVideoBgPreview(std::string const& videoPath,
                                   paimon::video::VideoPlayer const* player);
};
