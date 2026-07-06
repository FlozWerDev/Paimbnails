// Popup UI for audio-reactive beat shader config.
#pragma once

#include <Geode/Geode.hpp>

#include "../services/BeatShaderManager.hpp"

#include <vector>
#include <string>

namespace paimon::beat_shaders {

// Popup de configuracion montado sobre PaiConfigKit: interruptor grande,
// selector de estilo con descripcion, sliders con valor visible y tarjeta
// de pantallas donde se aplica.
class BeatShaderConfigLayer : public geode::Popup {
public:
    static BeatShaderConfigLayer* create();

protected:
    bool init() override;

    void rebuild();
    void persistAndRefresh(bool shaderChanged);

private:
    BeatShaderConfig m_cfg;
    std::vector<BeatShaderManager::ShaderEntry> m_shaders;
    int m_shaderIdx = 0;
    std::vector<std::string> m_layerKeys;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_shaderDescLabel = nullptr;
};

} // namespace paimon::beat_shaders
