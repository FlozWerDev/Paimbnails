#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include "../services/TransitionManager.hpp"

// ════════════════════════════════════════════════════════════
// TransitionConfigPopup — UI completa para configurar transiciones
// ════════════════════════════════════════════════════════════

class TransitionConfigPopup : public geode::Popup {
protected:
    bool init();

    // ── State ──
    TransitionConfig m_editingGlobal;
    TransitionConfig m_editingLevel;
    bool m_editingIsGlobal = true; // which section we last interacted with

    // ── UI — Global section ──
    cocos2d::CCLabelBMFont* m_globalNameLabel = nullptr;
    cocos2d::CCLabelBMFont* m_globalDescLabel = nullptr;
    cocos2d::CCLabelBMFont* m_globalDurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_globalIndexLabel = nullptr;
    CCMenuItemSpriteExtra* m_globalColorBtn = nullptr;
    CCMenuItemSpriteExtra* m_globalCustomBtn = nullptr;
    cocos2d::CCLayerColor* m_globalColorSwatch = nullptr;

    // ── UI — Level Entry section ──
    cocos2d::CCLabelBMFont* m_levelNameLabel = nullptr;
    cocos2d::CCLabelBMFont* m_levelDescLabel = nullptr;
    cocos2d::CCLabelBMFont* m_levelDurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_levelIndexLabel = nullptr;
    CCMenuItemSpriteExtra* m_levelColorBtn = nullptr;
    CCMenuItemSpriteExtra* m_levelCustomBtn = nullptr;
    cocos2d::CCLayerColor* m_levelColorSwatch = nullptr;

    // ── UI — General ──
    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_levelToggle = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    // ── Methods ──
    void updateGlobalDisplay();
    void updateLevelDisplay();
    void updateConditionalButtons();
    int getTypeIndex(TransitionType t) const;

    // ── Callbacks ──
    void onToggleEnabled(cocos2d::CCObject*);
    void onToggleLevelEntry(cocos2d::CCObject*);
    void onGlobalPrevType(cocos2d::CCObject*);
    void onGlobalNextType(cocos2d::CCObject*);
    void onGlobalDurDown(cocos2d::CCObject*);
    void onGlobalDurUp(cocos2d::CCObject*);
    void onGlobalColor(cocos2d::CCObject*);
    void onGlobalCustom(cocos2d::CCObject*);
    void onLevelPrevType(cocos2d::CCObject*);
    void onLevelNextType(cocos2d::CCObject*);
    void onLevelDurDown(cocos2d::CCObject*);
    void onLevelDurUp(cocos2d::CCObject*);
    void onLevelColor(cocos2d::CCObject*);
    void onLevelCustom(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onInfoGlobal(cocos2d::CCObject*);
    void onInfoLevel(cocos2d::CCObject*);
    void onInfoType(cocos2d::CCObject*);
    void onInfoDuration(cocos2d::CCObject*);
    void onInfoCustom(cocos2d::CCObject*);

public:
    static TransitionConfigPopup* create();
};
