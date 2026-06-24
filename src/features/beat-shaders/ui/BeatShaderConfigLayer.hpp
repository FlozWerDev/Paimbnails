// Popup UI for audio-reactive beat shader config.
#pragma once

#include <Geode/Geode.hpp>

#include "../services/BeatShaderManager.hpp"

#include <vector>
#include <string>

namespace paimon::beat_shaders {

class BeatShaderConfigLayer : public geode::Popup {
public:
    static BeatShaderConfigLayer* create();

protected:
    bool init();

    void buildHeader();
    void buildShaderPicker();
    void buildSensitivitySliders();
    void buildIntensityRow();
    void buildLayerToggles();
    void buildFooter();

    void refreshShaderLabel();
    void persistAndRefresh(bool shaderChanged);

    void onToggleEnabled(cocos2d::CCObject*);
    void onPrevShader(cocos2d::CCObject*);
    void onNextShader(cocos2d::CCObject*);
    void onIntensityChanged(cocos2d::CCObject*);
    void onBassChanged(cocos2d::CCObject*);
    void onMidChanged(cocos2d::CCObject*);
    void onTrebleChanged(cocos2d::CCObject*);
    void onBeatChanged(cocos2d::CCObject*);
    void onToggleLayer(cocos2d::CCObject*);
    void onResetDefaults(cocos2d::CCObject*);

private:
    BeatShaderConfig m_cfg;
    std::vector<BeatShaderManager::ShaderEntry> m_shaders;
    int m_shaderIdx = 0;

    cocos2d::CCLabelBMFont* m_shaderNameLabel = nullptr;
    cocos2d::CCLabelBMFont* m_shaderDescLabel = nullptr;

    Slider* m_intensitySlider = nullptr;
    Slider* m_bassSlider      = nullptr;
    Slider* m_midSlider       = nullptr;
    Slider* m_trebleSlider    = nullptr;
    Slider* m_beatSlider      = nullptr;

    CCMenuItemToggler* m_enabledToggle = nullptr;

    std::vector<CCMenuItemToggler*> m_layerToggles;
    std::vector<std::string>        m_layerKeys;
};

} // namespace paimon::beat_shaders
