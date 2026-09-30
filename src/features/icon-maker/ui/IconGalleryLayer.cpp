#include "IconGalleryLayer.hpp"

#include "IconActionSheet.hpp"
#include "IconEditorLayer.hpp"
#include "IconHelpPopup.hpp"
#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "IconNamePopup.hpp"
#include "NewIconPopup.hpp"
#include "../data/IconAnatomy.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../services/IconApplier.hpp"
#include "../services/IconBuildService.hpp"
#include "../services/MoreIconsBridge.hpp"
#include "../services/IconShare.hpp"
#include "../services/IconThumbs.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

namespace {

constexpr float kHeaderH = 74.f;
constexpr float kCardW = 118.f;
constexpr float kCardH = 132.f;
constexpr float kCardGap = 10.f;

std::string formatDate(std::int64_t unixMs) {
    if (unixMs <= 0) return "-";
    std::time_t t = static_cast<std::time_t>(unixMs / 1000);
    std::tm tmv{};
#ifdef GEODE_IS_WINDOWS
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m/%Y", &tmv);
    return buf;
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// an icon is "in use" when active for its gamemode, via more icons or the own applier.
bool isInUse(IconIndexEntry const& entry) {
    if (MoreIconsBridge::available()) {
        return MoreIconsBridge::activeOursSlotId(entry.type) == entry.id;
    }
    return IconApplier::get().activeFor(entry.type) == entry.id;
}

IconIndexEntry const* entryFor(std::string const& id) {
    for (auto const& entry : IconProjectStore::get().list()) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

}  // anonymous namespace

IconGalleryLayer* IconGalleryLayer::create() {
    auto* ret = new IconGalleryLayer();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

CCScene* IconGalleryLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(IconGalleryLayer::create());
    return scene;
}

void IconGalleryLayer::open() {
    if (auto* layer = IconGalleryLayer::create()) {
        geode::pushSceneWithLayer(layer);
    }
}

bool IconGalleryLayer::init() {
    if (!CCLayer::init()) return false;
    setKeypadEnabled(true);
    setID("icon-maker-gallery"_spr);

    IconProjectStore::get().loadIndex();

    buildBackground();
    buildHeader();

    auto win = CCDirector::get()->getWinSize();
    m_scrollHost = CCNode::create();
    m_scrollHost->setPosition({20.f, 34.f});
    addChild(m_scrollHost, 5);

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.5f);
        m_statusLabel->setColor(kit::kDescColor);
        m_statusLabel->setPosition({win.width / 2.f, 16.f});
        addChild(m_statusLabel, 6);
    }

    rebuildGrid();
    scheduleUpdate();
    return true;
}

void IconGalleryLayer::buildBackground() {
    auto win = CCDirector::get()->getWinSize();

    // same backdrop as the game menus, so the gallery blends in.
    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX(win.width / bg->getContentSize().width);
        bg->setScaleY(win.height / bg->getContentSize().height);
        bg->setColor({26, 48, 110});
        addChild(bg, -5);
    } else {
        auto* flat = CCLayerColor::create(ccc4(22, 40, 92, 255));
        flat->setContentSize(win);
        addChild(flat, -5);
    }

    if (auto* bottomLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png")) {
        bottomLeft->setAnchorPoint({0, 0});
        bottomLeft->setPosition({-2, -2});
        bottomLeft->setOpacity(120);
        addChild(bottomLeft, -1);
    }
    if (auto* bottomRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png")) {
        bottomRight->setAnchorPoint({1, 0});
        bottomRight->setPosition({win.width + 2, -2});
        bottomRight->setFlipX(true);
        bottomRight->setOpacity(120);
        addChild(bottomRight, -1);
    }
}

void IconGalleryLayer::buildHeader() {
    auto win = CCDirector::get()->getWinSize();

    if (auto* title = CCLabelBMFont::create("Mis iconos", "goldFont.fnt")) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setScale(0.66f);
        title->setPosition({44.f, win.height - 22.f});
        addChild(title, 5);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    addChild(menu, 10);

    if (auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        spr->setScale(0.72f);
        auto* back = CCMenuItemExt::createSpriteExtra(spr,
            [this](CCMenuItemSpriteExtra*) { this->onBack(); });
        back->setPosition({20.f, win.height - 22.f});
        menu->addChild(back);
    }

    float x = win.width - 20.f;
    auto addButton = [&](char const* label, char const* sprite,
                         std::function<void()> action) {
        auto* spr = ButtonSprite::create(label, "goldFont.fnt", sprite, 0.8f);
        if (!spr) return;
        spr->setScale(0.6f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [action](CCMenuItemSpriteExtra*) { if (action) action(); });
        x -= btn->getScaledContentSize().width / 2.f;
        btn->setPosition({x, win.height - 22.f});
        menu->addChild(btn);
        x -= btn->getScaledContentSize().width / 2.f + 8.f;
    };

    addButton("Crear", "GJ_button_01.png", [this] { this->onNewIcon(); });
    addButton("Importar", "GJ_button_05.png", [this] { this->onImportIcon(); });

    if (auto* base = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_infoIcon_001.png", 1.f, CircleBaseColor::Cyan, CircleBaseSize::Small)) {
        base->setScale(0.8f);
        auto* help = CCMenuItemExt::createSpriteExtra(base,
            [](CCMenuItemSpriteExtra*) {
                if (auto* p = IconHelpPopup::create()) p->show();
            });
        x -= help->getScaledContentSize().width / 2.f;
        help->setPosition({x, win.height - 22.f});
        menu->addChild(help);
    }

    // search + sort share the second row so the grid keeps the whole width.
    float const searchW = std::min(240.f, win.width * 0.42f);
    m_search = TextInput::create(searchW, "Buscar por nombre...", "chatFont.fnt");
    if (m_search) {
        m_search->setPosition({24.f + searchW / 2.f, win.height - 52.f});
        Ref<IconGalleryLayer> self = this;
        m_search->setCallback([self](std::string const& value) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            self->m_query = lowered(value);
            // out of the input's own callback: rebuilding tears down the
            // scroll layer the touch dispatcher may still be walking.
            Loader::get()->queueInMainThread([self] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                if (self->getParent()) self->rebuildGrid();
            });
        });
        addChild(m_search, 6);
    }

    auto* sortMenu = CCMenu::create();
    sortMenu->setPosition({0.f, 0.f});
    addChild(sortMenu, 10);

    float const rowCY = win.height - 52.f;
    float const tabsW = std::min(168.f, win.width * 0.32f);
    auto* tabs = kit::makeTabBar(tabsW, {"Recientes", "Nombre", "Creacion"}, 0,
        [this](int index) {
            m_sort = index == 1 ? Sort::Name
                   : index == 2 ? Sort::Created : Sort::Recent;
            Ref<IconGalleryLayer> self = this;
            Loader::get()->queueInMainThread([self] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                self->rebuildGrid();
            });
        });
    if (tabs) {
        tabs->setPosition({win.width - 20.f - tabsW, rowCY - kit::kTabBarHeight / 2.f});
        addChild(tabs, 6);
    }

    float toolX = win.width - 26.f - tabsW;

    // gamemode filter hides behind a button: ten chips don't fit.
    auto* filterHolder = CCNode::create();
    filterHolder->setAnchorPoint({0.5f, 0.5f});
    filterHolder->setContentSize({64.f, 22.f});
    if (auto* plate = paimon::SpriteHelper::createColorPanel(
            64.f, 22.f, {14, 24, 52}, 210, 3.f)) {
        plate->setAnchorPoint({0.f, 0.f});
        filterHolder->addChild(plate, -1);
    }
    m_filterLabel = CCLabelBMFont::create("Todos", "bigFont.fnt");
    m_filterLabel->setAnchorPoint({0.5f, 0.5f});
    m_filterLabel->limitLabelWidth(58.f, 0.36f, 0.14f);
    m_filterLabel->setPosition({32.f, 11.f});
    filterHolder->addChild(m_filterLabel);

    if (auto* filterBtn = CCMenuItemExt::createSpriteExtra(filterHolder,
            [this](CCMenuItemSpriteExtra*) { this->onFilterMenu(); })) {
        toolX -= 32.f;
        filterBtn->setPosition({toolX, rowCY});
        sortMenu->addChild(filterBtn);
        toolX -= 32.f + 6.f;
    }

    if (auto* star = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png")) {
        star->setScale(0.7f);
        star->setOpacity(110);
        m_favGlyph = star;
        if (auto* favBtn = CCMenuItemExt::createSpriteExtra(star,
                [this](CCMenuItemSpriteExtra*) {
                    m_onlyFavorites = !m_onlyFavorites;
                    if (m_favGlyph) m_favGlyph->setOpacity(m_onlyFavorites ? 255 : 110);
                    Ref<IconGalleryLayer> self = this;
                    Loader::get()->queueInMainThread([self] {
                        if (paimon::isRuntimeShuttingDown() || !self) return;
                        self->rebuildGrid();
                    });
                })) {
            toolX -= favBtn->getScaledContentSize().width / 2.f;
            favBtn->setPosition({toolX, rowCY});
            sortMenu->addChild(favBtn);
        }
    }
}

void IconGalleryLayer::onFilterMenu() {
    Ref<IconGalleryLayer> self = this;
    std::vector<IconActionSheet::Action> actions;

    actions.push_back({"Todos los gamemodes", "", [self] {
        if (!self) return;
        self->m_typeFilter = -1;
        if (self->m_filterLabel) {
            self->m_filterLabel->setString("Todos");
            self->m_filterLabel->limitLabelWidth(58.f, 0.36f, 0.14f);
        }
        self->rebuildGrid();
    }, false});

    auto const& types = supportedTypes();
    for (std::size_t i = 0; i < types.size(); ++i) {
        auto const* def = anatomyFor(types[i]);
        if (!def) continue;
        std::string name(def->displayName);
        int const index = static_cast<int>(i);
        actions.push_back({name, "", [self, index, name] {
            if (!self) return;
            self->m_typeFilter = index;
            if (self->m_filterLabel) {
                self->m_filterLabel->setString(name.c_str());
                self->m_filterLabel->limitLabelWidth(58.f, 0.36f, 0.14f);
            }
            self->rebuildGrid();
        }, false});
    }

    if (auto* sheet = IconActionSheet::create("Ver solo", std::move(actions))) {
        sheet->show();
    }
}

void IconGalleryLayer::rebuildGrid() {
    if (!m_scrollHost) return;
    m_scrollHost->removeAllChildren();
    m_scroll = nullptr;
    m_wheelTargetSet = false;

    auto win = CCDirector::get()->getWinSize();
    float const scrollW = win.width - 40.f;
    float const scrollH = win.height - kHeaderH - 44.f;

    auto const& types = supportedTypes();
    std::vector<IconIndexEntry> entries;
    for (auto const& entry : IconProjectStore::get().list()) {
        if (!m_query.empty() && lowered(entry.name).find(m_query) == std::string::npos) {
            continue;
        }
        if (m_onlyFavorites && !entry.favorite) continue;
        if (m_typeFilter >= 0 && m_typeFilter < static_cast<int>(types.size()) &&
            entry.type != types[static_cast<std::size_t>(m_typeFilter)]) {
            continue;
        }
        entries.push_back(entry);
    }

    // favorites always on top; chosen sort decides the rest.
    Sort const sort = m_sort;
    std::sort(entries.begin(), entries.end(),
        [sort](IconIndexEntry const& a, IconIndexEntry const& b) {
            if (a.favorite != b.favorite) return a.favorite;
            switch (sort) {
                case Sort::Name:    return lowered(a.name) < lowered(b.name);
                case Sort::Created: return a.createdAt > b.createdAt;
                case Sort::Recent:
                default:            return a.modifiedAt > b.modifiedAt;
            }
        });

    if (entries.empty()) {
        auto* empty = mkui::makeEmptyState(scrollW,
            m_query.empty() && !m_onlyFavorites && m_typeFilter < 0
                ? "Todavia no tienes iconos"
                : "Nada con ese filtro",
            m_query.empty() && !m_onlyFavorites && m_typeFilter < 0
                ? "Toca Crear para hacer el primero. Puedes partir de un icono "
                  "del juego y solo cambiarle los colores."
                : "Prueba con otras palabras, o quita el filtro de gamemode y "
                  "el de favoritos.");
        if (empty) {
            empty->setPosition({0.f, scrollH / 2.f});
            m_scrollHost->addChild(empty);
        }
        return;
    }

    int perRow = std::max(1, static_cast<int>((scrollW + kCardGap) / (kCardW + kCardGap)));
    int rowCount = (static_cast<int>(entries.size()) + perRow - 1) / perRow;
    float gridW = static_cast<float>(perRow) * (kCardW + kCardGap) - kCardGap;
    float originX = (scrollW - gridW) / 2.f;

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({0.f, 0.f});
    m_scrollHost->addChild(m_scroll);

    auto* content = m_scroll->m_contentLayer;
    float contentH = std::max(scrollH,
        static_cast<float>(rowCount) * (kCardH + kCardGap) - kCardGap + 8.f);
    content->setContentSize({scrollW, contentH});

    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto* card = buildCard(entries[i].id, kCardW);
        if (!card) continue;
        int col = static_cast<int>(i) % perRow;
        int row = static_cast<int>(i) / perRow;
        card->setPosition({
            originX + static_cast<float>(col) * (kCardW + kCardGap),
            contentH - 4.f - static_cast<float>(row + 1) * kCardH
                - static_cast<float>(row) * kCardGap,
        });
        content->addChild(card);
    }

    m_scroll->moveToTop();
}

CCNode* IconGalleryLayer::buildCard(std::string const& id, float width) {
    auto const* entry = entryFor(id);
    if (!entry) return nullptr;

    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize({width, kCardH});

    if (auto* panel = kit::makePlate(width, kCardH)) {
        panel->setPosition({0.f, 0.f});
        card->addChild(panel, -1);
    }

    float const thumbBox = width - 20.f;
    float const thumbCY = kCardH - 12.f - thumbBox / 2.f;

    if (auto* well = paimon::SpriteHelper::createColorPanel(
            thumbBox, thumbBox, {0, 0, 0}, 110, 6.f)) {
        well->setAnchorPoint({0.f, 0.f});
        well->setPosition({10.f, thumbCY - thumbBox / 2.f});
        card->addChild(well);
    }

    // the real icon, rendered in the background; until it lands the card shows
    // the vanilla default of the gamemode so the grid never looks broken.
    auto* placeholder = SimplePlayer::create(1);
    if (placeholder) {
        placeholder->updatePlayerFrame(1, entry->type);
        placeholder->setPosition({width / 2.f, thumbCY});
        placeholder->setScale(0.85f);
        placeholder->setOpacity(90);
        card->addChild(placeholder, 1);
    }

    Ref<CCNode> cardRef = card;
    IconThumbs::get().request(id, [cardRef, width, thumbCY, thumbBox](CCTexture2D* texture) {
        if (paimon::isRuntimeShuttingDown() || !cardRef || !texture) return;
        auto* sprite = CCSprite::createWithTexture(texture);
        if (!sprite) return;
        float longest = std::max(sprite->getContentSize().width,
                                 sprite->getContentSize().height);
        if (longest > 0.f) sprite->setScale(thumbBox / longest);
        sprite->setPosition({width / 2.f, thumbCY});
        cardRef->addChild(sprite, 2);
    });

    if (auto* name = CCLabelBMFont::create(entry->name.c_str(), "bigFont.fnt")) {
        name->setAnchorPoint({0.5f, 0.5f});
        name->limitLabelWidth(width - 30.f, 0.4f, 0.14f);
        name->setPosition({width / 2.f - 6.f, 28.f});
        card->addChild(name, 3);
    }

    if (isInUse(*entry)) {
        if (auto* badge = paimon::SpriteHelper::createColorPanel(
                40.f, 12.f, {70, 200, 110}, 235, 3.f)) {
            badge->setAnchorPoint({1.f, 1.f});
            badge->setPosition({width - 7.f, kCardH - 7.f});
            card->addChild(badge, 4);
        }
        if (auto* text = CCLabelBMFont::create("En uso", "bigFont.fnt")) {
            text->setAnchorPoint({1.f, 1.f});
            text->limitLabelWidth(36.f, 0.3f, 0.12f);
            text->setPosition({width - 9.f, kCardH - 8.f});
            card->addChild(text, 5);
        }
    }

    std::string info = entry->hasBuiltOnce ? "Listo para usar" : "Sin terminar";
    if (auto const* def = anatomyFor(entry->type)) {
        info = fmt::format("{}  -  {}", def->displayName, formatDate(entry->modifiedAt));
    }
    if (auto* infoLbl = CCLabelBMFont::create(info.c_str(), "chatFont.fnt")) {
        infoLbl->setAnchorPoint({0.5f, 0.5f});
        infoLbl->setScale(0.36f);
        infoLbl->setColor(kit::kDescColor);
        infoLbl->limitLabelWidth((width - 16.f) / 0.36f, 0.36f, 0.16f);
        infoLbl->setPosition({width / 2.f, 13.f});
        card->addChild(infoLbl, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    card->addChild(menu, 5);

    // the card body opens the editor; the corner button holds everything else.
    auto* hit = CCNode::create();
    hit->setAnchorPoint({0.5f, 0.5f});
    hit->setContentSize({width, kCardH - 22.f});
    auto* openBtn = CCMenuItemExt::createSpriteExtra(hit,
        [this, id](CCMenuItemSpriteExtra*) { this->onEditIcon(id); });
    openBtn->setPosition({width / 2.f, kCardH / 2.f + 11.f});
    menu->addChild(openBtn);

    if (auto* more = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png")) {
        more->setScale(0.3f);
        auto* btn = CCMenuItemExt::createSpriteExtra(more,
            [this, id](CCMenuItemSpriteExtra*) { this->onIconMenu(id); });
        btn->setPosition({width - 13.f, 28.f});
        menu->addChild(btn);
    }

    if (auto* star = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png")) {
        star->setScale(0.5f);
        star->setOpacity(entry->favorite ? 255 : 90);
        auto* btn = CCMenuItemExt::createSpriteExtra(star,
            [this, id](CCMenuItemSpriteExtra*) { this->onToggleFavorite(id); });
        btn->setPosition({12.f, kCardH - 10.f});
        menu->addChild(btn);
    }

    return card;
}


void IconGalleryLayer::onEnter() {
    CCLayer::onEnter();
    // names, dates and thumbnails may have changed while we were in the editor.
    if (m_enteredOnce) rebuildGrid();
    m_enteredOnce = true;
}

void IconGalleryLayer::update(float dt) {
    kit::stepWheelScroll(m_scroll, m_wheelTargetY, m_wheelTargetSet, dt);
}

void IconGalleryLayer::scrollWheel(float x, float y) {
    if (kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet)) {
        return;
    }
    CCLayer::scrollWheel(x, y);
}

void IconGalleryLayer::keyBackClicked() {
    onBack();
}

void IconGalleryLayer::onBack() {
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}

void IconGalleryLayer::setStatus(std::string const& text) {
    if (m_statusLabel) m_statusLabel->setString(text.c_str());
}


void IconGalleryLayer::onNewIcon() {
    Ref<IconGalleryLayer> self = this;
    auto* popup = NewIconPopup::create([self](std::string const& slotId) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        self->rebuildGrid();
        self->onEditIcon(slotId);
    });
    if (popup) popup->show();
}

void IconGalleryLayer::onEditIcon(std::string const& slotId) {
    IconEditorLayer::open(slotId);
}

void IconGalleryLayer::onIconMenu(std::string const& slotId) {
    auto const* entry = entryFor(slotId);
    std::string title = entry ? entry->name : slotId;

    Ref<IconGalleryLayer> self = this;
    std::vector<IconActionSheet::Action> actions{
        {"Editar", "Abre el creador con este icono.",
         [self, slotId] { if (self) self->onEditIcon(slotId); }, false},
        {"Usar ahora", "Compila el icono y te lo pone en el kit.",
         [self, slotId] { if (self) self->onUseIcon(slotId); }, false},
        {"Renombrar", "", [self, slotId] { if (self) self->onRenameIcon(slotId); }, false},
        {"Duplicar", "Una copia para probar cambios sin miedo.",
         [self, slotId] { if (self) self->onDuplicateIcon(slotId); }, false},
        {"Compartir", "Crea un archivo .paimbicon para pasarselo a alguien.",
         [self, slotId] { if (self) self->onShareIcon(slotId); }, false},
        {"Borrar", "Se va para siempre.",
         [self, slotId] { if (self) self->onDeleteIcon(slotId); }, true},
    };

    if (auto* sheet = IconActionSheet::create(title, std::move(actions))) sheet->show();
}

void IconGalleryLayer::onToggleFavorite(std::string const& slotId) {
    auto const* entry = entryFor(slotId);
    if (!entry) return;
    bool const wanted = !entry->favorite;
    if (auto r = IconProjectStore::get().setFavorite(slotId, wanted); !r) {
        setStatus(r.unwrapErr());
        return;
    }
    rebuildGrid();
    setStatus(wanted ? "Marcado como favorito." : "Quitado de favoritos.");
}

void IconGalleryLayer::onUseIcon(std::string const& slotId) {
    if (m_busy) return;
    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded) {
        setStatus("No se pudo abrir el icono.");
        return;
    }

    m_busy = true;
    setStatus("Preparando el icono...");

    Ref<IconGalleryLayer> self = this;
    IconBuildService::buildAndApply(loaded.unwrap(),
        [self](geode::Result<std::string> result) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            self->m_busy = false;
            if (!result) {
                self->setStatus("Error: " + result.unwrapErr());
                return;
            }
            self->setStatus(result.unwrap());
            self->rebuildGrid();
        });
}

void IconGalleryLayer::onImportIcon() {
    Ref<IconGalleryLayer> self = this;
    IconShare::pickAndImport([self](geode::Result<std::string> res) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (!res) {
            Notification::create(("No se pudo importar: " + res.unwrapErr()).c_str(),
                NotificationIcon::Error, 3.f)->show();
            return;
        }
        Notification::create("Icono importado!", NotificationIcon::Success, 2.f)->show();
        self->rebuildGrid();
    });
}

void IconGalleryLayer::onShareIcon(std::string const& slotId) {
    auto exported = IconShare::exportProject(slotId);
    if (!exported) {
        Notification::create(("No se pudo exportar: " + exported.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.f)->show();
        return;
    }
    setStatus("Archivo .paimbicon creado.");
    geode::utils::file::openFolder(exported.unwrap().parent_path());
}

void IconGalleryLayer::onDuplicateIcon(std::string const& slotId) {
    auto dup = IconProjectStore::get().duplicateProject(slotId);
    if (!dup) {
        Notification::create(("No se pudo duplicar: " + dup.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.f)->show();
        return;
    }
    rebuildGrid();
    setStatus("Copia creada.");
}

void IconGalleryLayer::onRenameIcon(std::string const& slotId) {
    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded) return;

    Ref<IconGalleryLayer> self = this;
    auto* popup = IconNamePopup::create("Nombre del icono", "Mi icono",
        loaded.unwrap().name, [self, slotId](std::string const& name) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            auto again = IconProjectStore::get().loadProject(slotId);
            if (!again) return;
            auto project = again.unwrap();
            project.name = name;
            project.modifiedAt = nowUnixMs();
            if (auto r = IconProjectStore::get().saveProject(project); !r) {
                Notification::create(("No se pudo guardar: " + r.unwrapErr()).c_str(),
                    NotificationIcon::Error, 3.f)->show();
                return;
            }
            self->rebuildGrid();
        });
    if (popup) popup->show();
}

void IconGalleryLayer::onDeleteIcon(std::string const& slotId) {
    auto const* entry = entryFor(slotId);
    std::string name = entry ? entry->name : slotId;

    Ref<IconGalleryLayer> self = this;
    PopupManager::get().quickPopup(
        "Borrar icono",
        fmt::format("Seguro que quieres borrar <cy>{}</c>?\n"
                    "Esto no se puede deshacer.", name),
        "Cancelar", "Borrar",
        [self, slotId](FLAlertLayer*, bool confirmed) {
            if (!confirmed || paimon::isRuntimeShuttingDown() || !self) return;
            if (auto r = IconProjectStore::get().deleteProject(slotId); !r) {
                Notification::create(("No se pudo borrar: " + r.unwrapErr()).c_str(),
                    NotificationIcon::Error, 3.f)->show();
            }
            IconThumbs::get().invalidate(slotId);
            self->rebuildGrid();
        }).showInstant();
}

}  // namespace paimon::icon_maker
