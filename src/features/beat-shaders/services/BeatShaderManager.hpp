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

    // Forces LayerBgConfig.shader and re-applies; null layer only updates the saved value.
    void applyToLayer(cocos2d::CCLayer* layer, std::string const& layerKey);

    // Pushes live-config audio uniforms to ShaderBgSprites for instant slider feedback.
    void refreshLiveSpriteUniforms();

    // Re-mounts backgrounds when the shader changed (LayerBackgroundManager rebuilds the sprite).
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
