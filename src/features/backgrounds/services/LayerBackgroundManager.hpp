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

// ════════════════════════════════════════════════════════════
// LayerBackgroundManager — aplica fondos personalizados por layer
// Unificado: incluye Menu, LevelInfo, Profile y todos los layers
// Saved value pattern: "layerbg-{key}-type", "layerbg-{key}-path"
// ════════════════════════════════════════════════════════════

struct LayerBgConfig {
    std::string type = "default";   // "default", "custom", "random", "menu", "id", "video"
    std::string customPath;         // ruta imagen/GIF/video
    int levelId = 0;                // para tipo "id"
    bool darkMode = false;
    float darkIntensity = 0.5f;
    std::string shader = "none";    // "none","grayscale","sepia","vignette","bloom","chromatic","pixelate","posterize","scanlines"
};

// Configuracion de musica per-layer
struct LayerMusicConfig {
    std::string mode = "default";   // "default", "newgrounds", "custom", "dynamic"
    int songID = 0;                 // Newgrounds song ID
    std::string customPath;         // ruta a archivo audio local
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

    // Aplica fondo al layer segun su key. Llama despues de super::init().
    // Retorna true si se aplico un fondo custom (para que el hook oculte UI extra).
    bool applyBackground(cocos2d::CCLayer* layer, std::string const& layerKey);

    // Consulta rapida: ¿este layer tiene un fondo custom configurado? (no aplica nada)
    bool hasCustomBackground(std::string const& layerKey) const;

    // Lee config de Mod saved values para una key
    LayerBgConfig getConfig(std::string const& layerKey) const;

    // Guarda config a Mod saved values
    void saveConfig(std::string const& layerKey, LayerBgConfig const& cfg);

    // Resuelve la cadena de referencias (Same as...) y retorna la config final concreta.
    // util para previews y consultas sin aplicar nada.
    LayerBgConfig resolveConfig(std::string const& layerKey) const;

    // Verifica si hay otro layer con un video configurado (diferente path).
    // Retorna el nombre del layer que tiene el video, o empty string si no hay.
    std::string hasOtherVideoConfigured(std::string const& excludeLayerKey, std::string const& videoPath) const;

    // ── Music per-layer (legacy, kept for migration) ──
    LayerMusicConfig getMusicConfig(std::string const& layerKey) const;
    void saveMusicConfig(std::string const& layerKey, LayerMusicConfig const& cfg);

    // ── Global music (replaces per-layer: one config for ALL layers) ──
    LayerMusicConfig getGlobalMusicConfig() const;
    void saveGlobalMusicConfig(LayerMusicConfig const& cfg);

    // Todos los layers soportados (key, displayName)
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

    // Migra saved values del formato legacy (bg-type, bg-custom-path, etc.)
    // al nuevo formato unificado layerbg-*. Solo la primera vez.
    void migrateFromLegacy();

    // Migra saved values de music per-layer al formato global.
    void migrateToGlobalMusic();

    // Apply a video background to a layer (public so MenuLayer can call it directly)
    void applyVideoBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    // Remove the currently applied layer background and stop any active video/audio.
    void clearAppliedBackground(cocos2d::CCLayer* layer, bool suppressAudioResume = false);

private:
    LayerBackgroundManager() = default;

    // helpers
    void hideOriginalBg(cocos2d::CCLayer* layer);
    cocos2d::CCTexture2D* loadTextureForConfig(LayerBgConfig const& cfg);
    void applyStaticBg(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* tex, LayerBgConfig const& cfg);
    void applyGifBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);

    // ── Shared video player cache for "Same As" reuse ──
    // When multiple layers resolve to the same video path, this cache
    // lets subsequent layers share the already-loaded VideoPlayer's texture
    // instead of creating a redundant decoder pipeline.
    // How long to keep a shared video player's shared_ptr alive after its
    // last reference is released.  The player itself is stopped immediately
    // (releasing decode threads + YUV buffers), but the shared_ptr is kept
    // briefly so that a quick re-acquire can restart playback via disk cache
    // instead of re-creating the player from scratch.
    static constexpr auto kSharedVideoTTL = std::chrono::seconds(3);

    struct SharedVideoEntry {
        std::shared_ptr<paimon::video::VideoPlayer> player;
        int refCount = 0;
        // When refCount drops to 0, the player is left running in the
        // background (we can't stop the DXVA decoder thread safely).
        // Mark it stale so the next acquire creates a fresh player
        // instead of reusing a potentially desynced one.
        bool stale = false;
        // TTL expiry for eviction when refCount reaches 0.
        std::chrono::steady_clock::time_point expiry =
            std::chrono::steady_clock::time_point::max();
    };
    std::unordered_map<std::string, SharedVideoEntry> m_sharedVideos;
    mutable std::mutex m_sharedVideosMutex;  // Protects m_sharedVideos across threads, including const lookups

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

    // Release all shared video players immediately.
    // Must be called during $on_game(Exiting) before MF shuts down,
    // otherwise the static singleton destructor runs during atexit
    // after MF is already torn down, causing msmpeg2vdec.dll crashes.
    void releaseAllSharedVideos();

    // Check if a shared video player already exists for the given path.
    bool hasSharedVideo(std::string const& path) const;

    // Check if a shared video player exists and is safe to reuse immediately.
    bool canReuseSharedVideo(std::string const& path) const;

    // Get total estimated RAM used by all shared video players (bytes).
    size_t getTotalVideoRAMBytes() const;

    // Instantly update the playback FPS on all currently active shared video players.
    // Call this whenever the video-fps-limit setting changes.
    void broadcastFPSUpdate(int newFPS);

    // Delete the disk cache for the layer's current video background if
    // it differs from nextVideoPath.  Call before clearAppliedBackground
    // so the old video path is still accessible.
    void cleanupOldVideoCache(cocos2d::CCLayer* layer, std::string const& nextVideoPath);

    // ── Video background first-frame preview cache ─────────────────────────
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

