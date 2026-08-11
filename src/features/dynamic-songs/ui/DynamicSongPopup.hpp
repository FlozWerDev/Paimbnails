// Config popup for Dynamic Song, built on PaiConfigKit.
#pragma once

#include <Geode/Geode.hpp>

#include "../services/DynamicSongConfig.hpp"

namespace paimon::dynsong {

class DynamicSongPopup : public geode::Popup {
public:
    static DynamicSongPopup* create();

protected:
    bool init() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;
    void onExit() override;

    void rebuild();
    void scheduleRebuild();
    void persist();

    // Runs the dive and the surface back to back on whatever is playing, so
    // the sliders can be judged by ear instead of by number.
    void previewDive();
    void previewSurface(float dt);

    cocos2d::CCNode* makeStatusRow(float width);
    void refreshStatus();

private:
    DynamicSongConfig m_cfg{};
    int m_tab = 0; // 0 = Basico, 1 = Buceo, 2 = Avanzado

    geode::ScrollLayer* m_scroll = nullptr;
    float m_scrollTargetY = 0.f;
    bool m_scrollTargetSet = false;

    cocos2d::CCLabelBMFont* m_statusMain = nullptr;
    cocos2d::CCLabelBMFont* m_statusSub = nullptr;

    float m_uiClock = 0.f;
    float m_lastStatusRefresh = -1.f;
    bool m_previewing = false;
};

} // namespace paimon::dynsong
