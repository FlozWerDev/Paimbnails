#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/Slider.hpp>

class ProgressBarConfigPopup : public geode::Popup {
public:
    static ProgressBarConfigPopup* create();

protected:
    bool init() override;
    void onExit() override;

    // tabs
    int m_currentTab = 0;
    cocos2d::CCNode* m_generalTab = nullptr;
    cocos2d::CCNode* m_positionTab = nullptr;
    cocos2d::CCNode* m_colorsTab = nullptr;
    cocos2d::CCNode* m_labelTab = nullptr;
    cocos2d::CCNode* m_fxTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    // sliders
    Slider* m_posXSlider = nullptr;
    cocos2d::CCLabelBMFont* m_posXLabel = nullptr;
    Slider* m_posYSlider = nullptr;
    cocos2d::CCLabelBMFont* m_posYLabel = nullptr;
    Slider* m_scaleLenSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLenLabel = nullptr;
    Slider* m_scaleThickSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleThickLabel = nullptr;
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    Slider* m_pctScaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_pctScaleLabel = nullptr;
    Slider* m_pctOffXSlider = nullptr;
    cocos2d::CCLabelBMFont* m_pctOffXLabel = nullptr;
    Slider* m_pctOffYSlider = nullptr;
    cocos2d::CCLabelBMFont* m_pctOffYLabel = nullptr;

    // toggles
    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_verticalToggle = nullptr;
    CCMenuItemToggler* m_useCustomPosToggle = nullptr;
    CCMenuItemToggler* m_freeDragToggle = nullptr;
    CCMenuItemToggler* m_useFillColorToggle = nullptr;
    CCMenuItemToggler* m_useBgColorToggle = nullptr;
    CCMenuItemToggler* m_showPctToggle = nullptr;
    CCMenuItemToggler* m_usePctColorToggle = nullptr;

    // color previews
    cocos2d::CCLayerColor* m_fillColorPreview = nullptr;
    cocos2d::CCLayerColor* m_bgColorPreview = nullptr;
    cocos2d::CCLayerColor* m_pctColorPreview = nullptr;

    // FX/Textures UI
    Slider* m_colorAnimSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_colorAnimSpeedLabel = nullptr;
    CCMenuItemSpriteExtra* m_fillModeBtn = nullptr;
    CCMenuItemSpriteExtra* m_bgModeBtn   = nullptr;
    CCMenuItemSpriteExtra* m_pctModeBtn  = nullptr;
    cocos2d::CCLayerColor* m_fillColor2Preview = nullptr;
    cocos2d::CCLayerColor* m_bgColor2Preview   = nullptr;
    cocos2d::CCLayerColor* m_pctColor2Preview  = nullptr;
    CCMenuItemToggler* m_useFillTexToggle = nullptr;
    CCMenuItemToggler* m_useBgTexToggle   = nullptr;
    cocos2d::CCLabelBMFont* m_fillTexPathLabel = nullptr;
    cocos2d::CCLabelBMFont* m_bgTexPathLabel   = nullptr;

    // tab setup helpers
    void createTabButtons();
    void onTabSwitch(cocos2d::CCObject* sender);
    void buildGeneralTab();
    void buildPositionTab();
    void buildColorsTab();
    void buildLabelTab();
    void buildFxTab();
    void refreshFxTab(); // updates mode button labels + path text

    // callbacks
    void onEnableToggled(cocos2d::CCObject*);
    void onVerticalToggled(cocos2d::CCObject*);
    void onUseCustomPosToggled(cocos2d::CCObject*);
    void onFreeDragToggled(cocos2d::CCObject*);
    void onUseFillColorToggled(cocos2d::CCObject*);
    void onUseBgColorToggled(cocos2d::CCObject*);
    void onShowPctToggled(cocos2d::CCObject*);
    void onUsePctColorToggled(cocos2d::CCObject*);
    void onPickFillColor(cocos2d::CCObject*);
    void onPickBgColor(cocos2d::CCObject*);
    void onPickPctColor(cocos2d::CCObject*);
    void onPickFont(cocos2d::CCObject*);
    void onEnterFreeEditMode(cocos2d::CCObject*);
    void onResetDefaults(cocos2d::CCObject*);
    void onCenterPosition(cocos2d::CCObject*);

    // slider callbacks
    void onPosXChanged(cocos2d::CCObject*);
    void onPosYChanged(cocos2d::CCObject*);
    void onScaleLenChanged(cocos2d::CCObject*);
    void onScaleThickChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onPctScaleChanged(cocos2d::CCObject*);
    void onPctOffXChanged(cocos2d::CCObject*);
    void onPctOffYChanged(cocos2d::CCObject*);

    // FX callbacks
    void onCycleFillMode(cocos2d::CCObject*);
    void onCycleBgMode(cocos2d::CCObject*);
    void onCyclePctMode(cocos2d::CCObject*);
    void onPickFillColor2(cocos2d::CCObject*);
    void onPickBgColor2(cocos2d::CCObject*);
    void onPickPctColor2(cocos2d::CCObject*);
    void onColorAnimSpeedChanged(cocos2d::CCObject*);
    void onUseFillTextureToggled(cocos2d::CCObject*);
    void onUseBgTextureToggled(cocos2d::CCObject*);
    void onPickFillTexture(cocos2d::CCObject*);
    void onPickBgTexture(cocos2d::CCObject*);
    void onClearFillTexture(cocos2d::CCObject*);
    void onClearBgTexture(cocos2d::CCObject*);

    void applyAndSave();
};
