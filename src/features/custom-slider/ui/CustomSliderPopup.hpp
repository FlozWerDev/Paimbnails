#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include "../services/CustomSliderManager.hpp"

#include <vector>

namespace paimon::slider {

// slider config popup on paiconfigkit; mode-dependent options rebuild content.
class CustomSliderPopup : public PaimonPopup {
public:
    static CustomSliderPopup* create();

protected:
    bool init() override;
    void onExit() override;

    geode::ScrollLayer*     m_scroll         = nullptr;
    cocos2d::CCNode*        m_previewNode    = nullptr;
    cocos2d::CCNode*        m_previewContent = nullptr;
    cocos2d::CCMenu*        m_shapeGridMenu  = nullptr;
    int                     m_tab            = 0; // 0 = basico, 1 = avanzado
    float                   m_previewScalePerUnit = 1.f;
    bool                    m_sliderRefreshPending = false;

    // rebuilds scroll content (mode/frame changes).
    void rebuild();
    // one card per tab; only the visible one builds.
    std::vector<cocos2d::CCNode*> buildBasicCards(float scrollW, float innerW);
    std::vector<cocos2d::CCNode*> buildAdvancedCards(float scrollW, float innerW);
    // same as rebuild() but next tick: never mutate the scene inside touch dispatch.
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
