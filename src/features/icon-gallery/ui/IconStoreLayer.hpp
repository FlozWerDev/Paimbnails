#pragma once
// Tienda de iconos: escena a pantalla completa con la rejilla de la galeria
// publica (iconsgallery.pages.dev), buscador, filtros y paginacion.
//
// Se abre desde el boton de la garage.

#include "../IconGalleryTypes.hpp"
#include "../services/GalleryStore.hpp"

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <vector>

namespace geode {
class ScrollLayer;
class TextInput;
}

namespace paimon::icon_gallery {

class IconStoreCard;

class IconStoreLayer : public cocos2d::CCLayer {
public:
    static IconStoreLayer* create();
    static void open();

    void onBack();

protected:
    bool init() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;
    void keyBackClicked() override;
    ~IconStoreLayer() override;

    void buildBackground();
    void buildHeader();
    void buildFooter();

    void startLoading();
    void rebuildGrid();
    void refreshFooter();
    void showMessage(std::string const& title, std::string const& body);

    void onIconPressed(std::string const& slug);
    void onFilters();
    // Debe quedar puesto antes de pedir nada: con el catalogo ya cacheado, el
    // store resuelve iconos de disco de forma sincrona y sin esto sus avisos
    // se perderian (tarjetas en blanco al volver a entrar).
    void installStoreCallback();
    void onIconReady(std::string const& slug);
    void requestVisiblePage();

    void setPage(int page);
    int pageCount() const;

    GalleryStore::Query m_query;
    std::vector<std::size_t> m_results;
    int m_page = 0;
    bool m_loading = true;
    bool m_enteredOnce = false;

    cocos2d::CCNode* m_scrollHost = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_messageHost = nullptr;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_footer = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextBtn = nullptr;
    cocos2d::CCNode* m_loadSpinner = nullptr;

    std::map<std::string, IconStoreCard*> m_cards;

    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
};

}  // namespace paimon::icon_gallery
