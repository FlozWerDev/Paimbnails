#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include "../services/DynamicTransitionManager.hpp"
#include <vector>

class PaimonDrawNode;

namespace paimon::transitions::dynamic {

class Visual;

// Looping miniature of the real compositor, with the active curve plotted underneath.
class ConfigPreview : public cocos2d::CCNode {
public:
    static ConfigPreview* create(float width);
    void setConfig(Config const& config);
    void setPanelMode(bool panel);
    bool panelMode() const { return m_panelMode; }
    void setLooping(bool looping);
    bool looping() const { return m_looping; }
    void setSlow(bool slow) { m_slow = slow; }
    bool slow() const { return m_slow; }
    void play(bool backwards);
    void update(float dt) override;

private:
    bool setup(float width);
    void captureScreens();
    void rebuildVisuals();
    void drawCurve();
    void present();
    float phaseDuration(int phase) const;
    cocos2d::CCPoint graphPoint(float t, float value) const;

    Config m_config, m_motion;
    cocos2d::CCSize m_stage;
    Rect m_button;
    float m_quality = -1.f;
    float m_time = 0.f;
    int m_phase = 0;
    bool m_panelMode = false, m_looping = true, m_slow = false, m_playing = true, m_instant = false;
    bool m_firstStep = true;
    geode::Ref<cocos2d::CCTexture2D> m_menu, m_layer, m_panel;
    cocos2d::CCNode* m_clip = nullptr;
    Visual* m_open = nullptr;
    Visual* m_back = nullptr;
    PaimonDrawNode* m_curve = nullptr;
    PaimonDrawNode* m_marker = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCLabelBMFont* m_curveName = nullptr;
    float m_curveLow = 0.f, m_curveHigh = 1.f;
};

class DynamicTransitionConfigPopup : public PaimonPopup {
public:
    static DynamicTransitionConfigPopup* create();

protected:
    bool init() override;

private:
    void buildPreviewControls();
    void rebuild();
    void scheduleRebuild();
    void persist();
    void choosePreset(int preset);
    void refreshPresetState();
    void refreshControlSkins();
    void startFullscreenTest(CCMenuItemSpriteExtra* source);

    Config m_config;
    ConfigPreview* m_preview = nullptr;
    cocos2d::CCNode* m_header = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_presetLabel = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_presetButtons;
    CCMenuItemSpriteExtra* m_loopButton = nullptr;
    CCMenuItemSpriteExtra* m_slowButton = nullptr;
    int m_tab = 0;
    int m_scrollTab = -1;
    bool m_rebuildQueued = false;
};

}
