#pragma once

// Pestaña Tienda del popup de cursor: navega los catalogos publicos de
// rw-designer.com y custom-cursor.com con categorias, buscador y paginacion.
// Aqui solo se leen listados y miniaturas; quien descarga ficheros es el popup
// de detalle, y solo cuando el usuario lo pide.
//
// El cuadro de busqueda filtra al momento lo que ya esta en pantalla. Para
// buscar en todo el catalogo hace falta pulsar el boton: rw-designer tiene
// buscador propio (/cursor-library?search=), pero custom-cursor bloquea el suyo
// y ahi toca recorrer sus colecciones una a una.

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../services/CursorShopClient.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

class CursorShopTab : public cocos2d::CCNode {
public:
    static CursorShopTab* create(cocos2d::CCSize size, std::function<void()> onInstalled);

    // El catalogo no se pide hasta que el usuario entra en la pestaña; si la
    // carga fallo, volver a entrar reintenta.
    void onShown();
    // El popup contenedor reenvia la rueda y el tick de scroll suave.
    void handleScrollWheel(float x, float y);
    void stepScroll(float dt);
    // Suelta el IME antes de que el popup se destruya.
    void shutdown();

private:
    using Store    = paimon::cursorshop::Store;
    using Category = paimon::cursorshop::Category;
    using Listing  = paimon::cursorshop::Listing;

    // Una peticion pendiente del rastreo de busqueda.
    struct ScanTarget {
        Category category;
        int page = 0;
    };

    std::function<void()> m_onInstalled;
    bool m_alive = true;

    Store m_store = Store::RwDesigner;
    std::array<std::vector<Category>, paimon::cursorshop::kStoreCount> m_categories{};
    std::array<int, paimon::cursorshop::kStoreCount> m_categoryIdx{};

    std::vector<Listing> m_items;
    std::vector<int> m_filtered;
    std::string m_query;

    // Identifica el listado ya descargado para no repetir la peticion al
    // paginar en local.
    std::string m_loadedKey;
    int m_serverPage = 0;
    int m_serverPageCount = 1;
    int m_localPage = 0;
    // -1 pide saltar a la ultima pagina local tras cargar (al ir hacia atras).
    int m_pendingLocalPage = 0;
    bool m_loading = false;

    // m_items son resultados de busqueda en vez de una pagina de categoria.
    bool m_searchResults = false;
    // Categoria sintetica que representa la busqueda en curso.
    Category m_searchCategory;
    bool m_scanning = false;
    std::vector<ScanTarget> m_scanTargets;
    std::size_t m_scanIndex = 0;

    geode::ScrollLayer* m_grid = nullptr;
    float m_gridScrollTargetY = 0.f;
    bool  m_gridScrollTargetSet = false;

    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_categoryLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_creditLabel = nullptr;
    cocos2d::CCLabelBMFont* m_overlayLabel = nullptr;
    CCMenuItemSpriteExtra* m_overlayButton = nullptr;
    // Lanza la busqueda cuando el filtro local si encontro algo y el cartel
    // del centro no llega a salir.
    CCMenuItemSpriteExtra* m_searchButton = nullptr;
    ButtonSprite* m_overlayButtonSprite = nullptr;
    std::array<ButtonSprite*, paimon::cursorshop::kStoreCount> m_storeSprites{};

    bool initWithSize(cocos2d::CCSize size);
    void buildChrome(cocos2d::CCSize size);

    Category const& currentCategory() const;
    std::string listingKey() const;
    int storeIndex() const { return static_cast<int>(m_store); }
    // Paginas locales que caben en una peticion.
    int localPagesPerFetch() const;
    // Paginas locales que ocupa lo que hay cargado ahora.
    int localPageCount() const;

    void selectStore(Store store);
    void applyStoreStyle();
    void ensureCategories();
    void requestPage(int serverPage);
    void fetchListing();
    bool matchesQuery(Listing const& item) const;
    void applyFilter();
    void rebuildGrid();
    void updateChrome();
    void setOverlay(std::string const& text, cocos2d::ccColor3B color);
    void setOverlayAction(char const* label, bool visible);

    void startSearch();
    void startDeepSearch();
    void stepDeepSearch();
    void finishDeepSearch();
    void stopDeepSearch();
    std::vector<ScanTarget> buildScanTargets() const;

    void onStoreButton(cocos2d::CCObject* sender);
    void onCategoryPrev(cocos2d::CCObject*);
    void onCategoryNext(cocos2d::CCObject*);
    void onPagePrev(cocos2d::CCObject*);
    void onPageNext(cocos2d::CCObject*);
    void onOverlayAction(cocos2d::CCObject*);
    void onCard(cocos2d::CCObject* sender);
};
