#pragma once
#include <Geode/Geode.hpp>
#include "../services/CustomSliderManager.hpp"

#include <vector>

namespace paimon::slider {

// Slider config popup on PaiConfigKit; mode-dependent options rebuild content.
class CustomSliderPopup : public geode::Popup {
public:
    static CustomSliderPopup* create();

protected:
    bool init() override;
    void onExit() override;

    geode::ScrollLayer*     m_scroll         = nullptr;
    cocos2d::CCNode*        m_previewNode    = nullptr;
    cocos2d::CCNode*        m_previewContent = nullptr;
    cocos2d::CCMenu*        m_shapeGridMenu  = nullptr;
    int                     m_tab            = 0; // 0 = Basico, 1 = Avanzado
    float                   m_previewScalePerUnit = 1.f;
    bool                    m_sliderRefreshPending = false;

    // Rebuilds scroll content (mode/frame changes).
    void rebuild();
    // One card per tab; only the visible one builds.
    std::vector<cocos2d::CCNode*> buildBasicCards(float scrollW, float innerW);
    std::vector<cocos2d::CCNode*> buildAdvancedCards(float scrollW, float innerW);
    // Same as rebuild() but next tick: never mutate the scene inside touch dispatch.
    void scheduleRebuild();
    void scheduleSliderRefresh();
    void applySliderRefresh(float);

    void refreshPreview();
    void updatePreviewScale();
    void reapplyAllSliders();
    void rebuildShapeGrid();
    void onPickImage();
};

} // namespace paimon::slider
