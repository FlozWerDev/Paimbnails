#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"

/**
 * PaiConfigLayer — Full-screen settings with 3 main tabs:
 *   1. Backgrounds  — per-layer bg config, left sidebar + right preview
 *   2. Profile      — profile picture image/shape with live preview
 *   3. Extras       — Pet config (beta) and future features
 */
class PaiConfigLayer : public cocos2d::CCLayer {
protected:
    bool init() override;
    void keyBackClicked() override;

    cocos2d::CCMenu* m_mainMenu = nullptr;
    cocos2d::CCMenu* m_bgMenu = nullptr;
    cocos2d::CCMenu* m_profileMenu = nullptr;
    cocos2d::CCMenu* m_extrasMenu = nullptr;

    // Extra menus that live outside the tab layers (need manual visibility toggle)
    cocos2d::CCMenu* m_bgSidebarMenu = nullptr;
    cocos2d::CCMenu* m_bgRow1Menu = nullptr;
    cocos2d::CCMenu* m_profileBtnColumn = nullptr;

    // ── Main tabs ──
    int m_currentMainTab = 0;
    std::vector<CCMenuItemSpriteExtra*> m_mainTabBtns;
    cocos2d::CCLayer* m_bgTab = nullptr;
    cocos2d::CCLayer* m_profileTab = nullptr;
    cocos2d::CCLayer* m_extrasTab = nullptr;

    // ── Background tab ──
    std::string m_selectedKey = "menu";
    std::vector<CCMenuItemSpriteExtra*> m_layerBtns;
    geode::TextInput* m_bgIdInput = nullptr;
    Slider* m_darkSlider = nullptr;
    CCMenuItemToggler* m_darkToggle = nullptr;
    CCMenuItemToggler* m_adaptiveToggle = nullptr;
    cocos2d::CCLabelBMFont* m_adaptiveLabel = nullptr;
    cocos2d::CCLayerColor* m_blockedOverlay = nullptr;
    cocos2d::CCLabelBMFont* m_blockedLabel = nullptr;
    cocos2d::CCNode* m_bgPreview = nullptr;
    cocos2d::CCLabelBMFont* m_bgStatusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_shaderLabel = nullptr;
    int m_shaderIndex = 0;

    // ── Profile tab ──
    cocos2d::CCNode* m_profilePreview = nullptr;

    // Tab switching
    void onMainTabSwitch(cocos2d::CCObject* sender);
    void switchMainTab(int idx);

    // Build tabs
    void buildBackgroundTab();
    void buildProfileTab();
    void buildExtrasTab();

    // Layer selector (Background tab)
    void onLayerSelect(cocos2d::CCObject* sender);
    void updateLayerButtons();
    void refreshForCurrentLayer();
    void rebuildBgPreview();

    // Background actions
    void onBgCustomImage(cocos2d::CCObject*);
    void onBgVideo(cocos2d::CCObject*);
    void onVideoSettings(cocos2d::CCObject*);
    void onBgRandom(cocos2d::CCObject*);
    void onBgSetID(cocos2d::CCObject*);
    void onBgSameAs(cocos2d::CCObject*);
    void onBgDefault(cocos2d::CCObject*);
    void onDarkMode(cocos2d::CCObject*);
    void onDarkIntensity(cocos2d::CCObject*);
    void onAdaptiveColors(cocos2d::CCObject*);
    void onShaderPrev(cocos2d::CCObject*);
    void onShaderNext(cocos2d::CCObject*);
    void updateShaderLabel();

    // Profile actions
    void onProfileImage(cocos2d::CCObject*);
    void onProfileImageClear(cocos2d::CCObject*);
    void onProfilePhoto(cocos2d::CCObject*);
    void rebuildProfilePreview();

    // Extras
    void onPetConfig(cocos2d::CCObject*);
    void onCustomCursor(cocos2d::CCObject*);
    void onTransitions(cocos2d::CCObject*);
    void onClearAllCache(cocos2d::CCObject*);

    // Common
    void onApply(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);
    CCMenuItemSpriteExtra* makeBtn(char const* text, cocos2d::CCPoint pos,
        cocos2d::SEL_MenuHandler handler, cocos2d::CCNode* parent, float scale = 0.55f);

public:
    static PaiConfigLayer* create();
    static cocos2d::CCScene* scene();
};


