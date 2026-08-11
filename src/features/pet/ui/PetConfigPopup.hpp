#pragma once
#include <Geode/Geode.hpp>
#include "../services/PetManager.hpp"
#include <array>
#include <vector>

// Popup de configuracion de la mascota, reconstruido sobre PaiConfigKit:
// - Galeria: elige la imagen de la mascota.
// - Ajustes: lo esencial (tamano, movimiento, donde aparece).
// - Avanzado: efectos y comportamientos extra sin perder ninguna opcion.
class PetConfigPopup : public geode::Popup {
protected:
    void onExit() override;
    void scrollWheel(float x, float y) override;

    cocos2d::CCNode* m_galleryContainer = nullptr;
    cocos2d::CCMenu* m_galleryMenu = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCLabelBMFont* m_selectedLabel = nullptr;

    // scrolls construidos con PaiConfigKit
    geode::ScrollLayer* m_scrollLayer = nullptr;
    geode::ScrollLayer* m_advancedScroll = nullptr;

    // destino de scroll suave (rueda del raton) por area
    float m_settingsScrollTargetY = 0.f;
    bool  m_settingsScrollTargetSet = false;
    float m_advancedScrollTargetY = 0.f;
    bool  m_advancedScrollTargetSet = false;

    // controles que hay que mantener sincronizados
    CCMenuItemToggler* m_enableToggle = nullptr;
    cocos2d::CCLabelBMFont* m_enableStateLabel = nullptr;
    CCMenuItemToggler* m_allLayersToggle = nullptr;
    CCMenuItemToggler* m_showInGameplayToggle = nullptr;
    std::array<cocos2d::CCLabelBMFont*, 4> m_iconStateValueLabels{};

    int m_currentTab = 0; // 0 = galeria, 1 = ajustes, 2 = avanzado
    cocos2d::CCNode* m_galleryTab = nullptr;
    cocos2d::CCNode* m_settingsTab = nullptr;
    cocos2d::CCNode* m_advancedTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    bool init() override;
    void createTabButtons();
    void onTabSwitch(cocos2d::CCObject* sender);
    void updateSmoothScroll(float dt);

    void buildGalleryTab();
    void refreshGallery();
    void onAddImage(cocos2d::CCObject*);
    void onDeleteImage(cocos2d::CCObject*);
    void onDeleteAllImages(cocos2d::CCObject*);
    void onSelectImage(cocos2d::CCObject*);
    void onOpenShop(cocos2d::CCObject*);

    void buildSettingsTab();
    void buildAdvancedTab();
    void openLayerPicker();
    void pickIconStateImage(int stateIdx);

public:
    void applyLive();
    void refreshVisibleLayerControls();
    void refreshIconStateLabels();

    static PetConfigPopup* create();
};
