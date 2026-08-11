// Config popup for Dynamic Volume, built on PaiConfigKit.
#pragma once

#include <Geode/Geode.hpp>

#include "../services/DynamicVolumeManager.hpp"

namespace paimon::dynvol {

class DynamicVolumePopup : public geode::Popup {
public:
    static DynamicVolumePopup* create();

protected:
    bool init() override;
    void update(float dt) override;

    void rebuild();
    void scheduleRebuild();
    void persist();

    // Curve preview: the ramp from the ducked floor back to full volume, plus a
    // playhead when a song is actually ramping right now.
    cocos2d::CCNode* makeCurvePreview(float width);
    void redrawCurve();

    cocos2d::CCNode* makeLiveMeter(float width);
    void refreshLiveMeter();

private:
    DynamicVolumeConfig m_cfg{};
    int m_tab = 0; // 0 = Basico, 1 = Curva, 2 = Avanzado

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_modeDescLabel = nullptr;

    cocos2d::CCDrawNode*    m_curveDraw    = nullptr;
    cocos2d::CCLabelBMFont* m_curveCaption = nullptr;
    cocos2d::CCSize         m_curveArea    = {0.f, 0.f};

    cocos2d::CCLabelBMFont* m_meterMain = nullptr;
    cocos2d::CCLabelBMFont* m_meterSub  = nullptr;

    // The live parts only need a few refreshes per second; redrawing 64 curve
    // segments and rebuilding two labels every frame is pure waste.
    float m_uiClock          = 0.f;
    float m_lastMeterRefresh = -1.f;
    float m_lastCurveHead    = -2.f;
    bool  m_curveDirty       = true;
};

} // namespace paimon::dynvol
