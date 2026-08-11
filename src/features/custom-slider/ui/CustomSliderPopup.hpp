#pragma once
#include <Geode/Geode.hpp>
#include "../services/CustomSliderManager.hpp"

namespace paimon::slider {

// Popup de configuracion del slider personalizado, montado sobre PaiConfigKit.
// Una sola lista de tarjetas; las opciones que dependen del modo (Icono vs
// Imagen/GIF) aparecen o desaparecen reconstruyendo el contenido.
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

    // Reconstruye el contenido scrolleable (cambios de modo/marco).
    void rebuild();
    // Igual que rebuild() pero diferido al siguiente tick, para no mutar la
    // escena dentro del touch dispatcher.
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
