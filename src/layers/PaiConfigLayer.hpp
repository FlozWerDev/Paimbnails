#pragma once

// PaiConfigLayer — el editor de fondos completo de Paimon.
//
// Una sola pantalla para todo lo que antes estaba repartido entre este layer y
// el viejo popup "Editor de Fondos": eliges la pantalla del juego a la
// izquierda, la fuente del fondo a la derecha, y abajo ves una miniatura EN
// VIVO (imagen, GIF, video o shader corriendo de verdad) con una maqueta de la
// UI vanilla de esa pantalla encima.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>

#include <string>
#include <vector>

#include "../features/backgrounds/services/LayerBackgroundManager.hpp"

namespace geode {
class ScrollLayer;
class TextInput;
}

namespace paimon::bgpreview {
class LayerPreviewNode;
}

class PaiConfigLayer : public cocos2d::CCLayer {
public:
    static PaiConfigLayer* create();
    static cocos2d::CCScene* scene();

    // Abre el editor encima de la escena actual, sin pushScene. Lo usan los
    // sitios que ya viven dentro de un popup y no pueden empujar escena.
    static PaiConfigLayer* openOverlay();

protected:
    bool init() override;
    void keyBackClicked() override;

    void buildChrome();          // fondo, titulo, back, pestanas, barra inferior
    void buildBackgroundsTab();
    void buildProfileTab();
    void buildExtrasTab();

    cocos2d::CCNode* buildScreenList(cocos2d::CCRect area);
    cocos2d::CCNode* buildPreviewCard(cocos2d::CCRect area);
    cocos2d::CCNode* buildControlsCard(cocos2d::CCRect area);

    // Filas del panel de controles (todas devuelven anchor {0,0}).
    cocos2d::CCNode* rowSources(float width);
    cocos2d::CCNode* rowLevelId(float width);
    cocos2d::CCNode* rowDarken(float width);
    cocos2d::CCNode* rowFilter(float width);
    cocos2d::CCNode* rowAdaptive(float width);
    cocos2d::CCNode* rowVideoSettings(float width);
    cocos2d::CCNode* rowCopyToAll(float width);
    cocos2d::CCNode* rowModuleWarning(float width);

    void switchTab(int index);
    void selectScreen(std::string const& key);
    void refreshAll();           // relee la config y actualiza controles + preview
    void refreshScreenList();
    void refreshPreview();

    LayerBgConfig currentConfig() const;
    void mutateConfig(std::function<void(LayerBgConfig&)> const& fn,
                      char const* toastKey = nullptr, char const* toastFallback = nullptr);

    void onPickImage();
    void onPickVideo();
    void onUseShaderBg();
    void onUseRandom();
    void onUseDynamicShader();
    void onUseLevelId();
    void onUseSameAs();
    void onUseDefault();
    void onVideoSettings();
    void onToggleDark(bool on);
    void onDarkSlider(cocos2d::CCObject*);
    void onFilterStep(int delta);
    void onFilterSlider(cocos2d::CCObject*);
    void onToggleAdaptive(bool on);
    void onToggleMock();
    void onExpandPreview();
    void onCopyToAllScreens();
    void onApplyAndRestart();
    void onResetScreen();
    void onBack();

    void rebuildProfilePreview();
    void onProfileImage();
    void onProfileClear();
    void onProfileShape();
    void onClearAllCache();

    cocos2d::CCNode* makeCardWindow(cocos2d::CCRect area, char const* title);
    cocos2d::CCNode* addScrollHint(cocos2d::CCNode* card, cocos2d::CCSize cardSize);
    void updateFilterLabels();
    std::vector<std::pair<std::string, std::string>> const& activeFilterList() const;

    cocos2d::CCMenu* m_chromeMenu = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;
    std::vector<cocos2d::CCNode*> m_tabPages;
    int m_currentTab = 0;

    std::string m_selectedKey = "menu";

    geode::ScrollLayer* m_screenScroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_screenButtons;

    paimon::bgpreview::LayerPreviewNode* m_preview = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_screenChipLabel = nullptr;
    cocos2d::CCLabelBMFont* m_blockedLabel = nullptr;
    cocos2d::CCNode* m_blockedOverlay = nullptr;
    CCMenuItemToggler* m_mockToggle = nullptr;

    geode::ScrollLayer* m_controlsScroll = nullptr;
    cocos2d::CCNode* m_controlsHint = nullptr;
    // Las filas viven en un Ref porque las que sobran salen del ScrollLayer:
    // ese layer usa setVisible() para recortar lo que no se ve, asi que la
    // visibilidad NO puede decir que filas quiere la config.
    std::vector<geode::Ref<cocos2d::CCNode>> m_controlRows;
    // Recoloca las filas activas del panel de controles para que ocultar una no
    // deje un hueco. Solo hace scroll al inicio si cambio que filas se ven.
    void relayoutControls();
    bool isControlRowEnabled(cocos2d::CCNode* row) const;
    std::string m_controlsSignature;
    geode::TextInput* m_levelIdInput = nullptr;
    CCMenuItemToggler* m_darkToggle = nullptr;
    Slider* m_darkSlider = nullptr;
    cocos2d::CCLabelBMFont* m_darkValueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_filterLabel = nullptr;
    Slider* m_filterSlider = nullptr;
    cocos2d::CCLabelBMFont* m_filterValueLabel = nullptr;
    CCMenuItemToggler* m_adaptiveToggle = nullptr;
    geode::Ref<cocos2d::CCNode> m_adaptiveRow;
    geode::Ref<cocos2d::CCNode> m_videoRow;
    geode::Ref<cocos2d::CCNode> m_moduleWarnRow;
    // Filas condicionales: que la config las quiera, no si estan dibujadas.
    bool m_showAdaptive = false;
    bool m_showVideo = false;
    bool m_showModuleWarn = false;
    cocos2d::CCLabelBMFont* m_filterTitle = nullptr;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_sourceButtons;

    cocos2d::CCNode* m_profilePreview = nullptr;
    int m_profilePreviewGen = 0;

    int m_filterIndex = 0;
    bool m_overlayMode = false;

    // Scroll con rueda
    float m_controlsTargetY = 0.f;
    bool m_controlsTargetSet = false;
    float m_screenTargetY = 0.f;
    bool m_screenTargetSet = false;
    void scrollWheel(float x, float y) override;
    void update(float dt) override;
};
