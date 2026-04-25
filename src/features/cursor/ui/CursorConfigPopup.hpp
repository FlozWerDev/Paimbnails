#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>

class CursorConfigPopup : public geode::Popup {
protected:
    void onExit() override;
    void scrollWheel(float x, float y) override;

    // Gallery state
    enum class ActiveSlot { Idle, Move };
    ActiveSlot m_activeSlot = ActiveSlot::Idle;

    cocos2d::CCNode* m_galleryContainer = nullptr;
    cocos2d::CCMenu* m_galleryMenu      = nullptr;

    // Preview sprites for idle/move slots
    cocos2d::CCSprite*      m_idlePreview     = nullptr;
    cocos2d::CCSprite*      m_movePreview     = nullptr;
    cocos2d::CCLayerColor*  m_idleSlotBg      = nullptr;
    cocos2d::CCLayerColor*  m_moveSlotBg      = nullptr;
    cocos2d::CCLabelBMFont* m_idleLabel       = nullptr;
    cocos2d::CCLabelBMFont* m_moveLabel       = nullptr;

    // Settings scroll
    geode::ScrollLayer*     m_scrollLayer     = nullptr;
    cocos2d::CCSprite*      m_scrollArrow     = nullptr;

    // Sliders
    Slider*                 m_scaleSlider     = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel      = nullptr;
    Slider*                 m_opacitySlider   = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel    = nullptr;

    // Toggles
    CCMenuItemToggler* m_enableToggle     = nullptr;
    CCMenuItemToggler* m_trailToggle      = nullptr;

    // Trail preset picker
    cocos2d::CCLabelBMFont* m_presetLabel = nullptr;

    // Tabs
    int m_currentTab = 0; // 0=gallery, 1=settings
    cocos2d::CCNode* m_galleryTab  = nullptr;
    cocos2d::CCNode* m_settingsTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    bool init() override;
    void createTabButtons();
    void onTabSwitch(cocos2d::CCObject* sender);

    // Gallery
    void buildGalleryTab();
    void refreshGallery();
    void updateSlotPreviews();
    void onActivateIdleSlot(cocos2d::CCObject*);
    void onActivateMoveSlot(cocos2d::CCObject*);
    void onSelectImage(cocos2d::CCObject*);
    void onDeleteImage(cocos2d::CCObject*);
    void onDeleteAllImages(cocos2d::CCObject*);
    void onAddImage(cocos2d::CCObject*);

    // Settings
    void buildSettingsTab();
    void checkScrollPosition(float dt);
    void onEnableToggled(cocos2d::CCObject*);
    void onTrailToggled(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onPresetPrev(cocos2d::CCObject*);
    void onPresetNext(cocos2d::CCObject*);
    void onEditTrail(cocos2d::CCObject*);
    void onLayerToggled(cocos2d::CCObject*);

    void applyLive();
    void updatePresetLabel();

public:
    static CursorConfigPopup* create();
};
