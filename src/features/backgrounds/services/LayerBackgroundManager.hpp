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
    std::string customPath;         // image/GIF/video
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

static inline std::vector<std::pair<std::string, std::string>> AUDIO_FILTERS = {
    {"none",        "None"},
    {"cave",        "Cave"},
    {"underwater",  "Underwater"},
    {"echo",        "Echo"},
    {"hall",        "Concert Hall"},
    {"radio",       "Old Radio"},
    {"phone",       "Phone Call"},
    {"chorus",      "Chorus"},
    {"flanger",     "Flanger"},
    {"distortion",  "Distortion"},
    {"tremolo",     "Tremolo"},
    {"nightcore",   "Nightcore"},
    {"vaporwave",   "Vaporwave"},
};

class LayerBackgroundManager {
public:
    static LayerBackgroundManager& get();

    // Release GL-owned textures before GameManager::reloadAll recreates the context.
    void onGLContextReload();

    // Call after super::init(); returns whether custom UI should be hidden.
    bool applyBackground(cocos2d::CCLayer* layer, std::string const& layerKey);

    void applyVanillaBackgroundTintFix(cocos2d::CCLayer* layer);

    bool hasCustomBackground(std::string const& layerKey) const;

    LayerBgConfig getConfig(std::string const& layerKey) const;

    void saveConfig(std::string const& layerKey, LayerBgConfig const& cfg);

    LayerBgConfig resolveConfig(std::string const& layerKey) const;

    std::string hasOtherVideoConfigured(std::string const& excludeLayerKey, std::string const& videoPath) const {
        (void)excludeLayerKey;
        (void)videoPath;
        return {};
    }

    LayerMusicConfig getMusicConfig(std::string const& layerKey) const;
    void saveMusicConfig(std::string const& layerKey, LayerMusicConfig const& cfg);

    LayerMusicConfig getGlobalMusicConfig() const;
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

    // Migrate legacy background keys once.
    void migrateFromLegacy();

    // Move external assets into managed storage.
    void migrateExternalAssetsToManagedStorage();

    void migrateToGlobalMusic();

    void applyVideoBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    bool applyProceduralShaderBg(cocos2d::CCLayer* layer, LayerBgConfig const& cfg);

    void clearAppliedBackground(cocos2d::CCLayer* layer, bool suppressAudioResume = false);

private:
    LayerBackgroundManager() = default;

    // Cache entries are invalidated by saveConfig.
    mutable std::unordered_map<std::string, LayerBgConfig> m_configCache;
    mutable std::mutex m_configCacheMutex;

    void hideOriginalBg(cocos2d::CCLayer* layer);
    void showOriginalBg(cocos2d::CCLayer* layer);
    cocos2d::CCTexture2D* loadTextureForConfig(LayerBgConfig const& cfg);
    bool applyStaticBg(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* tex, LayerBgConfig const& cfg);
    void applyGifBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    // Layers resolving to the same path share a decoder. An unreferenced player
    // is kept alive for this long so navigating away and back reuses it instead
    // of rebuilding the decoder and the whole GPU upload pipeline. Its decode
    // thread parks on a full ring buffer meanwhile, so idle cost is negligible.
    static constexpr auto kSharedVideoTTL = std::chrono::seconds(10);

    struct SharedVideoEntry {
        std::shared_ptr<paimon::video::VideoPlayer> player;
        int refCount = 0;
        // Unreferenced and awaiting eviction; revived on re-acquire.
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

    // Reuse-only acquire: never builds a decoder, so main-thread callers cannot
    // stall on one. Returns null when nothing reusable is cached.
    std::shared_ptr<paimon::video::VideoPlayer> acquireExistingSharedVideo(
        std::string const& path);

    void releaseSharedVideo(std::string const& path);

    void evictExpiredSharedVideos();

    // Call during $on_game(Exiting), before Media Foundation shuts down.
    void releaseAllSharedVideos();

    void forceReleaseSharedVideoByPath(std::string const& path);

    void forceEvictAllStaleVideos();

    // Stop video audio without destroying players or visuals.
    void releaseAllVideoAudio();

    bool hasSharedVideo(std::string const& path) const;

    size_t getTotalVideoRAMBytes() const;

    void broadcastFPSUpdate(int newFPS);

    void broadcastRotationUpdate(int newRotationDegrees);

    void cleanupOldVideoCache(cocos2d::CCLayer* layer, std::string const& nextVideoPath);

    // First-frame preview cache for video backgrounds.
    static std::filesystem::path getVideoBgPreviewDir();

    static std::filesystem::path getVideoBgPreviewPath(std::string const& videoPath);

    // Downscaled poster frame, retained in RAM so repeat layer entries neither
    // hit the disk nor re-upload a texture. Null when there is no preview yet.
    static cocos2d::CCTexture2D* getVideoBgPreviewTexture(std::string const& videoPath);

    // True when a preview newer than the video is already cached on disk.
    static bool hasVideoBgPreview(std::string const& videoPath);

    // Reads back the current frame and writes the preview off-thread. No-op when
    // a fresh preview already exists, so the GPU readback happens once per video.
    static void saveVideoBgPreview(std::string const& videoPath,
                                   paimon::video::VideoPlayer const* player);
};
