#include "CursorShopTab.hpp"
#include "CursorShopDetailPopup.hpp"
#include "../services/CursorShopImages.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::cursorshop;

namespace {

namespace kit = paimon::configkit;

constexpr float kCellWidth  = 108.f;
constexpr float kCellHeight = 86.f;
constexpr float kCellGap    = 6.f;
// Fichas por vista. Cada una arrastra la descarga de su miniatura, asi que
// conviene que sean pocas aunque la peticion traiga mas.
constexpr int kPageSize = 20;
// La rejilla enseña unas dos filas: con la velocidad por defecto (16) cada
// muesca de rueda se saltaba una entera.
constexpr float kScrollSpeed = 5.f;
// Tope de paginas que rastrea la busqueda profunda.
constexpr int kDeepSearchRequests = 12;

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string buildShopInfo() {
    return
        "<cy>Tienda de cursores</c> - catalogos publicos de dos webs:\n\n"
        "<cg>RW-Designer</c>: sets completos de Windows (.cur/.ani). Cada cursor "
        "trae su rol, asi que <cj>Instalar set</c> rellena los estados solo.\n\n"
        "<cb>Custom-Cursor</c>: packs de dos piezas (flecha + puntero) en PNG.\n\n"
        "<cy>Buscar</c>: el cuadro filtra lo que ya esta en pantalla. Si no hay "
        "nada, sale un boton para rastrear mas paginas de la tienda.\n\n"
        "Toca una ficha para ver sus cursores, previsualizarlos y elegir a que "
        "estado va cada uno. Solo se descarga lo que instalas.";
}

} // namespace

CursorShopTab* CursorShopTab::create(CCSize size, std::function<void()> onInstalled) {
    auto ret = new CursorShopTab();
    ret->m_onInstalled = std::move(onInstalled);
    if (ret->initWithSize(size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CursorShopTab::initWithSize(CCSize size) {
    if (!CCNode::init()) return false;
    this->setContentSize(size);

    m_categories[0] = ShopClient::builtinCategories(Store::RwDesigner);
    m_categories[1] = ShopClient::builtinCategories(Store::CustomCursor);

    buildChrome(size);
    applyStoreStyle();
    updateChrome();
    return true;
}

void CursorShopTab::onShown() {
    if (m_loading || m_scanning || !m_loadedKey.empty()) return;
    selectStore(m_store);
}

void CursorShopTab::buildChrome(CCSize size) {
    float centerX = size.width / 2.f;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    float storeRowY = size.height - 56.f;
    char const* labels[] = {"RW-Designer", "Custom-Cursor"};
    for (int i = 0; i < kStoreCount; ++i) {
        auto* sprite = ButtonSprite::create(labels[i]);
        sprite->setScale(0.5f);
        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(CursorShopTab::onStoreButton));
        button->setTag(i);
        button->setPosition({centerX - 250.f + 80.f + i * 96.f, storeRowY});
        menu->addChild(button);
        m_storeSprites[i] = sprite;
    }

    m_search = TextInput::create(150.f, "Buscar...", "chatFont.fnt");
    m_search->setMaxCharCount(24);
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(0.85f);
    m_search->setPosition({size.width - 92.f, storeRowY});
    m_search->setCallback([this](std::string const& text) {
        stopDeepSearch();
        m_query = toLower(text);
        m_localPage = 0;

        // Al vaciar la busqueda se vuelve a la categoria que estaba antes.
        if (m_query.empty() && m_searchResults) {
            m_searchResults = false;
            m_loadedKey.clear();
            requestPage(0);
            return;
        }
        applyFilter();
        rebuildGrid();
        updateChrome();
    });
    this->addChild(m_search, 5);

    if (auto* info = PaimonInfo::createInfoBtn("Tienda de Cursores", buildShopInfo(), this, 0.5f)) {
        info->setPosition({size.width - 16.f, storeRowY});
        menu->addChild(info);
    }

    float categoryRowY = size.height - 80.f;
    if (auto* prev = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png")) {
        prev->setScale(0.45f);
        auto* button = CCMenuItemSpriteExtra::create(
            prev, this, menu_selector(CursorShopTab::onCategoryPrev));
        button->setPosition({centerX - 110.f, categoryRowY});
        menu->addChild(button);
    }
    if (auto* next = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png")) {
        next->setScale(0.45f);
        next->setFlipX(true);
        auto* button = CCMenuItemSpriteExtra::create(
            next, this, menu_selector(CursorShopTab::onCategoryNext));
        button->setPosition({centerX + 110.f, categoryRowY});
        menu->addChild(button);
    }

    m_categoryLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_categoryLabel->setScale(0.42f);
    m_categoryLabel->setPosition({centerX, categoryRowY});
    this->addChild(m_categoryLabel, 5);

    float gridBottom = 46.f;
    float gridWidth = size.width - 24.f;
    float gridHeight = size.height - 96.f - gridBottom;
    float gridMidY = gridBottom + gridHeight / 2.f;

    auto* backdrop = CCScale9Sprite::create("square02b_001.png");
    backdrop->setContentSize({gridWidth + 8.f, gridHeight + 8.f});
    backdrop->setColor({0, 0, 0});
    backdrop->setOpacity(90);
    backdrop->setPosition({centerX, gridMidY});
    this->addChild(backdrop, 0);

    m_grid = ScrollLayer::create({gridWidth, gridHeight});
    m_grid->setPosition({12.f, gridBottom});
    this->addChild(m_grid, 1);

    m_overlayLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_overlayLabel->setScale(0.32f);
    m_overlayLabel->setAlignment(kCCTextAlignmentCenter);
    m_overlayLabel->setPosition({centerX, gridMidY + 14.f});
    this->addChild(m_overlayLabel, 3);

    m_overlayButtonSprite = ButtonSprite::create("Buscar en la tienda", "goldFont.fnt",
                                                 "GJ_button_01.png", 0.7f);
    m_overlayButtonSprite->setScale(0.55f);
    m_overlayButton = CCMenuItemSpriteExtra::create(
        m_overlayButtonSprite, this, menu_selector(CursorShopTab::onOverlayAction));
    m_overlayButton->setPosition({centerX, gridMidY - 22.f});
    m_overlayButton->setVisible(false);
    menu->addChild(m_overlayButton);

    float pagerY = 24.f;
    if (auto* prev = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        prev->setScale(0.5f);
        auto* button = CCMenuItemSpriteExtra::create(
            prev, this, menu_selector(CursorShopTab::onPagePrev));
        button->setPosition({centerX - 52.f, pagerY});
        menu->addChild(button);
    }
    if (auto* next = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        next->setScale(0.5f);
        next->setFlipX(true);
        auto* button = CCMenuItemSpriteExtra::create(
            next, this, menu_selector(CursorShopTab::onPageNext));
        button->setPosition({centerX + 52.f, pagerY});
        menu->addChild(button);
    }

    auto* searchSprite = ButtonSprite::create("Buscar web", "goldFont.fnt",
                                              "GJ_button_01.png", 0.7f);
    searchSprite->setScale(0.42f);
    m_searchButton = CCMenuItemSpriteExtra::create(
        searchSprite, this, menu_selector(CursorShopTab::onOverlayAction));
    m_searchButton->setPosition({centerX - 125.f, pagerY});
    m_searchButton->setVisible(false);
    menu->addChild(m_searchButton);

    m_pageLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_pageLabel->setScale(0.32f);
    m_pageLabel->setPosition({centerX, pagerY});
    this->addChild(m_pageLabel, 5);

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(0.42f);
    m_statusLabel->setColor(kit::kDescColor);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setPosition({14.f, pagerY});
    this->addChild(m_statusLabel, 5);

    m_creditLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_creditLabel->setScale(0.38f);
    m_creditLabel->setColor(kit::kDescColor);
    m_creditLabel->setOpacity(150);
    m_creditLabel->setAnchorPoint({1.f, 0.5f});
    m_creditLabel->setPosition({size.width - 14.f, pagerY});
    this->addChild(m_creditLabel, 5);
}

Category const& CursorShopTab::currentCategory() const {
    static Category const empty{};
    if (m_searchResults) return m_searchCategory;
    auto const& list = m_categories[storeIndex()];
    if (list.empty()) return empty;
    int index = std::clamp(m_categoryIdx[storeIndex()], 0, static_cast<int>(list.size()) - 1);
    return list[index];
}

std::string CursorShopTab::listingKey() const {
    auto const& category = currentCategory();
    if (category.paged) {
        return fmt::format("{}|{}|{}", storeIndex(), category.id, m_serverPage);
    }
    return fmt::format("{}|{}", storeIndex(), category.id);
}

int CursorShopTab::localPagesPerFetch() const {
    auto const& category = currentCategory();
    if (m_searchResults || !category.paged || category.fetchSize <= 0) return 1;
    return std::max(1, (category.fetchSize + kPageSize - 1) / kPageSize);
}

int CursorShopTab::localPageCount() const {
    int pages = (static_cast<int>(m_filtered.size()) + kPageSize - 1) / kPageSize;
    return std::max(1, pages);
}

void CursorShopTab::applyStoreStyle() {
    for (int i = 0; i < kStoreCount; ++i) {
        if (!m_storeSprites[i]) continue;
        bool active = i == storeIndex();
        m_storeSprites[i]->setColor(active ? ccc3(0, 255, 0) : ccc3(255, 255, 255));
        m_storeSprites[i]->setOpacity(active ? 255 : 150);
    }
    if (m_creditLabel) m_creditLabel->setString(ShopClient::storeCredit(m_store));
}

void CursorShopTab::selectStore(Store store) {
    m_store = store;
    applyStoreStyle();
    ensureCategories();
    m_serverPage = 0;
    m_localPage = 0;
    m_pendingLocalPage = 0;
    m_searchResults = false;
    m_loadedKey.clear();
    requestPage(0);
}

void CursorShopTab::ensureCategories() {
    // Las colecciones recientes de custom-cursor se piden una vez por sesion;
    // si la peticion falla, el siguiente intento vuelve a probar.
    if (m_store != Store::CustomCursor) return;
    static bool loaded = false;
    if (loaded) return;

    WeakRef<CursorShopTab> self = this;
    ShopClient::fetchCategories(Store::CustomCursor, [self](std::vector<Category> categories) {
        auto locked = self.lock();
        if (!locked) return;
        auto* tab = static_cast<CursorShopTab*>(locked.data());
        if (!tab->m_alive || categories.empty()) return;
        if (categories.size() > ShopClient::builtinCategories(Store::CustomCursor).size()) {
            loaded = true;
        }

        auto current = tab->currentCategory().id;
        tab->m_categories[static_cast<int>(Store::CustomCursor)] = std::move(categories);

        auto const& list = tab->m_categories[static_cast<int>(Store::CustomCursor)];
        auto at = std::find_if(list.begin(), list.end(),
            [&](Category const& c) { return c.id == current; });
        tab->m_categoryIdx[static_cast<int>(Store::CustomCursor)] =
            at == list.end() ? 0 : static_cast<int>(at - list.begin());
        tab->updateChrome();
    });
}

void CursorShopTab::requestPage(int serverPage) {
    auto const& category = currentCategory();
    if (category.id.empty()) return;

    if (category.paged) {
        m_serverPage = std::max(0, serverPage);
        fetchListing();
        return;
    }

    if (m_loadedKey == listingKey()) {
        m_localPage = std::clamp(m_localPage, 0, localPageCount() - 1);
        rebuildGrid();
        updateChrome();
        return;
    }

    m_serverPage = 0;
    fetchListing();
}

void CursorShopTab::fetchListing() {
    if (m_loading) return;
    m_loading = true;

    ShopImages::get().forgetFailures();
    m_items.clear();
    m_filtered.clear();
    m_localPage = 0;
    rebuildGrid();
    setOverlay("Cargando catalogo...", kit::kValueColor);
    setOverlayAction("", false);
    updateChrome();

    auto store = m_store;
    auto category = currentCategory();
    auto page = m_serverPage;
    auto key = listingKey();

    WeakRef<CursorShopTab> self = this;
    ShopClient::fetchListing(store, category, page,
                             [self, key](Result<ListingPage> res) {
        auto locked = self.lock();
        if (!locked) return;
        auto* tab = static_cast<CursorShopTab*>(locked.data());
        if (!tab->m_alive) return;

        tab->m_loading = false;

        // El usuario pudo cambiar de tienda o categoria mientras cargaba.
        if (key != tab->listingKey()) return;

        if (!res) {
            tab->setOverlay(fmt::format("{}\nToca otra categoria para reintentar.",
                                        res.unwrapErr()), ccc3(255, 130, 130));
            tab->updateChrome();
            return;
        }

        auto listing = res.unwrap();
        tab->m_items = std::move(listing.items);
        tab->m_serverPageCount = std::max(1, listing.pageCount);
        tab->m_loadedKey = key;
        tab->applyFilter();
        // Al retroceder de pagina se entra por el final del bloque.
        tab->m_localPage = tab->m_pendingLocalPage < 0
            ? tab->localPageCount() - 1
            : std::clamp(tab->m_pendingLocalPage, 0, tab->localPageCount() - 1);
        tab->m_pendingLocalPage = 0;
        tab->rebuildGrid();
        tab->updateChrome();
    });
}

bool CursorShopTab::matchesQuery(Listing const& item) const {
    if (m_query.empty()) return true;
    return toLower(item.name).find(m_query) != std::string::npos
        || toLower(item.author).find(m_query) != std::string::npos;
}

void CursorShopTab::applyFilter() {
    m_filtered.clear();
    m_filtered.reserve(m_items.size());
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (matchesQuery(m_items[i])) m_filtered.push_back(i);
    }
    m_localPage = std::clamp(m_localPage, 0, localPageCount() - 1);
}

void CursorShopTab::rebuildGrid() {
    if (!m_grid || !m_grid->m_contentLayer) return;

    auto* layer = m_grid->m_contentLayer;
    layer->removeAllChildren();

    int first = m_localPage * kPageSize;
    int last = std::min(first + kPageSize, static_cast<int>(m_filtered.size()));
    int visible = std::max(0, last - first);

    auto viewSize = m_grid->getContentSize();
    // Mismo reparto que hace RowLayout; calcular de menos dejaba la rejilla
    // con filas fantasma y el contenido descolocado.
    int columns = std::max(1, static_cast<int>((viewSize.width + kCellGap) / (kCellWidth + kCellGap)));
    int rows = (visible + columns - 1) / columns;
    float gridHeight = std::max(viewSize.height, rows * (kCellHeight + kCellGap) + kCellGap);
    layer->setContentSize({viewSize.width, gridHeight});

    if (visible == 0) {
        // Durante el rastreo el mensaje lo lleva stepDeepSearch.
        if (!m_scanning && !m_loading && !m_items.empty()) {
            setOverlay("Nada coincide en esta pagina.", kit::kDescColor);
            setOverlayAction("Buscar en la tienda", !m_query.empty());
        }
        m_grid->scrollToTop();
        m_gridScrollTargetSet = false;
        return;
    }
    if (!m_scanning) {
        setOverlay("", kit::kDescColor);
        setOverlayAction("", false);
    }

    auto* holder = CCNode::create();
    holder->setContentSize({viewSize.width, gridHeight});
    holder->setLayout(
        RowLayout::create()
            ->setGap(kCellGap)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::End)
    );
    layer->addChild(holder);

    for (int slot = first; slot < last; ++slot) {
        int index = m_filtered[slot];
        auto const& item = m_items[index];

        auto* cell = CCNode::create();
        cell->setContentSize({kCellWidth, kCellHeight});
        cell->setAnchorPoint({0.5f, 0.5f});
        holder->addChild(cell);

        auto* bg = paimon::SpriteHelper::createColorPanel(
            kCellWidth, kCellHeight, ccc3(24, 28, 46), 170);
        bg->setPosition({0.f, 0.f});
        cell->addChild(bg, 0);

        auto* thumbSlot = CCNode::create();
        thumbSlot->setContentSize({kCellWidth - 10.f, kCellHeight - 26.f});
        thumbSlot->setPosition({5.f, 21.f});
        cell->addChild(thumbSlot, 1);
        mountThumb(thumbSlot, item.thumbUrl, kCellWidth - 14.f, kCellHeight - 30.f);

        auto* name = CCLabelBMFont::create(item.name.c_str(), "bigFont.fnt");
        name->setScale(0.25f);
        float nameWidth = name->getContentSize().width * 0.25f;
        if (nameWidth > kCellWidth - 8.f) {
            name->setScale(0.25f * (kCellWidth - 8.f) / nameWidth);
        }
        name->setPosition({kCellWidth / 2.f, 13.f});
        cell->addChild(name, 2);

        if (item.animated) {
            auto* badge = CCLabelBMFont::create("ANI", "bigFont.fnt");
            badge->setScale(0.22f);
            badge->setColor({255, 100, 100});
            badge->setAnchorPoint({0.f, 1.f});
            badge->setPosition({4.f, kCellHeight - 3.f});
            cell->addChild(badge, 3);
        }

        if (!item.extra.empty()) {
            auto* extra = CCLabelBMFont::create(item.extra.c_str(), "chatFont.fnt");
            extra->setScale(0.34f);
            extra->setColor(kit::kValueColor);
            extra->setAnchorPoint({1.f, 1.f});
            extra->setPosition({kCellWidth - 4.f, kCellHeight - 3.f});
            cell->addChild(extra, 3);
        }

        auto* cellMenu = CCMenu::create();
        cellMenu->setPosition({0.f, 0.f});
        cellMenu->setContentSize({kCellWidth, kCellHeight});
        cell->addChild(cellMenu, 5);

        auto* hit = CCSprite::create();
        hit->setContentSize({kCellWidth, kCellHeight});
        hit->setOpacity(0);
        auto* button = CCMenuItemSpriteExtra::create(
            hit, this, menu_selector(CursorShopTab::onCard));
        button->setContentSize({kCellWidth, kCellHeight});
        button->setPosition({kCellWidth / 2.f, kCellHeight / 2.f});
        button->setTag(index);
        cellMenu->addChild(button);
    }

    holder->updateLayout();
    m_grid->scrollToTop();
    m_gridScrollTargetSet = false;
}

void CursorShopTab::updateChrome() {
    auto const& category = currentCategory();

    if (m_categoryLabel) {
        m_categoryLabel->setString(category.name.empty() ? "-" : category.name.c_str());
        float width = m_categoryLabel->getContentSize().width * 0.42f;
        m_categoryLabel->setScale(width > 190.f ? 0.42f * 190.f / width : 0.42f);
    }

    if (m_pageLabel) {
        int perFetch = localPagesPerFetch();
        int page = m_serverPage * perFetch + m_localPage + 1;
        int total = m_searchResults || !category.paged
            ? localPageCount()
            : m_serverPageCount * perFetch;
        m_pageLabel->setString(fmt::format("Pag {}/{}", page, std::max(page, total)).c_str());
    }

    if (m_statusLabel) {
        if (m_loading) {
            m_statusLabel->setString("Cargando...");
        } else if (m_scanning) {
            m_statusLabel->setString("Buscando...");
        } else if (m_filtered.empty()) {
            m_statusLabel->setString("");
        } else {
            int first = m_localPage * kPageSize + 1;
            int last = std::min<int>((m_localPage + 1) * kPageSize,
                                     static_cast<int>(m_filtered.size()));
            m_statusLabel->setString(
                fmt::format("{}-{} de {}", first, last, m_filtered.size()).c_str());
        }
    }

    if (m_searchButton) {
        bool offer = !m_query.empty() && !m_searchResults && !m_scanning && !m_loading;
        m_searchButton->setVisible(offer);
        m_searchButton->setEnabled(offer);
    }
}

void CursorShopTab::setOverlay(std::string const& text, ccColor3B color) {
    if (!m_overlayLabel) return;
    m_overlayLabel->setString(text.c_str());
    m_overlayLabel->setColor(color);
    m_overlayLabel->setVisible(!text.empty());
}

void CursorShopTab::setOverlayAction(char const* label, bool visible) {
    if (!m_overlayButton) return;
    if (visible && label && *label && m_overlayButtonSprite) {
        m_overlayButtonSprite->setString(label);
    }
    m_overlayButton->setVisible(visible);
    m_overlayButton->setEnabled(visible);
}

std::vector<CursorShopTab::ScanTarget> CursorShopTab::buildScanTargets() const {
    std::vector<ScanTarget> targets;
    auto const& category = currentCategory();

    // En rw-designer el catalogo es una sola lista larguisima: se rastrean sus
    // primeras paginas, que son las mejor valoradas.
    if (category.paged) {
        int pages = std::min(kDeepSearchRequests, std::max(1, m_serverPageCount));
        for (int i = 0; i < pages; ++i) targets.push_back({category, i});
        return targets;
    }

    // En custom-cursor cada coleccion viene entera de una peticion, asi que se
    // recorren las colecciones.
    for (auto const& other : m_categories[storeIndex()]) {
        if (static_cast<int>(targets.size()) >= kDeepSearchRequests) break;
        targets.push_back({other, 0});
    }
    return targets;
}

void CursorShopTab::startDeepSearch() {
    if (m_query.empty() || m_scanning || m_loading) return;

    m_scanTargets = buildScanTargets();
    if (m_scanTargets.empty()) return;

    m_scanIndex = 0;
    m_scanning = true;
    m_searchCategory = ShopClient::searchCategory(m_store, m_query);
    m_searchResults = true;
    // Al salir de la busqueda habra que recargar la categoria.
    m_loadedKey.clear();
    ShopImages::get().forgetFailures();
    m_items.clear();
    m_filtered.clear();
    m_localPage = 0;
    m_serverPage = 0;
    rebuildGrid();
    stepDeepSearch();
}

void CursorShopTab::stepDeepSearch() {
    if (!m_scanning) return;
    if (m_scanIndex >= m_scanTargets.size()) {
        finishDeepSearch();
        return;
    }

    setOverlay(fmt::format("Buscando \"{}\"...\n{}/{} paginas  -  {} encontradas",
                           m_query, m_scanIndex, m_scanTargets.size(), m_filtered.size()),
               kit::kValueColor);
    setOverlayAction("Parar", true);
    updateChrome();

    auto target = m_scanTargets[m_scanIndex];
    auto store = m_store;

    WeakRef<CursorShopTab> self = this;
    ShopClient::fetchListing(store, target.category, target.page,
                             [self](Result<ListingPage> res) {
        auto locked = self.lock();
        if (!locked) return;
        auto* tab = static_cast<CursorShopTab*>(locked.data());
        if (!tab->m_alive || !tab->m_scanning) return;

        if (res) {
            for (auto& item : res.unwrap().items) {
                if (!tab->matchesQuery(item)) continue;
                bool known = std::any_of(tab->m_items.begin(), tab->m_items.end(),
                    [&](Listing const& other) { return other.id == item.id; });
                if (known) continue;
                tab->m_items.push_back(std::move(item));
            }
            // La rejilla se monta al terminar: asi el cartel de progreso no
            // acaba pintado encima de las fichas.
            tab->applyFilter();
        }

        ++tab->m_scanIndex;
        tab->stepDeepSearch();
    });
}

void CursorShopTab::finishDeepSearch() {
    m_scanning = false;
    applyFilter();
    rebuildGrid();

    if (m_filtered.empty()) {
        setOverlay(fmt::format("Sin resultados para \"{}\".\nPrueba otra palabra u otra categoria.",
                               m_query), kit::kDescColor);
        setOverlayAction("", false);
    }
    updateChrome();
}

void CursorShopTab::stopDeepSearch() {
    if (!m_scanning) return;
    m_scanning = false;
    setOverlayAction("", false);
}

void CursorShopTab::onOverlayAction(CCObject*) {
    if (m_scanning) {
        stopDeepSearch();
        finishDeepSearch();
        return;
    }
    startSearch();
}

void CursorShopTab::startSearch() {
    if (m_query.empty() || m_loading || m_scanning) return;

    // custom-cursor no deja consultar su buscador, asi que ahi toca rastrear.
    if (!ShopClient::supportsSearch(m_store)) {
        startDeepSearch();
        return;
    }

    m_searchCategory = ShopClient::searchCategory(m_store, m_query);
    m_searchResults = true;
    m_serverPage = 0;
    m_localPage = 0;
    m_pendingLocalPage = 0;
    m_loadedKey.clear();
    requestPage(0);
}

void CursorShopTab::onStoreButton(CCObject* sender) {
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    auto store = static_cast<Store>(std::clamp(button->getTag(), 0, kStoreCount - 1));
    if (store == m_store) return;

    stopDeepSearch();
    if (m_search) m_search->setString("");
    m_query.clear();
    selectStore(store);
}

void CursorShopTab::onCategoryPrev(CCObject*) {
    auto const& list = m_categories[storeIndex()];
    if (list.empty()) return;
    stopDeepSearch();
    m_searchResults = false;
    int count = static_cast<int>(list.size());
    m_categoryIdx[storeIndex()] = (m_categoryIdx[storeIndex()] + count - 1) % count;
    m_loadedKey.clear();
    m_pendingLocalPage = 0;
    requestPage(0);
    updateChrome();
}

void CursorShopTab::onCategoryNext(CCObject*) {
    auto const& list = m_categories[storeIndex()];
    if (list.empty()) return;
    stopDeepSearch();
    m_searchResults = false;
    int count = static_cast<int>(list.size());
    m_categoryIdx[storeIndex()] = (m_categoryIdx[storeIndex()] + 1) % count;
    m_loadedKey.clear();
    m_pendingLocalPage = 0;
    requestPage(0);
    updateChrome();
}

void CursorShopTab::onPagePrev(CCObject*) {
    if (m_loading || m_scanning) return;

    if (m_localPage > 0) {
        --m_localPage;
        rebuildGrid();
        updateChrome();
        return;
    }
    if (!m_searchResults && currentCategory().paged && m_serverPage > 0) {
        m_pendingLocalPage = -1;
        requestPage(m_serverPage - 1);
    }
}

void CursorShopTab::onPageNext(CCObject*) {
    if (m_loading || m_scanning) return;

    if (m_localPage + 1 < localPageCount()) {
        ++m_localPage;
        rebuildGrid();
        updateChrome();
        return;
    }
    if (!m_searchResults && currentCategory().paged && m_serverPage + 1 < m_serverPageCount) {
        m_pendingLocalPage = 0;
        requestPage(m_serverPage + 1);
    }
}

void CursorShopTab::onCard(CCObject* sender) {
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    int index = button->getTag();
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;

    auto callback = m_onInstalled;
    if (auto* popup = CursorShopDetailPopup::create(m_items[index], callback)) {
        popup->show();
    }
}

void CursorShopTab::handleScrollWheel(float x, float y) {
    kit::queueWheelScroll(m_grid, x, y, m_gridScrollTargetY, m_gridScrollTargetSet, kScrollSpeed);
}

void CursorShopTab::stepScroll(float dt) {
    kit::stepWheelScroll(m_grid, m_gridScrollTargetY, m_gridScrollTargetSet, dt);
}

void CursorShopTab::shutdown() {
    m_alive = false;
    m_scanning = false;
    paimon::ui::detachGeodeTextInput(m_search);
    ShopImages::get().clear();
}
