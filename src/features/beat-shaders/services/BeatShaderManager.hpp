// BeatShaderManager — singleton owning the global "Beat Shaders" config.
//
// Mode of operation (postprocess, NOT overlay):
//   The mod's existing background pipeline (LayerBackgroundManager + the
//   per-layer hooks that call applyBackground) already routes any background
//   sprite — image, video frame, GIF — through a ShaderBgSprite that runs
//   `cfg.shader` over u_texture. Beat Shaders piggybacks on that pipeline:
//   when enabled, it forces `cfg.shader` to one of the *_beat.glsl variants
//   and flips the audio-reactive flag on every ShaderBgSprite already in the
//   scene so the FFT spectrum drives the chosen distortion.
//
// We do not hook the layer's init() any more; instead we listen for changes
// on the saved config and re-apply by traversing the active scene.
#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <utility>

namespace paimon::beat_shaders {

struct BeatShaderConfig {
    bool        enabled       = false;
    std::string shaderName    = "glitch-beat";
    float       intensity     = 0.7f;     // 0..1
    float       bassMult      = 1.0f;     // 0..3
    float       midMult       = 1.0f;
    float       trebleMult    = 1.0f;
    float       beatMult      = 1.0f;
    float       energyMult    = 1.0f;
};

class BeatShaderManager {
public:
    static BeatShaderManager& get();

    BeatShaderConfig getConfig() const;
    void saveConfig(BeatShaderConfig const& cfg);

    bool isLayerEnabled(std::string const& layerKey) const;
    void setLayerEnabled(std::string const& layerKey, bool enabled);

    // Applies the beat shader to the given layer's existing background by
    // forcing the LayerBgConfig.shader for that layer key and re-applying.
    // Safe to call from anywhere on the main thread; no-ops when feature is
    // disabled. The layer parameter is optional — when null, only the
    // saved-value side of things is updated and the next time the scene
    // mounts the layer it will pick the right shader.
    void applyToLayer(cocos2d::CCLayer* layer, std::string const& layerKey);

    // Walks the running scene, finds every ShaderBgSprite descendant and
    // updates its m_audioReactive / m_*Mult based on the live config. Used
    // by the config UI to give immediate feedback on slider changes without
    // rebuilding the background.
    void refreshLiveSpriteUniforms();

    // Re-mount backgrounds on every supported layer in the running scene
    // when the chosen shader changed (a different shader requires
    // LayerBackgroundManager to rebuild the sprite, since the program
    // reference is captured at apply-time).
    void rebuildBackgrounds();

    struct ShaderEntry {
        std::string id;
        std::string label;
        std::string description;
    };
    std::vector<ShaderEntry> availableShaders() const;
    std::vector<std::pair<std::string, std::string>> availableLayers() const;

    void init();
    void shutdown();

private:
    BeatShaderManager() = default;
    BeatShaderManager(BeatShaderManager const&) = delete;
    BeatShaderManager& operator=(BeatShaderManager const&) = delete;

    void activateAudioIfNeeded();
    void deactivateAudioIfUnused();

    bool m_audioActive = false;
};

} // namespace paimon::beat_shaders
