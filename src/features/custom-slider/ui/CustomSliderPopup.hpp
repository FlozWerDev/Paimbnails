#pragma once
#include <Geode/Geode.hpp>
#include "../services/CustomSliderManager.hpp"

namespace paimon::slider {

class CustomSliderPopup : public geode::Popup {
public:
    static CustomSliderPopup* create();

protected:
    bool init() override;
    void onExit() override;

    int m_currentTab = 0;
    cocos2d::CCNode* m_tabGeneral = nullptr;
    cocos2d::CCNode* m_tabAnim = nullptr;
    cocos2d::CCNode* m_tabTargets = nullptr;
    CCMenuItemSpriteExtra* m_tabBtn0 = nullptr;
    CCMenuItemSpriteExtra* m_tabBtn1 = nullptr;
    CCMenuItemSpriteExtra* m_tabBtn2 = nullptr;

    cocos2d::CCNode* m_previewNode = nullptr;

    cocos2d::CCNode* m_generalColumn = nullptr;
    cocos2d::CCNode* m_rowEnable = nullptr;
    cocos2d::CCNode* m_rowMode = nullptr;
    cocos2d::CCNode* m_rowIconType = nullptr;     // Icon mode
    cocos2d::CCNode* m_rowImagePick = nullptr;    // Image/GIF mode
    cocos2d::CCNode* m_rowPlayerIcon = nullptr;   // Icon mode
    cocos2d::CCNode* m_rowPlayerColors = nullptr; // Icon mode
    cocos2d::CCNode* m_rowScale = nullptr;
    cocos2d::CCNode* m_rowContainer = nullptr;    // Image/GIF mode
    cocos2d::CCNode* m_rowShape = nullptr;        // Image + container

    cocos2d::CCLabelBMFont* m_iconTypeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imagePathLabel = nullptr;
    cocos2d::CCNode* m_shapeGridNode = nullptr;
    cocos2d::CCMenu* m_shapeGridMenu = nullptr;
    cocos2d::CCLabelBMFont* m_borderToggleLabel = nullptr;
    Slider* m_scaleSlider = nullptr;
    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_playerIconToggle = nullptr;
    CCMenuItemToggler* m_playerColorsToggle = nullptr;
    CCMenuItemToggler* m_containerToggle = nullptr;
    CCMenuItemToggler* m_borderToggle = nullptr;

    cocos2d::CCNode* m_animColumn = nullptr;
    cocos2d::CCLabelBMFont* m_animTypeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_animDurationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_animBounceLabel = nullptr;
    cocos2d::CCLabelBMFont* m_animRotateLabel = nullptr;
    Slider* m_animDurationSlider = nullptr;
    Slider* m_animBounceSlider = nullptr;
    Slider* m_animRotateSlider = nullptr;
    CCMenuItemToggler* m_animToggle = nullptr;

    CCMenuItemToggler* m_optionsToggle = nullptr;
    CCMenuItemToggler* m_editorToggle = nullptr;
    CCMenuItemToggler* m_colorsToggle = nullptr;
    CCMenuItemToggler* m_garageToggle = nullptr;

    void refreshPreview();
    void switchTab(int tab);
    void buildGeneralTab();
    void buildAnimTab();
    void buildTargetsTab();
    void reapplyAllSliders();
    static int getPlayerIconId(SliderIconType type);
    static IconType toGDIconType(SliderIconType type);

    void onTab(cocos2d::CCObject*);
    void onToggleEnabled(cocos2d::CCObject*);
    void onModeLeft(cocos2d::CCObject*);
    void onModeRight(cocos2d::CCObject*);
    void onIconTypeLeft(cocos2d::CCObject*);
    void onIconTypeRight(cocos2d::CCObject*);
    void onTogglePlayerIcon(cocos2d::CCObject*);
    void onTogglePlayerColors(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onPickImage(cocos2d::CCObject*);
    void onToggleContainer(cocos2d::CCObject*);
    void onToggleBorder(cocos2d::CCObject*);
    void onShapeSelect(cocos2d::CCObject*);
    void rebuildShapeGrid();
    void onToggleAnimate(cocos2d::CCObject*);
    void onAnimTypeLeft(cocos2d::CCObject*);
    void onAnimTypeRight(cocos2d::CCObject*);
    void onAnimDurationChanged(cocos2d::CCObject*);
    void onAnimBounceChanged(cocos2d::CCObject*);
    void onAnimRotateChanged(cocos2d::CCObject*);
    void onToggleOptions(cocos2d::CCObject*);
    void onToggleEditor(cocos2d::CCObject*);
    void onToggleColors(cocos2d::CCObject*);
    void onToggleGarage(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
};

} // namespace paimon::slider
