#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>
#include "../services/ProfilePicCustomizer.hpp"

class ProfilePicEditorPopup : public geode::Popup {
protected:
    // Configuracion en edicion
    ProfilePicConfig m_editConfig;

    // Preview
    cocos2d::CCNode* m_previewContainer = nullptr;

    // Tabs activos
    cocos2d::CCNode* m_tabContent = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
    int m_currentTab = 0;

    // Controles de borde
    Slider* m_thicknessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_thicknessLabel = nullptr;
    Slider* m_frameOpacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_frameOpacityLabel = nullptr;

    // Controles de forma
    Slider* m_scaleXSlider = nullptr;
    Slider* m_scaleYSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    Slider* m_rotationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleXLabel = nullptr;
    cocos2d::CCLabelBMFont* m_scaleYLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sizeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_rotationLabel = nullptr;

    // Tab de decoraciones
    int m_decoCategoryIdx = 0;     // Indice de categoria
    int m_decoPage = 0;            // Pagina actual
    int m_selectedDecoIdx = -1;    // Decoracion seleccionada

    // Controles de ajuste
    Slider* m_imgZoomSlider = nullptr;
    Slider* m_imgRotationSlider = nullptr;
    Slider* m_imgOffsetXSlider = nullptr;
    Slider* m_imgOffsetYSlider = nullptr;
    cocos2d::CCLabelBMFont* m_imgZoomLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgRotationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgOffsetXLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgOffsetYLabel = nullptr;

    // Controles de decoracion individual
    Slider* m_decoScaleSlider = nullptr;
    Slider* m_decoRotSlider = nullptr;
    Slider* m_decoPosXSlider = nullptr;
    Slider* m_decoPosYSlider = nullptr;
    Slider* m_decoOpacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_decoScaleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_decoRotLabel = nullptr;
    cocos2d::CCLabelBMFont* m_decoPosXLabel = nullptr;
    cocos2d::CCLabelBMFont* m_decoPosYLabel = nullptr;
    cocos2d::CCLabelBMFont* m_decoOpacityLabel = nullptr;

    // Textura de preview retenida
    geode::Ref<cocos2d::CCTexture2D> m_previewTexture;

    // Descarga la imagen si no esta en cache
    bool m_triggeredDownload = false;

    bool init();

    // Gestion de tabs
    void createTabs();
    void switchTab(int tab);
    void onTabBtn(cocos2d::CCObject* sender);
    void rebuildCurrentTab();

    // Tab de borde
    cocos2d::CCNode* createFrameTab();
    void onFrameToggle(cocos2d::CCObject* sender);
    void onThicknessChanged(cocos2d::CCObject* sender);
    void onFrameOpacityChanged(cocos2d::CCObject* sender);
    void onBorderColorSelect(cocos2d::CCObject* sender);
    void onPickCustomBorderColor(cocos2d::CCObject* sender);

    // Tab de forma
    cocos2d::CCNode* createShapeTab();
    void onScaleXChanged(cocos2d::CCObject* sender);
    void onScaleYChanged(cocos2d::CCObject* sender);
    void onSizeChanged(cocos2d::CCObject* sender);
    void onRotationChanged(cocos2d::CCObject* sender);
    void onStencilSelect(cocos2d::CCObject* sender);
    void onResetShape(cocos2d::CCObject* sender);

    // Tab de decoraciones
    cocos2d::CCNode* createDecoTab();
    void onCategorySelect(cocos2d::CCObject* sender);
    void onAddDeco(cocos2d::CCObject* sender);
    void onDecoPage(cocos2d::CCObject* sender);
    void onSelectPlacedDeco(cocos2d::CCObject* sender);
    void onDecoScaleChanged(cocos2d::CCObject* sender);
    void onDecoRotationChanged(cocos2d::CCObject* sender);
    void onDecoPosXChanged(cocos2d::CCObject* sender);
    void onDecoPosYChanged(cocos2d::CCObject* sender);
    void onDecoOpacityChanged(cocos2d::CCObject* sender);
    void onDecoFlipX(cocos2d::CCObject* sender);
    void onDecoFlipY(cocos2d::CCObject* sender);
    void onDecoZUp(cocos2d::CCObject* sender);
    void onDecoZDown(cocos2d::CCObject* sender);
    void onDecoDelete(cocos2d::CCObject* sender);
    void onDecoDuplicate(cocos2d::CCObject* sender);
    void onDecoColorSelect(cocos2d::CCObject* sender);
    void onDecoPickColor(cocos2d::CCObject* sender);
    void onClearAllDecos(cocos2d::CCObject* sender);

    // Tab de ajustes
    cocos2d::CCNode* createAdjustTab();
    void onImgZoomChanged(cocos2d::CCObject* sender);
    void onImgRotationChanged(cocos2d::CCObject* sender);
    void onImgOffsetXChanged(cocos2d::CCObject* sender);
    void onImgOffsetYChanged(cocos2d::CCObject* sender);
    void onResetAdjust(cocos2d::CCObject* sender);

    // Barra de herramientas
    void onPreset(cocos2d::CCObject* sender);
    void onRandomize(cocos2d::CCObject* sender);
    void onResetAll(cocos2d::CCObject* sender);

    // Preview
    void rebuildPreview();
    void triggerImageDownloadIfNeeded();

    // Acciones
    void onSave(cocos2d::CCObject* sender);

public:
    static ProfilePicEditorPopup* create();
};
