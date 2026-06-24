#pragma once
#include <Geode/Geode.hpp>
#include "../services/CursorManager.hpp"
#include <array>
#include <string>
#include <vector>

class CursorConfigPopup : public geode::Popup {
protected:
    void onExit() override;
    void scrollWheel(float x, float y) override;

    // Smooth-scroll target tracking (per scrollable area).
    float m_thumbScrollTargetY      = 0.f;
    bool  m_thumbScrollTargetSet    = false;
    float m_settingsScrollTargetY   = 0.f;
    bool  m_settingsScrollTargetSet = false;

    // Per-state slots (Idle / Move / Hover / Click / Text / Disabled)
    static constexpr int kSlotCount = CURSOR_STATE_COUNT;
    static constexpr std::array<CursorState, kSlotCount> kSlotStates = {
        CursorState::Idle, CursorState::Move, CursorState::Hover,
        CursorState::Click, CursorState::Text, CursorState::Disabled
    };

    CursorState m_activeSlot = CursorState::Idle;

    struct SlotWidgets {
        cocos2d::CCLayerColor*  bg      = nullptr;
        cocos2d::CCLabelBMFont* label   = nullptr;
        cocos2d::CCSprite*      preview = nullptr;
    };
    std::array<SlotWidgets, kSlotCount> m_slots{};

    // Pack navigation
    // m_packList[0] siempre es "" (sueltas); el resto son nombres de pack.
    std::vector<std::string> m_packList;
    int m_currentPackIdx = 0;
    cocos2d::CCLabelBMFont* m_packLabel = nullptr;

    // Thumbnail grid (scrollable)
    geode::ScrollLayer*     m_thumbScroll = nullptr;
    cocos2d::CCLabelBMFont* m_emptyGalleryLabel = nullptr;

    // Settings scroll
    geode::ScrollLayer*     m_scrollLayer     = nullptr;
    cocos2d::CCSprite*      m_scrollArrow     = nullptr;

    // Sliders
    Slider*                 m_scaleSlider     = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel      = nullptr;
    Slider*                 m_opacitySlider   = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel    = nullptr;
    Slider*                 m_followDelaySlider = nullptr;
    cocos2d::CCLabelBMFont* m_followDelayLabel  = nullptr;

    // Toggles
    CCMenuItemToggler* m_enableToggle     = nullptr;
    CCMenuItemToggler* m_trailToggle      = nullptr;
    CCMenuItemToggler* m_followDelayToggle = nullptr;
    CCMenuItemToggler* m_hoverToggle      = nullptr;
    CCMenuItemToggler* m_clickToggle      = nullptr;
    CCMenuItemToggler* m_textToggle       = nullptr;
    CCMenuItemToggler* m_disabledToggle   = nullptr;

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
    void refreshPackList();
    void refreshGallery();
    void updateSlotPreviews();
    static char const* slotDisplayName(CursorState state);
    std::string currentPack() const;
    void onActivateSlot(cocos2d::CCObject*);
    void onPackPrev(cocos2d::CCObject*);
    void onPackNext(cocos2d::CCObject*);
    void onDeletePack(cocos2d::CCObject*);
    void onSelectImage(cocos2d::CCObject*);
    void onDeleteImage(cocos2d::CCObject*);
    void onDeleteAllImages(cocos2d::CCObject*);
    void onAddImage(cocos2d::CCObject*);

    // Settings
    void buildSettingsTab();
    void checkScrollPosition(float dt);
    void updateSmoothScroll(float dt);
    void onEnableToggled(cocos2d::CCObject*);
    void onTrailToggled(cocos2d::CCObject*);
    void onHoverToggled(cocos2d::CCObject*);
    void onClickToggled(cocos2d::CCObject*);
    void onTextToggled(cocos2d::CCObject*);
    void onDisabledToggled(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onFollowDelayToggled(cocos2d::CCObject*);
    void onFollowDelayChanged(cocos2d::CCObject*);
    void onPresetPrev(cocos2d::CCObject*);
    void onPresetNext(cocos2d::CCObject*);
    void onEditTrail(cocos2d::CCObject*);
    void onLayerToggled(cocos2d::CCObject*);

    void applyLive();
    void updatePresetLabel();

public:
    static CursorConfigPopup* create();
};
