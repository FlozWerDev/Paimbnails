#pragma once
#include <Geode/Geode.hpp>
#include "../services/CursorManager.hpp"
#include "CursorShopTab.hpp"
#include <array>
#include <string>
#include <vector>

class CursorConfigPopup : public geode::Popup {
protected:
    void onExit() override;
    void scrollWheel(float x, float y) override;

    // Smooth-scroll targets for each scrollable area.
    float m_thumbScrollTargetY      = 0.f;
    bool  m_thumbScrollTargetSet    = false;
    float m_settingsScrollTargetY   = 0.f;
    bool  m_settingsScrollTargetSet = false;
    float m_advancedScrollTargetY   = 0.f;
    bool  m_advancedScrollTargetSet = false;
    float m_trailScrollTargetY      = 0.f;
    bool  m_trailScrollTargetSet    = false;
    float m_transitionScrollTargetY = 0.f;
    bool  m_transitionScrollTargetSet = false;
    float m_clickScrollTargetY      = 0.f;
    bool  m_clickScrollTargetSet    = false;

    // Slots for Idle, Move, Hover, Click, Text, and Disabled.
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

    // m_packList[0] is the loose-image pack; the rest are pack names.
    std::vector<std::string> m_packList;
    int m_currentPackIdx = 0;
    cocos2d::CCLabelBMFont* m_packLabel = nullptr;

    geode::ScrollLayer*     m_thumbScroll = nullptr;
    cocos2d::CCLabelBMFont* m_emptyGalleryLabel = nullptr;

    geode::ScrollLayer*     m_scrollLayer = nullptr;
    geode::ScrollLayer*     m_advancedScroll = nullptr;
    cocos2d::CCSprite*      m_scrollArrow = nullptr;

    CCMenuItemToggler*      m_enableToggle     = nullptr;
    cocos2d::CCLabelBMFont* m_enableStateLabel = nullptr;
    cocos2d::CCLabelBMFont* m_presetLabel      = nullptr;

    // Refresh the toggle when gallery code changes enabled state.
    void syncEnableUI(bool enabled);

    geode::ScrollLayer*     m_trailScroll   = nullptr;
    cocos2d::CCNode*        m_trailControls = nullptr;
    paimon::cursorfx::CursorTrailNode* m_previewTrail = nullptr;
    cocos2d::CCSprite*      m_previewCursor = nullptr;
    cocos2d::CCNode*        m_previewArea   = nullptr;
    cocos2d::CCSize         m_previewSize{};
    float m_previewDemoTime = 0.f;
    // Debounced save for slider changes.
    float m_trailSaveTimer  = 0.f;
    bool  m_trailDirty      = false;

    geode::ScrollLayer* m_transitionScroll = nullptr;
    cocos2d::CCNode* m_transitionControls = nullptr;
    cocos2d::CCNode* m_transitionPreviewArea = nullptr;
    cocos2d::CCSize m_transitionPreviewSize{};
    std::array<cocos2d::CCSprite*, kSlotCount> m_transitionSprites{};
    std::array<cocos2d::CCPoint, kSlotCount> m_transitionBaseScales{};
    cocos2d::CCLabelBMFont* m_transitionStateLabel = nullptr;
    cocos2d::CCLabelBMFont* m_transitionPresetLabel = nullptr;
    int m_transitionFrom = 0;
    int m_transitionTo = 1;
    float m_transitionPreviewTime = 0.f;
    float m_transitionSaveTimer = 0.f;
    bool m_transitionDirty = false;

    geode::ScrollLayer* m_clickScroll = nullptr;
    cocos2d::CCNode* m_clickControls = nullptr;
    cocos2d::CCNode* m_clickPreviewArea = nullptr;
    cocos2d::CCSize m_clickPreviewSize{};
    paimon::cursorfx::CursorClickNode* m_clickPreview = nullptr;
    cocos2d::CCSprite* m_clickPreviewCursor = nullptr;
    cocos2d::CCPoint m_clickCursorBaseScale{1.f, 1.f};
    cocos2d::CCLabelBMFont* m_clickPresetLabel = nullptr;
    cocos2d::CCLabelBMFont* m_clickHintLabel = nullptr;
    // Preview button state, independent of the real cursor.
    bool  m_clickPreviewHeld = false;
    float m_clickPreviewAnimTime = 999.f;
    bool  m_clickPreviewAnimHeld = false;
    // Test timer keeps the release animation visible.
    float m_clickTestTimer = 0.f;
    float m_clickSaveTimer = 0.f;
    bool  m_clickDirty     = false;

    int m_currentTab = 0; // gallery, shop, settings, trail, transition, click, advanced
    cocos2d::CCNode* m_galleryTab  = nullptr;
    CursorShopTab*   m_shopTab     = nullptr;
    cocos2d::CCNode* m_settingsTab = nullptr;
    cocos2d::CCNode* m_trailTab    = nullptr;
    cocos2d::CCNode* m_transitionTab = nullptr;
    cocos2d::CCNode* m_clickTab    = nullptr;
    cocos2d::CCNode* m_advancedTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    bool init() override;
    void createTabButtons();
    void onTabSwitch(cocos2d::CCObject* sender);

    void buildGalleryTab();
    void buildShopTab();
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

    void buildSettingsTab();
    void buildAdvancedTab();
    void checkScrollPosition(float dt);
    void updateSmoothScroll(float dt);

    void buildTrailTab();
    void rebuildTrailControls();
    // Defer rebuilding so a control callback can finish safely.
    void queueRebuildTrailControls();
    void applyTrailLive();
    void flushTrailSave();
    void updateTrailPreview(float dt);

    void buildTransitionTab();
    void rebuildTransitionControls();
    void queueRebuildTransitionControls();
    void applyTransitionLive();
    void flushTransitionSave();
    void refreshTransitionPreviewSprites();
    void replayTransitionPreview();
    void updateTransitionPreview(float dt);
    void updateTransitionPresetLabel();

    void buildClickTab();
    void rebuildClickControls();
    void queueRebuildClickControls();
    void applyClickLive();
    void flushClickSave();
    void updateClickPreview(float dt);
    void updateClickPresetLabel();
    void triggerPreviewClick();
    void openBurstTuning(bool release);
    void openHoldTuning();

    void applyLive();
    void updatePresetLabel();

public:
    static CursorConfigPopup* create();
};
