// Postprocess beat-shader config singleton.
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

    // Apply the beat shader to the layer's existing background by forcing its
    // LayerBgConfig.shader and re-applying. No-op when disabled; layer may be null
    // (then only the saved-value side updates, applied when the layer next mounts).
    void applyToLayer(cocos2d::CCLayer* layer, std::string const& layerKey);

    // Update every ShaderBgSprite's audio-reactive uniforms from the live config,
    // for instant feedback on slider changes without rebuilding the background.
    void refreshLiveSpriteUniforms();

    // Re-mount backgrounds on every supported layer when the chosen shader changed
    // (a different shader requires LayerBackgroundManager to rebuild the sprite).
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
    bool isShuttingDown() const { return m_shuttingDown; }

private:
    BeatShaderManager() = default;
    BeatShaderManager(BeatShaderManager const&) = delete;
    BeatShaderManager& operator=(BeatShaderManager const&) = delete;

    void activateAudioIfNeeded();
    void deactivateAudioIfUnused();

    bool m_audioActive = false;
    bool m_shuttingDown = false;
};

} // namespace paimon::beat_shaders
