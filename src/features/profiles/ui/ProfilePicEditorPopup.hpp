#pragma once
#include <Geode/Geode.hpp>
#include "../services/ProfilePicCustomizer.hpp"
#include "ProfilePicIconsDetailPopup.hpp"

class ProfilePicEditorPopup : public geode::Popup {
protected:
    ProfilePicConfig m_editConfig;

    cocos2d::CCNode* m_previewContainer = nullptr;
    cocos2d::CCLabelBMFont* m_previewStatusLabel = nullptr;

    cocos2d::CCNode* m_tabContent = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
    int m_currentTab = 0;

    Slider* m_thicknessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_thicknessLabel = nullptr;
    Slider* m_frameOpacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_frameOpacityLabel = nullptr;

    Slider* m_scaleXSlider = nullptr;
    Slider* m_scaleYSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    Slider* m_rotationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleXLabel = nullptr;
    cocos2d::CCLabelBMFont* m_scaleYLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sizeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_rotationLabel = nullptr;

    int m_decoCategoryIdx = 0;
    int m_decoPage = 0;
    int m_selectedDecoIdx = -1;

    Slider* m_imgZoomSlider = nullptr;
    Slider* m_imgRotationSlider = nullptr;
    Slider* m_imgOffsetXSlider = nullptr;
    Slider* m_imgOffsetYSlider = nullptr;
    Slider* m_imgOpacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_imgZoomLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgRotationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgOffsetXLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgOffsetYLabel = nullptr;
    cocos2d::CCLabelBMFont* m_imgOpacityLabel = nullptr;

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

    geode::Ref<cocos2d::CCTexture2D> m_previewTexture;

    bool m_triggeredDownload = false;

    bool init();

    void createTabs();
    void switchTab(int tab);
    void onTabBtn(cocos2d::CCObject* sender);
    void rebuildCurrentTab();

    // Photo tab: image source + framing
    cocos2d::CCNode* createPhotoTab();
    void onPhotoSourceProfile(cocos2d::CCObject* sender);
    void onPickCustomPhoto(cocos2d::CCObject* sender);
    void onImgZoomChanged(cocos2d::CCObject* sender);
    void onImgRotationChanged(cocos2d::CCObject* sender);
    void onImgOffsetXChanged(cocos2d::CCObject* sender);
    void onImgOffsetYChanged(cocos2d::CCObject* sender);
    void onImgOpacityChanged(cocos2d::CCObject* sender);
    void onImgFlipX(cocos2d::CCObject* sender);
    void onImgFlipY(cocos2d::CCObject* sender);
    void onResetAdjust(cocos2d::CCObject* sender);

    // Shape tab
    cocos2d::CCNode* createShapeTab();
    void onScaleXChanged(cocos2d::CCObject* sender);
    void onScaleYChanged(cocos2d::CCObject* sender);
    void onSizeChanged(cocos2d::CCObject* sender);
    void onRotationChanged(cocos2d::CCObject* sender);
    void onStencilSelect(cocos2d::CCObject* sender);
    void onResetShape(cocos2d::CCObject* sender);

    // Border tab
    cocos2d::CCNode* createBorderTab();
    void onFrameToggle(cocos2d::CCObject* sender);
    void onThicknessChanged(cocos2d::CCObject* sender);
    void onFrameOpacityChanged(cocos2d::CCObject* sender);
    void onBorderColorSelect(cocos2d::CCObject* sender);
    void onPickCustomBorderColor(cocos2d::CCObject* sender);

    // Deco tab
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
    void onDecoPickColor(cocos2d::CCObject* sender);
    void onClearAllDecos(cocos2d::CCObject* sender);

    // Icon tab
    cocos2d::CCNode* createIconTab();
    void onOnlyIconToggle(cocos2d::CCObject* sender);
    void onGameIconSelect(cocos2d::CCObject* sender);
    void onOpenIconsDetail(cocos2d::CCObject* sender);

    // Style tab
    cocos2d::CCNode* createStyleTab();
    void onFontSelect(cocos2d::CCObject* sender);
    void onPreset(cocos2d::CCObject* sender);
    void onRandomize(cocos2d::CCObject* sender);
    void onResetAll(cocos2d::CCObject* sender);

    void rebuildPreview();
    void triggerImageDownloadIfNeeded();

    void onSave(cocos2d::CCObject* sender);

public:
    static ProfilePicEditorPopup* create();
};
