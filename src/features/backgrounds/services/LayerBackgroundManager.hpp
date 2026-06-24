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

// Forward declaration for shared video player cache
namespace paimon::video {
    class VideoPlayer;
}

// Saved value pattern: "layerbg-{key}-type", "layerbg-{key}-path"
struct LayerBgConfig {
    std::string type = "default";   // "default", "custom", "random", "menu", "id", "video", "shader"
    std::string customPath;         // image/GIF/video path
    int levelId = 0;                // for type "id"
    bool darkMode = false;
    float darkIntensity = 0.5f;
    std::string shader = "none";    // "none","grayscale","sepia","vignette","bloom","chromatic","pixelate","posterize","scanlines"
};

// Per-layer music config
struct LayerMusicConfig {
    std::string mode = "default";   // "default", "newgrounds", "custom", "dynamic"
    int songID = 0;                 // Newgrounds song ID
    std::string customPath;         // local audio file path
    float speed = 1.0f;            // playback speed 0.1 - 1.0
    bool randomStart = false;       // start from a random position
    int startMs = 0;                // loop/play start time (0 = from beginning)
    int endMs = 0;                  // loop/play end time (0 = until end)
    std::string filter = "none";    // audio filter: none, cave, underwater, echo, etc.
};

// Available audio filters
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

    // Apply a background to the layer by key. Call after super::init().
    // Returns true if a custom background was applied (so the hook hides extra UI).
    bool applyBackground(cocos2d::CCLayer* layer, std::string const& layerKey);

    // Remove the vanilla blue background tint when no custom bg is active.
    void applyVanillaBackgroundTintFix(cocos2d::CCLayer* layer);

    // Does this layer have a custom background configured? (applies nothing)
    bool hasCustomBackground(std::string const& layerKey) const;

    // Read config from Mod saved values for a key
    LayerBgConfig getConfig(std::string const& layerKey) const;

    // Save config to Mod saved values
    void saveConfig(std::string const& layerKey, LayerBgConfig const& cfg);

    // Resolve the "Same as..." reference chain to a concrete config (for previews/queries).
    LayerBgConfig resolveConfig(std::string const& layerKey) const;

    // DEPRECATED stub (multi-video now supported); always returns an empty string.
    std::string hasOtherVideoConfigured(std::string const& excludeLayerKey, std::string const& videoPath) const {
        (void)excludeLayerKey;
        (void)videoPath;
        return {};
    }

    // Music per-layer (legacy, kept for migration)
    LayerMusicConfig getMusicConfig(std::string const& layerKey) const;
    void saveMusicConfig(std::string const& layerKey, LayerMusicConfig const& cfg);

    // Global music (replaces per-layer: one config for ALL layers)
    LayerMusicConfig getGlobalMusicConfig() const;
    void saveGlobalMusicConfig(LayerMusicConfig const& cfg);

    // All supported layers (key, displayName)
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

    // Migrate legacy saved values (bg-type, bg-custom-path, etc.) to the unified
    // layerbg-* format. Runs once.
    void migrateFromLegacy();

    // Import old external paths into the mod saveDir to avoid missing-file errors
    // and unify local assets.
    void migrateExternalAssetsToManagedStorage();

    // Migrate per-layer music saved values to the global format.
    void migrateToGlobalMusic();

    // Apply a video background to a layer (public so MenuLayer can call it directly)
    void applyVideoBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    // Apply a procedural GPU-only shader background.
    void applyProceduralShaderBg(cocos2d::CCLayer* layer, LayerBgConfig const& cfg);

    // Remove the currently applied layer background and stop any active video/audio.
    void clearAppliedBackground(cocos2d::CCLayer* layer, bool suppressAudioResume = false);

private:
    LayerBackgroundManager() = default;

    // helpers
    void hideOriginalBg(cocos2d::CCLayer* layer);
    cocos2d::CCTexture2D* loadTextureForConfig(LayerBgConfig const& cfg);
    void applyStaticBg(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* tex, LayerBgConfig const& cfg);
    void applyGifBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    // Shared video player cache for "Same As" reuse: layers resolving to the same
    // path share one decoded VideoPlayer instead of duplicating the pipeline.
    // kSharedVideoTTL: grace period to keep a released player warm (paused) before
    // eviction, so a quick re-acquire resumes instantly. 1500ms balances transition
    // latency against idle RAM.
    static constexpr auto kSharedVideoTTL = std::chrono::milliseconds(1500);

    struct SharedVideoEntry {
        std::shared_ptr<paimon::video::VideoPlayer> player;
        int refCount = 0;
        // At refCount 0 the decoder keeps running; mark stale so the next acquire
        // creates a fresh player rather than reusing a possibly desynced one.
        bool stale = false;
        // TTL expiry for eviction once refCount reaches 0.
        std::chrono::steady_clock::time_point expiry =
            std::chrono::steady_clock::time_point::max();
        // Last acquire / refCount touch; used by the LRU eviction policy.
        std::chrono::steady_clock::time_point lastUsed =
            std::chrono::steady_clock::now();
    };
    std::unordered_map<std::string, SharedVideoEntry> m_sharedVideos;
    std::unordered_map<std::string, int> m_pendingSharedVideoCreates;
    mutable std::mutex m_sharedVideosMutex;  // Protects m_sharedVideos across threads, including const lookups

    // Multi-video budget helpers (private)
    // Active (refCount > 0, non-stale) entry count. Caller must hold m_sharedVideosMutex.
    int activeVideoCount_locked() const;

    // Per-video target FPS for the given active count (clamped fpsLimit/count when adaptive).
    int adaptiveFPSForCount(int activeCount) const;

    // Re-apply adaptiveFPSForCount() to every active player. Caller must hold m_sharedVideosMutex.
    void rebalanceAdaptiveFPS_locked();

    // Evict LRU inactive entries until under maxConcurrentVideos; returns players to
    // tear down outside the lock. Caller must hold m_sharedVideosMutex.
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>>
        evictLRUForBudget_locked(std::string const& reservedPath, int maxConcurrent);

public:
    // Get or create a shared video player for the given path.
    // Returns nullptr if the path is empty or loading fails.
    std::shared_ptr<paimon::video::VideoPlayer> acquireSharedVideo(
        std::string const& path, bool requireCanonicalAudio);

    // Release a reference to a shared video player.
    // When refCount drops to 0, the player enters a TTL grace period
    // (kSharedVideoTTL) before being destroyed.
    void releaseSharedVideo(std::string const& path);

    // Evict shared video entries whose TTL has expired.
    // Called lazily from acquireSharedVideo.
    void evictExpiredSharedVideos();

    // Release all shared video players immediately. Call during $on_game(Exiting)
    // before MF shuts down, else the atexit singleton destructor crashes msmpeg2vdec.dll.
    void releaseAllSharedVideos();

    // Force-release and evict a shared player, bypassing the TTL (e.g. when a layer's
    // config changes away from a video) to free its decoder/RAM eagerly.
    void forceReleaseSharedVideoByPath(std::string const& path);

    // Force-evict all stale shared video entries immediately, bypassing TTL.
    // Useful when switching to "default" to ensure no leftover players block
    // new video acquisitions.
    void forceEvictAllStaleVideos();

    // Stop video-bg audio without destroying players or removing visuals, so a layer
    // that needs the audio channel (e.g. LevelInfoLayer's dynamic song) can grab it on
    // the next frame. Idempotent; clears the global flag and stops the FMOD channel synchronously.
    void releaseAllVideoAudio();

    // Check if a shared video player already exists for the given path.
    bool hasSharedVideo(std::string const& path) const;

    // Check if a shared video player exists and is safe to reuse immediately.
    bool canReuseSharedVideo(std::string const& path) const;

    // Get total estimated RAM used by all shared video players (bytes).
    size_t getTotalVideoRAMBytes() const;

    // Instantly update the playback FPS on all currently active shared video players.
    // Call this whenever the video-fps-limit setting changes.
    void broadcastFPSUpdate(int newFPS);

    // Instantly rotate all active video background containers.
    // Call this whenever the video-rotation setting changes.
    void broadcastRotationUpdate(int newRotationDegrees);

    // Delete the disk cache for the layer's current video background if
    // it differs from nextVideoPath.  Call before clearAppliedBackground
    // so the old video path is still accessible.
    void cleanupOldVideoCache(cocos2d::CCLayer* layer, std::string const& nextVideoPath);

    // Video background first-frame preview cache
    // Saves the first decoded frame to disk so it can be displayed instantly
    // on the next launch while the video decoder re-initialises.

    // Returns the save directory for video preview files.
    static std::filesystem::path getVideoBgPreviewDir();

    // Returns the full path for the preview file for the given video path.
    static std::filesystem::path getVideoBgPreviewPath(std::string const& videoPath);

    // Loads a previously-saved preview. Returns false if the file does not
    // exist or if the source video is newer than the preview (stale cache).
    static bool loadVideoBgPreview(std::string const& videoPath,
                                   std::vector<uint8_t>& outPixels,
                                   int& outW, int& outH);

    // Copies the first RGBA frame from the player and saves it to disk in
    // a background thread so the main thread is never blocked.
    static void saveVideoBgPreview(std::string const& videoPath,
                                   paimon::video::VideoPlayer const* player);
};
