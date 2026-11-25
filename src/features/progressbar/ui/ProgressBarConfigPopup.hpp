#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../services/ProgressBarManager.hpp"
#include "../visuals/CustomProgressBarNode.hpp"
#include <Geode/Geode.hpp>
#include <array>
#include <vector>

class ProgressBarConfigPopup : public PaimonPopup {
public:
    static ProgressBarConfigPopup* create();

protected:
    bool init() override;
    void onExit() override;
    void update(float dt) override;

    int m_currentTab = 0;
    std::array<cocos2d::CCNode*, 5> m_tabContainers{};
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    // Live Preview Stage
    CustomProgressBarNode* m_previewBar = nullptr;
    cocos2d::CCLabelBMFont* m_previewInfoLbl = nullptr;
    CCMenuItemSpriteExtra* m_previewPlayBtn = nullptr;
    geode::Ref<cocos2d::CCTexture2D> m_previewFillTex;
    geode::Ref<cocos2d::CCTexture2D> m_previewBgTex;
    std::string m_loadedFillTexPath;
    std::string m_loadedBgTexPath;
    float m_previewProgress = 0.f;
    float m_previewTime = 0.f;
    bool m_previewPlaying = true;
    ProgressBarTelemetry m_previewTel;

    // Tab 0: Presets & Core
    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_hidePracticeToggle = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_presetBtns;

    // Tab 1: Style & Engine
    CCMenuItemSpriteExtra* m_styleBtn = nullptr;
    Slider* m_widthSlider = nullptr;      cocos2d::CCLabelBMFont* m_widthLabel = nullptr;
    Slider* m_heightSlider = nullptr;     cocos2d::CCLabelBMFont* m_heightLabel = nullptr;
    Slider* m_borderWSlider = nullptr;    cocos2d::CCLabelBMFont* m_borderWLabel = nullptr;
    Slider* m_radiusSlider = nullptr;     cocos2d::CCLabelBMFont* m_radiusLabel = nullptr;
    Slider* m_smoothSlider = nullptr;     cocos2d::CCLabelBMFont* m_smoothLabel = nullptr;
    CCMenuItemToggler* m_tipGlowToggle = nullptr;
    CCMenuItemToggler* m_pbMarkerToggle = nullptr;
    CCMenuItemToggler* m_cpMarkerToggle = nullptr;
    CCMenuItemToggler* m_milestoneToggle = nullptr;
    CCMenuItemToggler* m_deathMarkerToggle = nullptr;
    CCMenuItemToggler* m_newBestFlashToggle = nullptr;

    // Tab 2: Colors & Media
    CCMenuItemSpriteExtra* m_fillModeBtn = nullptr;
    CCMenuItemSpriteExtra* m_bgModeBtn = nullptr;
    CCMenuItemSpriteExtra* m_pctModeBtn = nullptr;
    cocos2d::CCSprite* m_fillColor1Preview = nullptr;
    cocos2d::CCSprite* m_fillColor2Preview = nullptr;
    cocos2d::CCSprite* m_fillColor3Preview = nullptr;
    cocos2d::CCSprite* m_bgColorPreview = nullptr;
    cocos2d::CCSprite* m_borderColorPreview = nullptr;
    Slider* m_opacitySlider = nullptr;    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    Slider* m_animSpeedSlider = nullptr;  cocos2d::CCLabelBMFont* m_animSpeedLabel = nullptr;
    CCMenuItemToggler* m_shadowToggle = nullptr;
    CCMenuItemToggler* m_useFillTexToggle = nullptr;
    CCMenuItemToggler* m_useBgTexToggle = nullptr;
    cocos2d::CCLabelBMFont* m_fillTexPathLabel = nullptr;
    cocos2d::CCLabelBMFont* m_bgTexPathLabel = nullptr;

    // Tab 3: Label & Telemetry
    CCMenuItemToggler* m_showPctToggle = nullptr;
    CCMenuItemSpriteExtra* m_labelFmtBtn = nullptr;
    CCMenuItemSpriteExtra* m_precisionBtn = nullptr;
    CCMenuItemSpriteExtra* m_placementBtn = nullptr;
    CCMenuItemSpriteExtra* m_fontBtn = nullptr;
    CCMenuItemToggler* m_usePctColorToggle = nullptr;
    cocos2d::CCSprite* m_pctColorPreview = nullptr;
    cocos2d::CCSprite* m_pctColor2Preview = nullptr;
    Slider* m_pctScaleSlider = nullptr;   cocos2d::CCLabelBMFont* m_pctScaleLabel = nullptr;
    Slider* m_pctOffXSlider = nullptr;    cocos2d::CCLabelBMFont* m_pctOffXLabel = nullptr;
    Slider* m_pctOffYSlider = nullptr;    cocos2d::CCLabelBMFont* m_pctOffYLabel = nullptr;

    // Tab 4: Layout & Decorations
    CCMenuItemToggler* m_verticalToggle = nullptr;
    CCMenuItemToggler* m_useCustomPosToggle = nullptr;
    CCMenuItemToggler* m_freeDragToggle = nullptr;
    Slider* m_posXSlider = nullptr;       cocos2d::CCLabelBMFont* m_posXLabel = nullptr;
    Slider* m_posYSlider = nullptr;       cocos2d::CCLabelBMFont* m_posYLabel = nullptr;
    Slider* m_scaleLenSlider = nullptr;   cocos2d::CCLabelBMFont* m_scaleLenLabel = nullptr;
    Slider* m_scaleThickSlider = nullptr; cocos2d::CCLabelBMFont* m_scaleThickLabel = nullptr;
    Slider* m_rotationSlider = nullptr;   cocos2d::CCLabelBMFont* m_rotationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_decoCountLbl = nullptr;

    void buildPreviewStage();
    void refreshPreviewTextures();
    void createTabButtons();
    void buildPresetsTab();
    void buildStyleTab();
    void buildColorsTab();
    void buildLabelTab();
    void buildLayoutTab();
    void syncAllControlsFromConfig();

    void onTabSwitch(cocos2d::CCObject* sender);
    void onTogglePreviewPlay(cocos2d::CCObject*);

    // Tab 0 callbacks
    void onEnableToggled(cocos2d::CCObject*);
    void onHidePracticeToggled(cocos2d::CCObject*);
    void onSelectPreset(cocos2d::CCObject* sender);
    void onResetDefaults(cocos2d::CCObject*);
    void onEnterFreeEditMode(cocos2d::CCObject*);
    void onCenterPosition(cocos2d::CCObject*);

    // Tab 1 callbacks
    void onCycleStyle(cocos2d::CCObject*);
    void onWidthChanged(cocos2d::CCObject*);
    void onHeightChanged(cocos2d::CCObject*);
    void onBorderWChanged(cocos2d::CCObject*);
    void onRadiusChanged(cocos2d::CCObject*);
    void onSmoothChanged(cocos2d::CCObject*);
    void onTipGlowToggled(cocos2d::CCObject*);
    void onPbMarkerToggled(cocos2d::CCObject*);
    void onCpMarkerToggled(cocos2d::CCObject*);
    void onMilestoneToggled(cocos2d::CCObject*);
    void onDeathMarkerToggled(cocos2d::CCObject*);
    void onNewBestFlashToggled(cocos2d::CCObject*);

    // Tab 2 callbacks
    void onCycleFillMode(cocos2d::CCObject*);
    void onCycleBgMode(cocos2d::CCObject*);
    void onCyclePctMode(cocos2d::CCObject*);
    void onPickFillColor1(cocos2d::CCObject*);
    void onPickFillColor2(cocos2d::CCObject*);
    void onPickFillColor3(cocos2d::CCObject*);
    void onPickBgColor(cocos2d::CCObject*);
    void onPickBorderColor(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onAnimSpeedChanged(cocos2d::CCObject*);
    void onShadowToggled(cocos2d::CCObject*);
    void onUseFillTextureToggled(cocos2d::CCObject*);
    void onUseBgTextureToggled(cocos2d::CCObject*);
    void onPickFillTexture(cocos2d::CCObject*);
    void onPickBgTexture(cocos2d::CCObject*);
    void onClearFillTexture(cocos2d::CCObject*);
    void onClearBgTexture(cocos2d::CCObject*);

    // Tab 3 callbacks
    void onShowPctToggled(cocos2d::CCObject*);
    void onCycleLabelFmt(cocos2d::CCObject*);
    void onCyclePrecision(cocos2d::CCObject*);
    void onCyclePlacement(cocos2d::CCObject*);
    void onPickFont(cocos2d::CCObject*);
    void onUsePctColorToggled(cocos2d::CCObject*);
    void onPickPctColor(cocos2d::CCObject*);
    void onPickPctColor2(cocos2d::CCObject*);
    void onPctScaleChanged(cocos2d::CCObject*);
    void onPctOffXChanged(cocos2d::CCObject*);
    void onPctOffYChanged(cocos2d::CCObject*);

    // Tab 4 callbacks
    void onVerticalToggled(cocos2d::CCObject*);
    void onUseCustomPosToggled(cocos2d::CCObject*);
    void onFreeDragToggled(cocos2d::CCObject*);
    void onPosXChanged(cocos2d::CCObject*);
    void onPosYChanged(cocos2d::CCObject*);
    void onScaleLenChanged(cocos2d::CCObject*);
    void onScaleThickChanged(cocos2d::CCObject*);
    void onRotationChanged(cocos2d::CCObject*);
    void onAddDecoration(cocos2d::CCObject*);
    void onClearDecorations(cocos2d::CCObject*);

    void applyAndSave();
};
