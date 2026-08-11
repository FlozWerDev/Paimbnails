#pragma once

// Ficha de un set/pack de la tienda: rejilla con todos sus cursores, vista
// previa grande del seleccionado e instalacion (uno suelto o el set entero).
//
// Las descargas salen de aqui, nunca del listado: los dos sitios piden que no
// se rastreen sus rutas de fichero, asi que solo se baja lo que el usuario pide.

#include <Geode/Geode.hpp>
#include "../services/CursorShopClient.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

class CursorShopDetailPopup : public geode::Popup {
public:
    // `onInstalled` avisa a la galeria para que se repinte.
    static CursorShopDetailPopup* create(paimon::cursorshop::Listing listing,
                                         std::function<void()> onInstalled);

protected:
    bool init() override;
    void onExit() override;
    void scrollWheel(float x, float y) override;

private:
    paimon::cursorshop::Listing m_listing;
    paimon::cursorshop::Detail  m_detail;
    std::function<void()> m_onInstalled;

    bool m_alive = true;
    bool m_loaded = false;
    bool m_busy = false;

    int m_selected = 0;
    CursorState m_assignState = CursorState::Idle;

    geode::ScrollLayer* m_grid = nullptr;
    float m_gridScrollTargetY = 0.f;
    bool  m_gridScrollTargetSet = false;

    cocos2d::CCNode*        m_sideMenu = nullptr;
    cocos2d::CCNode*        m_previewBox = nullptr;
    cocos2d::CCNode*        m_previewSlot = nullptr;
    cocos2d::CCLabelBMFont* m_previewName = nullptr;
    cocos2d::CCLabelBMFont* m_stateLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_subtitle = nullptr;
    CCMenuItemSpriteExtra* m_animateButton = nullptr;

    // Un .ani ya descargado y decodificado.
    struct Animation {
        std::vector<geode::Ref<cocos2d::CCTexture2D>> frames;
        float step = 0.1f;   // segundos por fotograma
    };
    std::map<std::string, Animation> m_animations;
    bool m_animating = false;

    // Cola de descargas de "Instalar todo".
    std::vector<int> m_queue;
    std::size_t m_queueIndex = 0;
    std::string m_queuePack;
    int m_queueDone = 0;
    bool m_queueAssign = false;

    void fetchDetail();
    void buildBody();
    void rebuildGrid();
    void updateSelection();
    void updateStateLabel();
    void setStatus(std::string const& text, cocos2d::ccColor3B color);
    void setBusy(bool busy);

    void onCursorCell(cocos2d::CCObject* sender);
    void onStatePrev(cocos2d::CCObject*);
    void onStateNext(cocos2d::CCObject*);
    void onInstallOne(cocos2d::CCObject*);
    void onInstallAssign(cocos2d::CCObject*);
    void onInstallAll(cocos2d::CCObject*);
    void onAnimate(cocos2d::CCObject*);

    // Baja el .ani, lo decodifica y lo reproduce en la vista previa.
    void playAnimation(std::string const& url);
    void showAnimation(std::string const& url);

    void startQueue(std::vector<int> indices, std::string const& packName, bool assign);
    void stepQueue();
    void finishQueue();

    void updateSmoothScroll(float dt);

    static char const* stateName(CursorState state);
    static cocos2d::ccColor3B stateColor(CursorState state);
};
