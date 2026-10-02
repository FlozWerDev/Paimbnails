#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>

#include "../services/BeatShaderManager.hpp"

#include <vector>
#include <string>

namespace paimon::beat_shaders {

class BeatShaderConfigLayer : public PaimonPopup {
public:
    static BeatShaderConfigLayer* create();

protected:
    bool init() override;

    void rebuild();
    void scheduleRebuild();
    void persistAndRefresh(bool shaderChanged);

private:
    BeatShaderConfig m_cfg;
    std::vector<BeatShaderManager::ShaderEntry> m_shaders;
    int m_shaderIdx = 0;
    std::vector<std::string> m_layerKeys;
    int m_tab = 0; // 0 = basico, 1 = avanzado

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_shaderDescLabel = nullptr;
};

} // namespace paimon::beat_shaders
