#include "PaiDrawUI.hpp"

#include "PaiDrawIcon.hpp"
#include "../../utils/DynamicPopupRegistry.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/MenuGameLayer.hpp>

using namespace geode::prelude;

namespace paidraw {

namespace {

CCLabelBMFont* makeLabel(std::string const& text, char const* font, float scale, cocos2d::CCPoint pos,
    cocos2d::ccColor3B color = {255, 255, 255}, cocos2d::CCPoint anchor = {0.5f, 0.5f}) {
    auto* label = CCLabelBMFont::create(text.c_str(), font);
    label->setScale(scale);
    label->setPosition(pos);
    label->setColor(color);
    label->setAnchorPoint(anchor);
    return label;
}

CCMenuItemSpriteExtra* makeTextButton(cocos2d::CCNode* target, char const* text, cocos2d::SEL_MenuHandler cb,
    cocos2d::CCPoint pos, cocos2d::CCMenu* parent, float scale = 0.6f, char const* bg = "GJ_button_04.png") {
    auto* sprite = ButtonSprite::create(text, "bigFont.fnt", bg, .8f);
    sprite->setScale(scale);
    auto* button = CCMenuItemSpriteExtra::create(sprite, target, cb);
    button->setPosition(pos);
    parent->addChild(button);
    return button;
}

// ─────────────────────────────────────────────────────────────────────────────
// Paleta de tema 100% Geometry Dash.
//
// Antes el mod usaba tintes "PaimonDraw" (morados, fondos azul brillante).
// Ahora todo el chrome se construye con assets puros de GD y los colores
// canónicos del juego: oro de `goldFont.fnt`, verde "complete", rojo
// "fail", aqua "shop badge", etc. Cada panel se monta con NineSlice
// `GJ_square01..05.png` sin tintar (blanco puro), igual que los popups
// nativos del juego (`LevelInfoLayer`, `EditLevelLayer`, `LevelSelectLayer`).
// ─────────────────────────────────────────────────────────────────────────────

namespace theme {
    // ── Paneles: blanco puro (sin tintar) + opacidad. Es exactamente
    // como GD pinta `GJ_square01.png` en menús nativos. ────────────────
    constexpr cocos2d::ccColor3B kPanelTint        {255, 255, 255};
    constexpr cocos2d::ccColor3B kPanelInnerTint   {255, 255, 255};
    constexpr cocos2d::ccColor3B kDragBarTint      {255, 255, 255};

    // ── Acentos canónicos GD ──
    constexpr cocos2d::ccColor3B kAccentGold       {255, 217, 119}; // goldFont.fnt
    constexpr cocos2d::ccColor3B kAccentLightGold  {255, 240, 170};
    constexpr cocos2d::ccColor3B kAccentGreen      {102, 255, 102}; // lvl complete
    constexpr cocos2d::ccColor3B kAccentRed        {255,  71,  71}; // lvl fail
    constexpr cocos2d::ccColor3B kAccentAqua       {125, 200, 255}; // shop chest blue
    constexpr cocos2d::ccColor3B kAccentOrange     {255, 175,  90}; // editor accent

    // ── Texto secundario en paneles GD ──
    constexpr cocos2d::ccColor3B kTextOnDark       {255, 255, 255};
    constexpr cocos2d::ccColor3B kTextSubtle       {200, 210, 230};
    constexpr cocos2d::ccColor3B kTextMuted        {150, 160, 180};

    // ── Chip fills (semitransparente para que no choquen con el panel) ──
    constexpr cocos2d::ccColor3B kChipDarkFill     { 35,  40,  60};
    constexpr cocos2d::ccColor3B kChipDangerFill   { 80,  20,  20};
    constexpr cocos2d::ccColor3B kChipOkFill       { 20,  60,  20};
    constexpr cocos2d::ccColor3B kChipWarnFill     { 70,  50,  10};
    constexpr cocos2d::ccColor3B kChipGoldFill     { 60,  45,  15};
}

// Fondo GD-canónico: `MenuGameLayer` (el fondo animado de la pantalla
// principal del juego). Sin tintar. Exactamente lo que GD usa en
// `MenuLayer`, `CreatorLayer`, `LevelSelectLayer`. Cuando MenuGameLayer
// no esté disponible (caso raro), caemos a `GJ_gradientBG.png` también
// sin tintar para no introducir colores custom.
void addNativeBackground(CCLayer* layer, cocos2d::ccColor4B /*fallbackColor*/ = ccc4(0, 0, 0, 255)) {
    if (auto* bg = MenuGameLayer::create()) {
        layer->addChild(bg, -10);
        return;
    }
    auto win = CCDirector::sharedDirector()->getWinSize();
    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        auto bgSize = bg->getTextureRect().size;
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX((win.width  + 10.f) / std::max(bgSize.width,  1.f));
        bg->setScaleY((win.height + 10.f) / std::max(bgSize.height, 1.f));
        bg->setPosition({-5.f, -5.f});
        // Sin setColor: dejamos el gradiente original de GD.
        layer->addChild(bg, -10);
        return;
    }
    auto solid = CCLayerColor::create(ccc4(0, 0, 0, 255));
    solid->setContentSize(win);
    layer->addChild(solid, -10);
}

// Crea un panel GD-canónico: `GJ_square01.png` (el 9-slice de los popups
// de GD) sin tintar (blanco puro) y, opcionalmente, un frame interno
// `GJ_square05.png` para esa estética "doble borde" bevelado típica del
// editor / level select.
//
// Cadena de fallbacks (estilo `geode::Popup` + `textureldr::PackNode`):
//   1. `geode::NineSlice` — primitiva 9-slice nativa de Geode (preferida
//      porque HappyTextures y TexturePacks la respetan mejor que
//      `CCScale9Sprite`).
//   2. `CCScale9Sprite` — fallback si NineSlice no resolvió el frame.
//   3. CCDrawNode rounded rect pintado a mano — último recurso.
CCNode* makeFramedPanel(float width, float height,
    cocos2d::ccColor3B fillColor = {0, 0, 0},
    cocos2d::ccColor3B borderColor = {0, 0, 0},
    GLubyte opacity = 150) {
    auto* node = CCNode::create();
    node->setContentSize({width, height});
    node->setAnchorPoint({0.f, 0.f});

    // ── Capa principal: NineSlice GJ_square01.png (Geode-canónico) ──────
    bool placedMain = false;
    if (auto* main = paimon::SpriteHelper::safeCreateNineSlice("GJ_square01.png")) {
        main->setContentSize({width, height});
        main->setAnchorPoint({0.f, 0.f});
        main->setPosition({0.f, 0.f});
        main->setColor(fillColor);
        main->setOpacity(opacity);
        node->addChild(main, 0);
        placedMain = true;
    }
    if (!placedMain) {
        if (auto* main = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
            main->setContentSize({width, height});
            main->setAnchorPoint({0.f, 0.f});
            main->setPosition({0.f, 0.f});
            main->setColor(fillColor);
            main->setOpacity(opacity);
            node->addChild(main, 0);
        }
    }

    // ── Inset decorativo: GJ_square05.png más estrecho, mismo tono que
    // el borde, para crear una "ventana" como en el level select de GD.
    if (width > 64.f && height > 64.f) {
        bool placedInner = false;
        if (auto* inner = paimon::SpriteHelper::safeCreateNineSlice(
                "GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
            inner->setContentSize({width - 16.f, height - 16.f});
            inner->setAnchorPoint({0.5f, 0.5f});
            inner->setPosition({width / 2.f, height / 2.f});
            inner->setColor(borderColor);
            inner->setOpacity(70);
            node->addChild(inner, 1);
            placedInner = true;
        }
        if (!placedInner) {
            if (auto* inner = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
                inner->setContentSize({width - 16.f, height - 16.f});
                inner->setAnchorPoint({0.5f, 0.5f});
                inner->setPosition({width / 2.f, height / 2.f});
                inner->setColor(borderColor);
                inner->setOpacity(70);
                node->addChild(inner, 1);
            }
        }
    }

    return node;
}

// Crea una "drag bar" estilo GD (`GJ_dragBar_001.png`) — ese strip horizontal
// fino con gradiente — y le pega un título centrado con `goldFont.fnt`.
// Es el equivalente del `setTitle` que usa `Geode::Popup` pero con el
// recurso de drag bar para que se sienta auténticamente GD.
//
// Devuelve un CCNode con ancho `width` y alto del drag bar (~26 px). El
// llamador lo posiciona donde quiera dentro del panel.
CCNode* makeSectionStrip(std::string const& title, float width,
    cocos2d::ccColor3B stripTint = theme::kDragBarTint,
    cocos2d::ccColor3B titleColor = theme::kAccentGold,
    float fontScale = 0.62f) {
    auto* container = CCNode::create();

    float stripH = 26.f;
    bool placed = false;
    // Insets pequeños porque el drag bar tiene corners cortos.
    if (auto* drag = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_dragBar_001.png", {4.f, 8.f, 4.f, 8.f})) {
        drag->setContentSize({width, stripH});
        drag->setAnchorPoint({0.5f, 0.5f});
        drag->setPosition({width / 2.f, stripH / 2.f});
        drag->setColor(stripTint);
        drag->setOpacity(220);
        container->addChild(drag, 0);
        placed = true;
    }
    if (!placed) {
        if (auto* drag = paimon::SpriteHelper::safeCreateScale9("GJ_dragBar_001.png")) {
            drag->setContentSize({width, stripH});
            drag->setAnchorPoint({0.5f, 0.5f});
            drag->setPosition({width / 2.f, stripH / 2.f});
            drag->setColor(stripTint);
            drag->setOpacity(220);
            container->addChild(drag, 0);
        }
    }

    auto* label = CCLabelBMFont::create(title.c_str(), "goldFont.fnt");
    label->setScale(fontScale);
    label->setColor(titleColor);
    label->limitLabelWidth(width - 24.f, fontScale, 0.32f);
    label->setAnchorPoint({0.5f, 0.5f});
    label->setPosition({width / 2.f, stripH / 2.f + 1.f});
    container->addChild(label, 1);

    container->setContentSize({width, stripH});
    container->setAnchorPoint({0.f, 0.f});
    return container;
}

void updateButtonState(CCMenuItemSpriteExtra* button, bool active,
    char const* activeBg = "GJ_button_01.png", char const* inactiveBg = "GJ_button_04.png") {
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(button->getNormalImage())) {
        sprite->updateBGImage(active ? activeBg : inactiveBg);
        sprite->setColor(ccc3(255, 255, 255));
    }
}

CCNode* makePlayerIcon(PlayerInfo const& player, float size) {
    auto* gm = GameManager::get();
    auto iconID = std::max(player.iconID, 1);
    auto* simple = SimplePlayer::create(iconID);
    if (!simple) return nullptr;

    if (player.iconType > 0) {
        simple->updatePlayerFrame(iconID, static_cast<IconType>(player.iconType));
    }
    if (gm) {
        auto col1 = gm->colorForIdx(player.color1);
        auto col2 = gm->colorForIdx(player.color2);
        simple->setColor(col1);
        simple->setSecondColor(col2);
        if (player.glow) simple->setGlowOutline(col2);
        else simple->disableGlowOutline();
    }
    auto maxDim = std::max(simple->getContentSize().width, simple->getContentSize().height);
    simple->setScale(maxDim > 0.f ? size / maxDim : 0.7f);
    return simple;
}

void fillScroll(geode::ScrollLayer* scroll, std::vector<CCNode*> const& rows, float rowHeight, float padding = 6.f) {
    if (!scroll) return;
    auto* layer = scroll->m_contentLayer;
    if (!layer) return;
    layer->removeAllChildren();

    float width = scroll->getContentSize().width;
    float total = padding;
    for (auto* row : rows) {
        (void)row;
        total += rowHeight + padding;
    }
    total = std::max(total, scroll->getContentSize().height);
    layer->setContentSize({width, total});

    float y = total - padding - rowHeight / 2.f;
    for (auto* row : rows) {
        // Anclar al centro para que la fila ocupe [0, width] horizontalmente
        // dentro del scroll en vez de extenderse fuera del panel.
        row->setAnchorPoint({0.5f, 0.5f});
        row->setPosition({width / 2.f, y});
        layer->addChild(row);
        y -= rowHeight + padding;
    }
    layer->setPositionY(0.f);
}

// Chip estilo GD: usa `GJ_square05.png` (la mini-cápsula bevelada que GD
// usa para badges como "REWARD" o "NEW") tintada con el color de fondo.
// Si el frame no existe, cae a un rounded rect plano.
CCNode* makeChip(std::string const& text, cocos2d::ccColor3B textColor, cocos2d::ccColor3B fillColor,
    float padX = 10.f, float fontScale = 0.42f, char const* font = "chatFont.fnt") {
    auto* label = CCLabelBMFont::create(text.c_str(), font);
    label->setScale(fontScale);
    float w = std::max(label->getScaledContentSize().width + padX * 2.f, 36.f);
    float h = std::max(label->getScaledContentSize().height + 8.f, 18.f);

    auto* node = CCNode::create();
    node->setContentSize({w, h});
    node->setAnchorPoint({0.5f, 0.5f});

    bool placed = false;
    if (auto* bg = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        bg->setContentSize({w, h});
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({w / 2.f, h / 2.f});
        bg->setColor(fillColor);
        bg->setOpacity(235);
        node->addChild(bg, 0);
        placed = true;
    }
    if (!placed) {
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
            bg->setContentSize({w, h});
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->setPosition({w / 2.f, h / 2.f});
            bg->setColor(fillColor);
            bg->setOpacity(235);
            node->addChild(bg, 0);
        }
    }

    label->setColor(textColor);
    label->setAnchorPoint({0.5f, 0.5f});
    label->setPosition({w / 2.f, h / 2.f + 1.f});
    node->addChild(label, 1);
    return node;
}

// Botón circular con icono — equivalente al `CircleButtonSprite` que usa
// Geode (DarkPurple/DarkAqua/Green). Aquí lo construimos a mano para no
// depender de bindings concretos: un panel `GJ_square05.png` redondito
// más el icono encima. Devuelve un CCNode listo para envolver con
// `CCMenuItemSpriteExtra::create`.
CCNode* makeIconCircleNode(char const* iconFrame, cocos2d::ccColor3B baseColor,
                           float size = 30.f, float iconScale = 0.78f) {
    auto* node = CCNode::create();
    node->setContentSize({size, size});
    node->setAnchorPoint({0.5f, 0.5f});

    // Mini-panel `GJ_square05.png` redondeado (NineSlice preferido).
    bool placed = false;
    if (auto* bg = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        bg->setContentSize({size, size});
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({size / 2.f, size / 2.f});
        bg->setColor(baseColor);
        bg->setOpacity(245);
        node->addChild(bg, 0);
        placed = true;
    }
    if (!placed) {
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
            bg->setContentSize({size, size});
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->setPosition({size / 2.f, size / 2.f});
            bg->setColor(baseColor);
            bg->setOpacity(245);
            node->addChild(bg, 0);
        }
    }

    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
        float maxDim = std::max(icon->getContentSize().width, icon->getContentSize().height);
        if (maxDim > 0.f) icon->setScale(iconScale * size / maxDim);
        icon->setAnchorPoint({0.5f, 0.5f});
        icon->setPosition({size / 2.f, size / 2.f});
        node->addChild(icon, 1);
        return node;
    }
    // Si el icon frame no se carga, devolvemos null para que el llamador
    // pueda hacer un fallback de texto (preservar accesibilidad).
    return nullptr;
}

} // namespace

PaiDrawLobbyLayer* PaiDrawLobbyLayer::create() {
    auto* layer = new PaiDrawLobbyLayer();
    if (layer && layer->init()) {
        layer->autorelease();
        return layer;
    }
    CC_SAFE_DELETE(layer);
    return nullptr;
}

CCScene* PaiDrawLobbyLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(PaiDrawLobbyLayer::create());
    return scene;
}

bool PaiDrawLobbyLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);

    PaiDrawManager::get().init();
    PaiDrawManager::get().refreshLobby();

    buildLayout();
    refreshLists();

    WeakRef<PaiDrawLobbyLayer> weakSelf = this;
    m_connectionSub = paimon::EventBus::get().subscribe<ConnectionEvent>([weakSelf](ConnectionEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshLists();
    });
    m_lobbySub = paimon::EventBus::get().subscribe<LobbyUpdatedEvent>([weakSelf](LobbyUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshLists();
    });
    return true;
}

void PaiDrawLobbyLayer::keyBackClicked() {
    onBack(nullptr);
}

void PaiDrawLobbyLayer::buildLayout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu, 10);

    // ── Botón Atrás GD-canónico ─────────────────────────────────────────
    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawLobbyLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

    // ── Logo del header (icono PaiDraw + título) ────────────────────────
    if (auto* logoIcon = createPaiDrawIcon(34.f)) {
        logoIcon->setPosition({win.width / 2.f - 92.f, win.height - 24.f});
        this->addChild(logoIcon, 6);
    }

    auto* headerTitle = makeLabel("PAIDRAW", "goldFont.fnt", 0.92f,
        {win.width / 2.f, win.height - 22.f}, theme::kAccentGold);
    this->addChild(headerTitle, 6);

    auto* headerSub = makeLabel("Multiplayer Drawing Lobby", "goldFont.fnt", 0.48f,
        {win.width / 2.f, win.height - 44.f}, theme::kAccentLightGold);
    this->addChild(headerSub, 6);

    // Línea divisoria GD: degradado fino blanco translúcido
    auto* topSep = CCLayerColor::create({255, 255, 255, 60});
    topSep->setContentSize({win.width - 80.f, 1.f});
    topSep->setPosition({40.f, win.height - 56.f});
    this->addChild(topSep, 4);

    // ── Layout: panel central blanco GD-puro con la lista de jugadores ─
    constexpr float kBottomBar = 56.f;
    constexpr float kTopMargin = 64.f;
    constexpr float kSidePadding = 20.f;
    constexpr float kHeaderInside = 40.f;

    float panelTop = win.height - kTopMargin;
    float panelBottom = kBottomBar;
    float panelH = panelTop - panelBottom;
    float panelW = std::min(win.width - kSidePadding * 2.f, 720.f);
    float panelX = (win.width - panelW) / 2.f;
    float panelY = panelBottom;

    auto* panel = makeFramedPanel(panelW, panelH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    panel->setPosition({panelX, panelY});
    this->addChild(panel, 0);

    // ── Drag bar header del panel (estilo popups GD: "Choose Level") ────
    auto* dragHeader = makeSectionStrip("ONLINE PLAYERS", panelW - 20.f);
    dragHeader->setPosition({panelX + 10.f, panelTop - 30.f});
    this->addChild(dragHeader, 5);

    // Counter dorado a la derecha del drag bar (estilo "Stars: 1234")
    m_titleLabel = makeLabel("0 PLAYERS", "goldFont.fnt", 0.40f,
        {panelX + panelW - 26.f, panelTop - 17.f}, theme::kAccentLightGold,
        {1.f, 0.5f});
    this->addChild(m_titleLabel, 6);

    // Estado de conexión, pequeñito centrado debajo del header
    m_statusLabel = makeLabel("Conectando...", "chatFont.fnt", 0.55f,
        {panelX + panelW / 2.f, panelTop - 50.f}, theme::kTextSubtle);
    this->addChild(m_statusLabel, 5);

    // ── Lista scrollable ──
    constexpr float kScrollMargin = 12.f;
    float scrollTop = panelTop - kHeaderInside - 20.f;
    float scrollBottom = panelY + kScrollMargin;

    m_onlineScroll = geode::ScrollLayer::create({
        panelW - kScrollMargin * 2.f,
        scrollTop - scrollBottom
    });
    m_onlineScroll->setPosition({panelX + kScrollMargin, scrollBottom});
    this->addChild(m_onlineScroll, 3);

    // ── Botones inferiores grandes: JOIN ROOM y CREATE ROOM ──
    // Usamos el `GJ_button_01.png` (verde GD canónico, mismo que "PLAY"
    // en el menú principal) y `GJ_button_02.png` (azul GD canónico, mismo
    // que "ICONS" / "ACHIEVEMENTS").
    float btnY = kBottomBar / 2.f - 4.f;
    float halfWindowW = win.width / 2.f;

    auto* joinSprite = ButtonSprite::create("Join Room", "bigFont.fnt", "GJ_button_01.png", 0.9f);
    joinSprite->setScale(0.85f);
    auto* joinBtn = CCMenuItemSpriteExtra::create(
        joinSprite, this, menu_selector(PaiDrawLobbyLayer::onJoinRoom));
    joinBtn->setPosition({halfWindowW - 92.f, btnY});
    m_menu->addChild(joinBtn);

    auto* createSprite = ButtonSprite::create("Create Room", "bigFont.fnt", "GJ_button_02.png", 0.9f);
    createSprite->setScale(0.85f);
    auto* createBtn = CCMenuItemSpriteExtra::create(
        createSprite, this, menu_selector(PaiDrawLobbyLayer::onCreateRoom));
    createBtn->setPosition({halfWindowW + 92.f, btnY});
    m_menu->addChild(createBtn);

    // Botón refresh discreto a la derecha del header
    if (auto* refreshSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png")) {
        refreshSprite->setScale(0.55f);
        auto* refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSprite, this, menu_selector(PaiDrawLobbyLayer::onRefresh));
        refreshBtn->setPosition({win.width - 26.f, win.height - 22.f});
        m_menu->addChild(refreshBtn);
    }
}

void PaiDrawLobbyLayer::onCreateRoom(CCObject*) {
    CCDirector::sharedDirector()->pushScene(PaiDrawCreateRoomLayer::scene());
}

void PaiDrawLobbyLayer::onJoinRoom(CCObject*) {
    CCDirector::sharedDirector()->pushScene(PaiDrawRoomsLayer::scene());
}

void PaiDrawLobbyLayer::onRefresh(CCObject*) {
    PaiDrawManager::get().refreshLobby();
    refreshLists();
}

void PaiDrawLobbyLayer::onBack(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_connectionSub);
    paimon::EventBus::get().unsubscribe(m_lobbySub);
    CCDirector::sharedDirector()->popScene();
}

void PaiDrawLobbyLayer::updateHeader() {
    auto state = PaiDrawManager::get().snapshot();
    if (m_titleLabel) {
        // Counter al estilo "STARS: 1234" del menú principal de GD.
        m_titleLabel->setString(
            fmt::format("{} PLAYERS", state.onlineCount).c_str());
    }
    if (m_statusLabel) {
        if (state.authenticated) m_statusLabel->setString("Server connected");
        else if (state.connected) m_statusLabel->setString("Connected, authenticating...");
        else if (state.offlinePreview) m_statusLabel->setString("Offline preview active");
        else m_statusLabel->setString("Connecting...");
    }
}

CCNode* PaiDrawLobbyLayer::createPlayerRow(PlayerInfo const& player, float width, float height) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    // ── Fila estilo "comment cell" GD ────────────────────────────────────
    // Usamos `GJ_commentCell_001.png` (la cápsula 9-slice que GD usa para
    // las celdas de comentarios en `LevelInfoLayer` y `ProfilePage`). Sin
    // tintar — la dejamos blanca, que es el look GD nativo. Caemos a
    // `GJ_square03.png` y luego a `GJ_square01.png` si no carga.
    bool placedRowBg = false;
    if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
        cell->setContentSize({width, height});
        cell->setAnchorPoint({0.f, 0.f});
        cell->setOpacity(225);
        row->addChild(cell, 0);
        placedRowBg = true;
    }
    if (!placedRowBg) {
        if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice("GJ_square03.png")) {
            cell->setContentSize({width, height});
            cell->setAnchorPoint({0.f, 0.f});
            cell->setOpacity(180);
            row->addChild(cell, 0);
            placedRowBg = true;
        }
    }
    if (!placedRowBg) {
        if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice("GJ_square01.png")) {
            cell->setContentSize({width, height});
            cell->setAnchorPoint({0.f, 0.f});
            cell->setOpacity(160);
            row->addChild(cell, 0);
        }
    }

    // ── Avatar del jugador (icono GD canónico) ──────────────────────────
    if (auto* icon = makePlayerIcon(player, 32.f)) {
        icon->setPosition({28.f, height / 2.f});
        row->addChild(icon, 2);
    }

    // ── Nombre del jugador en bigFont blanco GD ─────────────────────────
    float textX = 56.f;
    auto* name = makeLabel(player.name, "bigFont.fnt", 0.50f,
        {textX, height / 2.f + 8.f}, theme::kTextOnDark, {0.f, 0.5f});
    name->limitLabelWidth(width - textX - 110.f, 0.50f, 0.28f);
    row->addChild(name, 3);

    // ── Subtítulo: nivel del jugador con icono de estrella GD ───────────
    if (auto* starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        float maxDim = std::max(starIcon->getContentSize().width, starIcon->getContentSize().height);
        if (maxDim > 0.f) starIcon->setScale(11.f / maxDim);
        starIcon->setPosition({textX + 6.f, height / 2.f - 9.f});
        row->addChild(starIcon, 3);
    }

    auto* level = makeLabel(fmt::format("Level {}", std::max(player.level, 1)),
        "chatFont.fnt", 0.55f,
        {textX + 14.f, height / 2.f - 9.f}, theme::kTextSubtle, {0.f, 0.5f});
    row->addChild(level, 3);

    // ── Chip de estado a la derecha (GD-canónico) ───────────────────────
    cocos2d::ccColor3B chipText = (player.status == PlayerStatus::Free)
        ? theme::kAccentGreen
        : theme::kAccentGold;
    cocos2d::ccColor3B chipFill = (player.status == PlayerStatus::Free)
        ? theme::kChipOkFill
        : theme::kChipGoldFill;

    auto* statusChip = makeChip(statusLabel(player.status), chipText, chipFill, 8.f, 0.40f);
    statusChip->setPosition({width - statusChip->getContentSize().width / 2.f - 10.f, height / 2.f});
    row->addChild(statusChip, 4);

    return row;
}

void PaiDrawLobbyLayer::rebuildOnlineList() {
    auto state = PaiDrawManager::get().snapshot();
    std::vector<CCNode*> rows;
    rows.reserve(state.onlinePlayers.size());
    float width = m_onlineScroll->getContentSize().width;
    for (auto const& player : state.onlinePlayers) {
        rows.push_back(createPlayerRow(player, width, 50.f));
    }
    fillScroll(m_onlineScroll, rows, 50.f, 6.f);
}

void PaiDrawLobbyLayer::refreshLists() {
    updateHeader();
    rebuildOnlineList();
}

// ────────────────────────────────────────────────────────────
// PaiDrawRoomsLayer — listado de salas activas para unirse
// ────────────────────────────────────────────────────────────

PaiDrawRoomsLayer* PaiDrawRoomsLayer::create() {
    auto* layer = new PaiDrawRoomsLayer();
    if (layer && layer->init()) {
        layer->autorelease();
        return layer;
    }
    CC_SAFE_DELETE(layer);
    return nullptr;
}

CCScene* PaiDrawRoomsLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(PaiDrawRoomsLayer::create());
    return scene;
}

bool PaiDrawRoomsLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);

    PaiDrawManager::get().refreshLobby();
    buildLayout();
    refreshLists();

    WeakRef<PaiDrawRoomsLayer> weakSelf = this;
    m_connectionSub = paimon::EventBus::get().subscribe<ConnectionEvent>([weakSelf](ConnectionEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshLists();
    });
    m_lobbySub = paimon::EventBus::get().subscribe<LobbyUpdatedEvent>([weakSelf](LobbyUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshLists();
    });
    return true;
}

void PaiDrawRoomsLayer::keyBackClicked() {
    onBack(nullptr);
}

void PaiDrawRoomsLayer::buildLayout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 10);

    // ── Botón Atrás GD-canónico ─────────────────────────────────────────
    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawRoomsLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

    // ── Header con icono PaiDraw GD ─────────────────────────────────────
    if (auto* logoIcon = createPaiDrawIcon(30.f)) {
        logoIcon->setPosition({win.width / 2.f - 110.f, win.height - 23.f});
        this->addChild(logoIcon, 6);
    }

    auto* headerTitle = makeLabel("ACTIVE ROOMS", "goldFont.fnt", 0.85f,
        {win.width / 2.f, win.height - 22.f}, theme::kAccentGold);
    this->addChild(headerTitle, 6);

    auto* headerSub = makeLabel("Pick a room to join", "goldFont.fnt", 0.46f,
        {win.width / 2.f, win.height - 44.f}, theme::kAccentLightGold);
    this->addChild(headerSub, 6);

    auto* topSep = CCLayerColor::create({255, 255, 255, 60});
    topSep->setContentSize({win.width - 80.f, 1.f});
    topSep->setPosition({40.f, win.height - 56.f});
    this->addChild(topSep, 4);

    // ── Panel central ──
    constexpr float kBottomBar = 56.f;
    constexpr float kTopMargin = 64.f;
    constexpr float kSidePadding = 20.f;

    float panelTop = win.height - kTopMargin;
    float panelBottom = kBottomBar;
    float panelH = panelTop - panelBottom;
    float panelW = std::min(win.width - kSidePadding * 2.f, 720.f);
    float panelX = (win.width - panelW) / 2.f;
    float panelY = panelBottom;

    auto* panel = makeFramedPanel(panelW, panelH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    panel->setPosition({panelX, panelY});
    this->addChild(panel, 0);

    // Drag bar header del panel
    auto* dragHeader = makeSectionStrip("ROOM LIST", panelW - 20.f);
    dragHeader->setPosition({panelX + 10.f, panelTop - 30.f});
    this->addChild(dragHeader, 5);

    // Counter dorado
    m_titleLabel = makeLabel("0 ROOMS", "goldFont.fnt", 0.40f,
        {panelX + panelW - 26.f, panelTop - 17.f}, theme::kAccentLightGold,
        {1.f, 0.5f});
    this->addChild(m_titleLabel, 6);

    // Refresh discreto a la derecha del header (nivel del header de pantalla)
    if (auto* refreshSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png")) {
        refreshSprite->setScale(0.55f);
        auto* refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSprite, this, menu_selector(PaiDrawRoomsLayer::onRefresh));
        refreshBtn->setPosition({win.width - 26.f, win.height - 22.f});
        m_menu->addChild(refreshBtn);
    }

    // Lista
    constexpr float kScrollMargin = 12.f;
    constexpr float kHeaderInside = 40.f;
    float scrollTop = panelTop - kHeaderInside - 20.f;
    float scrollBottom = panelY + kScrollMargin;

    m_roomScroll = geode::ScrollLayer::create({
        panelW - kScrollMargin * 2.f,
        scrollTop - scrollBottom
    });
    m_roomScroll->setPosition({panelX + kScrollMargin, scrollBottom});
    this->addChild(m_roomScroll, 3);

    // Mensaje "no hay salas" — con un icono de candado/pintura
    m_emptyLabel = makeLabel("No active rooms. Create one!",
        "bigFont.fnt", 0.55f,
        {panelX + panelW / 2.f, panelY + panelH / 2.f},
        theme::kTextSubtle);
    m_emptyLabel->setVisible(false);
    this->addChild(m_emptyLabel, 4);

    // ── Botón inferior: CREATE ROOM (verde GD canónico) ────────────────
    float btnY = kBottomBar / 2.f - 4.f;
    auto* createSprite = ButtonSprite::create("Create Room", "bigFont.fnt", "GJ_button_01.png", 0.9f);
    createSprite->setScale(0.85f);
    auto* createBtn = CCMenuItemSpriteExtra::create(
        createSprite, this, menu_selector(PaiDrawRoomsLayer::onCreateRoom));
    createBtn->setPosition({win.width / 2.f, btnY});
    m_menu->addChild(createBtn);
}

void PaiDrawRoomsLayer::onBack(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_connectionSub);
    paimon::EventBus::get().unsubscribe(m_lobbySub);
    CCDirector::sharedDirector()->popScene();
}

void PaiDrawRoomsLayer::onRefresh(CCObject*) {
    PaiDrawManager::get().refreshLobby();
    refreshLists();
}

void PaiDrawRoomsLayer::onCreateRoom(CCObject*) {
    CCDirector::sharedDirector()->pushScene(PaiDrawCreateRoomLayer::scene());
}

void PaiDrawRoomsLayer::onJoinRoom(CCObject* sender) {
    auto roomId = static_cast<uint32_t>(sender->getTag());
    PaiDrawManager::get().joinRoom(roomId);
    CCDirector::sharedDirector()->pushScene(PaiDrawRoomLayer::scene());
}

CCNode* PaiDrawRoomsLayer::createRoomRow(RoomInfo const& room, float width, float height) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    bool inGame = room.state == RoomState::InGame;

    // ── Comment cell GD-canónico ────────────────────────────────────────
    bool placedRowBg = false;
    if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
        cell->setContentSize({width, height});
        cell->setAnchorPoint({0.f, 0.f});
        cell->setOpacity(225);
        row->addChild(cell, 0);
        placedRowBg = true;
    }
    if (!placedRowBg) {
        if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice("GJ_square03.png")) {
            cell->setContentSize({width, height});
            cell->setAnchorPoint({0.f, 0.f});
            cell->setOpacity(180);
            row->addChild(cell, 0);
        }
    }

    constexpr float kJoinW = 76.f;
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    // ── Botón JOIN/LOCK con sprites GD ──────────────────────────────────
    // Si la sala tiene contraseña, usamos `GJ_button_06.png` (gris, mismo
    // que "BACK" en GD) para denotar que requiere acción extra. Sin
    // contraseña, `GJ_button_01.png` (verde, mismo que "PLAY").
    auto* joinSprite = ButtonSprite::create(
        room.hasPassword ? "Locked" : "Join", "bigFont.fnt",
        room.hasPassword ? "GJ_button_06.png" : "GJ_button_01.png", 0.7f);
    joinSprite->setScale(0.62f);
    auto* joinBtn = CCMenuItemSpriteExtra::create(
        joinSprite, this, menu_selector(PaiDrawRoomsLayer::onJoinRoom));
    joinBtn->setPosition({width - kJoinW / 2.f - 8.f, height / 2.f});
    joinBtn->setTag(static_cast<int>(room.id));
    menu->addChild(joinBtn);

    float textX = 14.f;

    // ── Icono "lock" GD si la sala tiene contraseña ─────────────────────
    if (room.hasPassword) {
        if (auto* lock = paimon::SpriteHelper::safeCreateWithFrameName("GJ_lock_001.png")) {
            float maxDim = std::max(lock->getContentSize().width, lock->getContentSize().height);
            if (maxDim > 0.f) lock->setScale(20.f / maxDim);
            lock->setColor(theme::kAccentGold);
            lock->setPosition({textX + 10.f, height - 16.f});
            row->addChild(lock, 3);
            textX += 22.f;
        }
    }

    float textRight = width - kJoinW - 18.f;
    float textW = textRight - textX;

    auto* nameLabel = CCLabelBMFont::create(room.config.name.c_str(), "goldFont.fnt");
    nameLabel->setScale(0.56f);
    nameLabel->limitLabelWidth(textW * 0.65f, 0.56f, 0.32f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({textX, height - 16.f});
    nameLabel->setColor(theme::kAccentGold);
    row->addChild(nameLabel, 2);

    // Chip de estado verde/rojo (verde = waiting, rojo = in game)
    auto* stateChip = makeChip(roomStateLabel(room.state),
        inGame ? theme::kAccentRed : theme::kAccentGreen,
        inGame ? theme::kChipDangerFill : theme::kChipOkFill,
        7.f, 0.34f);
    float chipX = textX + nameLabel->getScaledContentSize().width + 10.f
                  + stateChip->getContentSize().width / 2.f;
    chipX = std::min(chipX, textRight - stateChip->getContentSize().width / 2.f);
    stateChip->setPosition({chipX, height - 16.f});
    row->addChild(stateChip, 3);

    // Host (con icono de jugador GD pequeño)
    if (auto* hostIcon = paimon::SpriteHelper::safeCreateWithFrameName("playerCubeIcon_001.png")) {
        float maxDim = std::max(hostIcon->getContentSize().width, hostIcon->getContentSize().height);
        if (maxDim > 0.f) hostIcon->setScale(11.f / maxDim);
        hostIcon->setColor(theme::kAccentLightGold);
        hostIcon->setPosition({textX + 6.f, height / 2.f});
        row->addChild(hostIcon, 3);
    }

    auto* hostLabel = CCLabelBMFont::create(
        fmt::format("Host: {}", room.hostName).c_str(), "chatFont.fnt");
    hostLabel->setScale(0.55f);
    hostLabel->limitLabelWidth(textW - 16.f, 0.55f, 0.32f);
    hostLabel->setAnchorPoint({0.f, 0.5f});
    hostLabel->setPosition({textX + 14.f, height / 2.f});
    hostLabel->setColor(theme::kTextOnDark);
    row->addChild(hostLabel, 2);

    // Modo + jugadores
    auto* metaLabel = CCLabelBMFont::create(
        fmt::format("{}  {}/{} players", modeLabel(room.config.mode),
            room.playerCount(), room.config.maxPlayers).c_str(),
        "chatFont.fnt");
    metaLabel->setScale(0.52f);
    metaLabel->limitLabelWidth(textW, 0.52f, 0.30f);
    metaLabel->setAnchorPoint({0.f, 0.5f});
    metaLabel->setPosition({textX, 14.f});
    metaLabel->setColor(theme::kTextSubtle);
    row->addChild(metaLabel, 2);

    return row;
}

void PaiDrawRoomsLayer::rebuildRoomList() {
    auto state = PaiDrawManager::get().snapshot();
    std::vector<CCNode*> rows;
    rows.reserve(state.rooms.size());
    float width = m_roomScroll->getContentSize().width;
    for (auto const& room : state.rooms) {
        rows.push_back(createRoomRow(room, width, 64.f));
    }
    fillScroll(m_roomScroll, rows, 64.f, 8.f);

    if (m_emptyLabel) {
        m_emptyLabel->setVisible(rows.empty());
    }
}

void PaiDrawRoomsLayer::refreshLists() {
    if (m_titleLabel) {
        auto state = PaiDrawManager::get().snapshot();
        m_titleLabel->setString(
            fmt::format("{} ROOMS", state.rooms.size()).c_str());
    }
    rebuildRoomList();
}

PaiDrawCreateRoomLayer* PaiDrawCreateRoomLayer::create(bool editMode) {
    auto* ret = new PaiDrawCreateRoomLayer();
    if (ret && ret->init(editMode)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaiDrawCreateRoomLayer::scene(bool editMode) {
    auto* scene = CCScene::create();
    scene->addChild(PaiDrawCreateRoomLayer::create(editMode));
    return scene;
}

bool PaiDrawCreateRoomLayer::init(bool editMode) {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    m_editMode = editMode;
    buildLayout();
    loadInitialValues();
    refreshSelectionColors();
    return true;
}

void PaiDrawCreateRoomLayer::keyBackClicked() {
    onBack(nullptr);
}

void PaiDrawCreateRoomLayer::buildLayout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 10);

    // ── Header con botón atrás GD ───────────────────────────────────────
    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawCreateRoomLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

    // Logo PaiDraw a la izquierda del título
    if (auto* logoIcon = createPaiDrawIcon(28.f)) {
        logoIcon->setPosition({win.width / 2.f - (m_editMode ? 75.f : 80.f), win.height - 24.f});
        this->addChild(logoIcon, 6);
    }

    m_titleLabel = makeLabel(m_editMode ? "EDIT ROOM" : "CREATE ROOM",
        "goldFont.fnt", 0.85f,
        {win.width / 2.f, win.height - 22.f}, theme::kAccentGold);
    this->addChild(m_titleLabel, 6);

    this->addChild(makeLabel(
        m_editMode ? "Update existing room settings" : "Configure your match",
        "goldFont.fnt", 0.46f,
        {win.width / 2.f, win.height - 44.f}, theme::kAccentLightGold), 6);

    auto* separator = CCLayerColor::create({255, 255, 255, 60});
    separator->setContentSize({win.width - 80.f, 1.f});
    separator->setPosition({40.f, win.height - 56.f});
    this->addChild(separator, 4);

    // ── Layout: panel central GD blanco con dos columnas ──
    constexpr float kBottomBar = 56.f;
    constexpr float kHeaderArea = 64.f;
    float panelTop = win.height - kHeaderArea;
    float panelBottom = kBottomBar + 6.f;
    float panelH = panelTop - panelBottom;
    float panelW = std::min(win.width - 40.f, 600.f);
    float panelX = (win.width - panelW) / 2.f;
    float panelY = panelBottom;

    auto* panel = makeFramedPanel(panelW, panelH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    panel->setPosition({panelX, panelY});
    this->addChild(panel, 0);

    // Drag bar GD canónico que dice "ROOM SETTINGS"
    auto* dragHeader = makeSectionStrip("ROOM SETTINGS", panelW - 20.f);
    dragHeader->setPosition({panelX + 10.f, panelTop - 30.f});
    this->addChild(dragHeader, 5);

    // Layout interno: dos columnas
    constexpr float kPad = 16.f;
    float innerW = panelW - kPad * 2.f;
    constexpr float kColGap = 18.f;
    float colW = (innerW - kColGap) / 2.f;
    float leftColX = panelX + kPad;
    float rightColX = leftColX + colW + kColGap;
    float innerTop = panelY + panelH - kPad - 28.f; // espacio para drag bar

    auto sectionLabel = [&](char const* text, float x, float y, cocos2d::ccColor3B color) {
        auto* lbl = makeLabel(text, "goldFont.fnt", 0.42f,
            {x, y}, color, {0.f, 0.5f});
        this->addChild(lbl, 5);
        return lbl;
    };

    // ──────── COLUMNA IZQUIERDA ────────
    constexpr float kLeftRow = 64.f;
    float yL = innerTop - 12.f;

    sectionLabel("ROOM NAME", leftColX, yL, theme::kAccentGold);
    m_nameInput = geode::TextInput::create(colW, "Room name");
    m_nameInput->setPosition({leftColX + colW / 2.f, yL - 24.f});
    m_nameInput->setMaxCharCount(32);
    this->addChild(m_nameInput, 5);

    yL -= kLeftRow;
    sectionLabel("PASSWORD", leftColX, yL, theme::kAccentGold);
    m_passwordInput = geode::TextInput::create(colW,
        m_editMode ? "Set on create" : "Optional");
    m_passwordInput->setPosition({leftColX + colW / 2.f, yL - 24.f});
    m_passwordInput->setMaxCharCount(24);
    this->addChild(m_passwordInput, 5);

    yL -= kLeftRow;
    sectionLabel("MAX PLAYERS", leftColX, yL, theme::kAccentAqua);

    // Badge GD-canónico (GJ_square05.png) con el contador
    if (auto* badgeBg = paimon::SpriteHelper::safeCreateNineSlice("GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        badgeBg->setContentSize({40.f, 22.f});
        badgeBg->setOpacity(225);
        badgeBg->setAnchorPoint({0.f, 0.f});
        badgeBg->setPosition({leftColX + colW - 40.f, yL - 11.f});
        this->addChild(badgeBg, 4);
    }

    m_playerCountLabel = makeLabel(std::to_string(m_maxPlayers),
        "goldFont.fnt", 0.50f,
        {leftColX + colW - 20.f, yL}, theme::kAccentGold);
    this->addChild(m_playerCountLabel, 5);

    // Slider GD-canónico (Slider::create usa los assets de GD)
    m_playerSlider = Slider::create(this,
        menu_selector(PaiDrawCreateRoomLayer::onPlayersChanged), 0.55f);
    m_playerSlider->setPosition({leftColX + colW / 2.f, yL - 26.f});
    this->addChild(m_playerSlider, 5);

    // ──────── COLUMNA DERECHA ────────
    constexpr float kRightRow = 44.f;

    auto addChoiceRow = [&](char const* title, float baseY,
        std::vector<std::pair<std::string, int>> const& entries,
        std::vector<CCMenuItemSpriteExtra*>& buttons,
        cocos2d::SEL_MenuHandler cb,
        cocos2d::ccColor3B titleColor)
    {
        sectionLabel(title, rightColX, baseY, titleColor);

        float btnY = baseY - 22.f;
        size_t count = entries.size();
        constexpr float kBtnGap = 6.f;
        float btnW = (colW - kBtnGap * static_cast<float>(count - 1)) / static_cast<float>(count);
        for (size_t i = 0; i < count; ++i) {
            auto const& [text, tag] = entries[i];
            // GJ_button_04.png = gris (estado inactivo);
            // updateButtonState lo cambia a GJ_button_01.png cuando se selecciona.
            auto* sprite = ButtonSprite::create(text.c_str(), "bigFont.fnt", "GJ_button_04.png", 0.7f);
            float scaleX = (btnW - 4.f) / std::max(sprite->getContentSize().width, 1.f);
            scaleX = std::clamp(scaleX, 0.30f, 0.55f);
            sprite->setScale(scaleX);
            auto* btn = CCMenuItemSpriteExtra::create(sprite, this, cb);
            btn->setPosition({
                rightColX + btnW / 2.f + static_cast<float>(i) * (btnW + kBtnGap),
                btnY
            });
            btn->setTag(tag);
            m_menu->addChild(btn);
            buttons.push_back(btn);
        }
    };

    float yR = innerTop - 12.f;
    addChoiceRow("ROUNDS", yR,
        {{"3", 3}, {"6", 6}, {"10", 10}},
        m_roundButtons,
        menu_selector(PaiDrawCreateRoomLayer::onRounds),
        theme::kAccentAqua);

    yR -= kRightRow;
    addChoiceRow("ROUND TIME", yR,
        {{"60s", 60}, {"90s", 90}, {"120s", 120}},
        m_timeButtons,
        menu_selector(PaiDrawCreateRoomLayer::onTime),
        theme::kAccentAqua);

    yR -= kRightRow;
    addChoiceRow("GAME MODE", yR,
        {{"Classic", static_cast<int>(GameMode::Classic)},
         {"Animation", static_cast<int>(GameMode::Animation)},
         {"Chain", static_cast<int>(GameMode::Chain)}},
        m_modeButtons,
        menu_selector(PaiDrawCreateRoomLayer::onMode),
        theme::kAccentOrange);

    yR -= kRightRow;
    addChoiceRow("LANGUAGE", yR,
        {{"Spanish", static_cast<int>(WordLanguage::Spanish)},
         {"English", static_cast<int>(WordLanguage::English)},
         {"Both", static_cast<int>(WordLanguage::Both)}},
        m_languageButtons,
        menu_selector(PaiDrawCreateRoomLayer::onLanguage),
        theme::kAccentOrange);

    // ── Botón final, separado del panel ──
    auto* createSprite = ButtonSprite::create(
        m_editMode ? "Apply" : "Create Room",
        "bigFont.fnt", "GJ_button_01.png", 0.9f);
    createSprite->setScale(0.85f);
    auto* createBtn = CCMenuItemSpriteExtra::create(
        createSprite, this, menu_selector(PaiDrawCreateRoomLayer::onCreate));
    createBtn->setPosition({win.width / 2.f, kBottomBar / 2.f - 4.f});
    m_menu->addChild(createBtn);
}

void PaiDrawCreateRoomLayer::loadInitialValues() {
    if (m_editMode) {
        auto config = PaiDrawManager::get().snapshot().currentRoom.config;
        m_mode = config.mode;
        m_rounds = config.rounds;
        m_timeSeconds = config.roundTimeSeconds;
        m_language = config.language;
        m_maxPlayers = config.maxPlayers;
        if (m_nameInput) m_nameInput->setString(config.name);
    }

    if (m_playerSlider) {
        m_playerSlider->setValue((m_maxPlayers - 2) / 23.f);
    }
    onPlayersChanged(nullptr);
}

void PaiDrawCreateRoomLayer::onBack(CCObject*) {
    CCDirector::sharedDirector()->popScene();
}

void PaiDrawCreateRoomLayer::onPlayersChanged(CCObject*) {
    m_maxPlayers = 2 + static_cast<int>(std::round(m_playerSlider->getValue() * 23.f));
    if (m_playerCountLabel) {
        m_playerCountLabel->setString(std::to_string(m_maxPlayers).c_str());
    }
}

void PaiDrawCreateRoomLayer::onMode(CCObject* sender) {
    m_mode = static_cast<GameMode>(sender->getTag());
    refreshSelectionColors();
}

void PaiDrawCreateRoomLayer::onRounds(CCObject* sender) {
    m_rounds = sender->getTag();
    refreshSelectionColors();
}

void PaiDrawCreateRoomLayer::onTime(CCObject* sender) {
    m_timeSeconds = sender->getTag();
    refreshSelectionColors();
}

void PaiDrawCreateRoomLayer::onLanguage(CCObject* sender) {
    m_language = static_cast<WordLanguage>(sender->getTag());
    refreshSelectionColors();
}

void PaiDrawCreateRoomLayer::refreshSelectionColors() {
    for (auto* button : m_modeButtons) {
        updateButtonState(button, button->getTag() == static_cast<int>(m_mode));
    }
    for (auto* button : m_roundButtons) {
        updateButtonState(button, button->getTag() == m_rounds);
    }
    for (auto* button : m_timeButtons) {
        updateButtonState(button, button->getTag() == m_timeSeconds);
    }
    for (auto* button : m_languageButtons) {
        updateButtonState(button, button->getTag() == static_cast<int>(m_language));
    }
}

void PaiDrawCreateRoomLayer::onCreate(CCObject*) {
    RoomConfig config;
    config.name = m_nameInput ? sanitizeRoomName(m_nameInput->getString()) : "";
    config.password = (!m_editMode && m_passwordInput) ? m_passwordInput->getString() : "";
    config.maxPlayers = m_maxPlayers;
    config.rounds = m_rounds;
    config.roundTimeSeconds = m_timeSeconds;
    config.mode = m_mode;
    config.language = m_language;

    if (config.name.empty()) {
        PaimonNotify::show("Give the room a name first", NotificationIcon::Warning);
        return;
    }

    if (m_editMode) {
        PaiDrawManager::get().updateRoomConfig(config);
        CCDirector::sharedDirector()->popScene();
        return;
    }

    PaiDrawManager::get().createRoom(config);
    CCDirector::sharedDirector()->replaceScene(PaiDrawRoomLayer::scene());
}

PaiDrawRoomLayer* PaiDrawRoomLayer::create() {
    auto* layer = new PaiDrawRoomLayer();
    if (layer && layer->init()) {
        layer->autorelease();
        return layer;
    }
    CC_SAFE_DELETE(layer);
    return nullptr;
}

CCScene* PaiDrawRoomLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(PaiDrawRoomLayer::create());
    return scene;
}

bool PaiDrawRoomLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    buildLayout();
    refreshRoom();
    WeakRef<PaiDrawRoomLayer> weakSelf = this;
    m_roomSub = paimon::EventBus::get().subscribe<RoomUpdatedEvent>([weakSelf](RoomUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshRoom();
    });
    m_chatSub = paimon::EventBus::get().subscribe<ChatUpdatedEvent>([weakSelf](ChatUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->rebuildChat();
    });
    return true;
}

void PaiDrawRoomLayer::keyBackClicked() { onBack(nullptr); }

void PaiDrawRoomLayer::buildLayout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu, 10);

    // ── Header con botón atrás GD ───────────────────────────────────────
    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawRoomLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

    m_roomTitle = makeLabel("PaiDraw Room", "goldFont.fnt", 0.85f,
        {win.width / 2.f, win.height - 22.f}, theme::kAccentGold);
    this->addChild(m_roomTitle, 5);

    m_roomMeta = makeLabel("Waiting for players...", "goldFont.fnt", 0.46f,
        {win.width / 2.f, win.height - 44.f}, theme::kAccentLightGold);
    this->addChild(m_roomMeta, 5);

    auto* separator = CCLayerColor::create({255, 255, 255, 60});
    separator->setContentSize({win.width - 80.f, 1.f});
    separator->setPosition({40.f, win.height - 56.f});
    this->addChild(separator, 4);

    // ── Layout: dos paneles GD blancos ─────────────────────────────────
    constexpr float kSidePadding = 18.f;
    constexpr float kPanelGap = 14.f;
    constexpr float kBottomBar = 60.f;
    constexpr float kHeaderArea = 64.f;

    float panelTop = win.height - kHeaderArea;
    float panelBottom = kBottomBar;
    float panelH = panelTop - panelBottom;
    float availableW = win.width - kSidePadding * 2.f - kPanelGap;
    float leftW = std::floor(availableW * 0.40f);
    float rightW = availableW - leftW;
    float leftX = kSidePadding;
    float rightX = leftX + leftW + kPanelGap;

    auto* leftPanel = makeFramedPanel(leftW, panelH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    leftPanel->setPosition({leftX, panelBottom});
    this->addChild(leftPanel, 0);

    auto* rightPanel = makeFramedPanel(rightW, panelH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    rightPanel->setPosition({rightX, panelBottom});
    this->addChild(rightPanel, 0);

    // Drag bars GD-canónicos para los headers de cada panel
    auto* leftHeader = makeSectionStrip("PLAYERS", leftW - 16.f);
    leftHeader->setPosition({leftX + 8.f, panelBottom + panelH - 30.f});
    this->addChild(leftHeader, 5);

    auto* rightHeader = makeSectionStrip("ROOM CHAT", rightW - 16.f);
    rightHeader->setPosition({rightX + 8.f, panelBottom + panelH - 30.f});
    this->addChild(rightHeader, 5);

    constexpr float kScrollMargin = 12.f;
    constexpr float kHeaderInside = 36.f;
    constexpr float kChatInputArea = 38.f;

    m_playerScroll = geode::ScrollLayer::create({
        leftW - kScrollMargin * 2.f,
        panelH - kHeaderInside - kScrollMargin
    });
    m_playerScroll->setPosition({leftX + kScrollMargin, panelBottom + kScrollMargin});
    this->addChild(m_playerScroll, 3);

    m_chatScroll = geode::ScrollLayer::create({
        rightW - kScrollMargin * 2.f,
        panelH - kHeaderInside - kScrollMargin - kChatInputArea
    });
    m_chatScroll->setPosition({rightX + kScrollMargin, panelBottom + kScrollMargin + kChatInputArea});
    this->addChild(m_chatScroll, 3);

    // ── Input chat dentro del panel derecho ──
    float chatInputY = panelBottom + kScrollMargin + 14.f;
    float chatInputW = rightW - kScrollMargin * 2.f - 70.f;
    m_chatInput = geode::TextInput::create(chatInputW, "Type a message...");
    m_chatInput->setPosition({rightX + kScrollMargin + chatInputW / 2.f, chatInputY});
    this->addChild(m_chatInput, 5);

    auto* sendSprite = ButtonSprite::create("Send", "bigFont.fnt", "GJ_button_03.png", 0.7f);
    sendSprite->setScale(0.55f);
    auto* sendBtn = CCMenuItemSpriteExtra::create(
        sendSprite, this, menu_selector(PaiDrawRoomLayer::onSendChat));
    sendBtn->setPosition({rightX + rightW - 30.f, chatInputY});
    m_menu->addChild(sendBtn);

    // ── Botones inferiores GD canónicos ─────────────────────────────────
    // Ready (verde GD), Start (azul GD), Settings (gris GD).
    float btnY = kBottomBar / 2.f - 4.f;
    makeTextButton(this, "Ready", menu_selector(PaiDrawRoomLayer::onReady),
        {leftX + 50.f, btnY}, m_menu, 0.65f, "GJ_button_01.png");
    makeTextButton(this, "Start", menu_selector(PaiDrawRoomLayer::onStart),
        {leftX + 138.f, btnY}, m_menu, 0.65f, "GJ_button_02.png");

    // Settings: usamos un botón de icono (engranaje GD) para la
    // configuración, en lugar del texto "CONFIG", para coincidir con el
    // patrón del menú principal de GD donde "OPTIONS" usa el engranaje.
    if (auto* gear = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png")) {
        gear->setScale(0.55f);
        auto* gearBtn = CCMenuItemSpriteExtra::create(
            gear, this, menu_selector(PaiDrawRoomLayer::onOpenCreate));
        gearBtn->setPosition({leftX + 220.f, btnY});
        m_menu->addChild(gearBtn);
    } else {
        makeTextButton(this, "Settings", menu_selector(PaiDrawRoomLayer::onOpenCreate),
            {leftX + 220.f, btnY}, m_menu, 0.58f, "GJ_button_06.png");
    }
}

CCNode* PaiDrawRoomLayer::createPlayerRow(PlayerInfo const& player, float width, float height) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    // ── Comment cell GD-canónico (sin tinte morado) ─────────────────────
    bool placedRowBg = false;
    if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
        cell->setContentSize({width, height});
        cell->setAnchorPoint({0.f, 0.f});
        cell->setOpacity(player.host ? 240 : 220);
        // El host tiene un sutil tinte dorado para distinguirlo, pero
        // sin perder la identidad GD-canónica de la celda.
        if (player.host) cell->setColor(theme::kAccentLightGold);
        row->addChild(cell, 0);
        placedRowBg = true;
    }
    if (!placedRowBg) {
        if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice("GJ_square03.png")) {
            cell->setContentSize({width, height});
            cell->setAnchorPoint({0.f, 0.f});
            cell->setOpacity(180);
            if (player.host) cell->setColor(theme::kAccentLightGold);
            row->addChild(cell, 0);
        }
    }

    // ── Avatar GD canónico (icono SimplePlayer del jugador) ─────────────
    if (auto* icon = makePlayerIcon(player, 26.f)) {
        icon->setPosition({22.f, height / 2.f});
        row->addChild(icon, 2);
    }

    // ── Nombre + ping ───────────────────────────────────────────────────
    float textX = 42.f;
    auto* name = makeLabel(player.name, "bigFont.fnt", 0.45f,
        {textX, height / 2.f + 7.f}, theme::kTextOnDark, {0.f, 0.5f});
    name->limitLabelWidth(width - textX - 80.f, 0.45f, 0.24f);
    row->addChild(name, 3);

    auto* ping = makeLabel(fmt::format("{} ms", player.pingMs),
        "chatFont.fnt", 0.55f,
        {textX, height / 2.f - 9.f}, theme::kTextSubtle, {0.f, 0.5f});
    row->addChild(ping, 3);

    // ── Chips a la derecha ──────────────────────────────────────────────
    cocos2d::ccColor3B chipText = player.ready ? theme::kAccentGreen : theme::kAccentRed;
    cocos2d::ccColor3B chipFill = player.ready ? theme::kChipOkFill : theme::kChipDangerFill;
    auto* readyChip = makeChip(player.ready ? "READY" : "NOT READY",
        chipText, chipFill, 6.f, 0.36f);
    readyChip->setPosition({width - readyChip->getContentSize().width / 2.f - 6.f,
                            player.host ? height / 2.f - 10.f : height / 2.f});
    row->addChild(readyChip, 4);

    if (player.host) {
        auto* hostChip = makeChip("HOST", theme::kAccentGold, theme::kChipGoldFill, 6.f, 0.36f);
        hostChip->setPosition({width - hostChip->getContentSize().width / 2.f - 6.f, height / 2.f + 11.f});
        row->addChild(hostChip, 4);
    }
    return row;
}

void PaiDrawRoomLayer::rebuildPlayers() {
    auto room = PaiDrawManager::get().snapshot().currentRoom;
    std::vector<CCNode*> rows;
    float width = m_playerScroll->getContentSize().width;
    for (auto const& player : room.players) {
        rows.push_back(createPlayerRow(player, width, 46.f));
    }
    fillScroll(m_playerScroll, rows, 46.f, 6.f);
}

void PaiDrawRoomLayer::rebuildChat() {
    auto messages = PaiDrawManager::get().snapshot().roomChat;
    std::vector<CCNode*> rows;
    float width = m_chatScroll->getContentSize().width;
    for (auto const& message : messages) {
        auto* row = CCNode::create();
        row->setContentSize({width, 36.f});
        row->setAnchorPoint({0.5f, 0.5f});

        // Cell GD-canónico para mensajes; tinta sutil cuando el mensaje
        // es correcto (verde GD) o cerca (oro GD).
        cocos2d::ccColor3B cellTint = theme::kPanelTint;
        if (message.correct)        cellTint = theme::kAccentGreen;
        else if (message.nearGuess) cellTint = theme::kAccentLightGold;

        bool placedRowBg = false;
        if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice(
                "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
            cell->setContentSize({width, 36.f});
            cell->setAnchorPoint({0.f, 0.f});
            cell->setColor(cellTint);
            cell->setOpacity(message.correct || message.nearGuess ? 220 : 200);
            row->addChild(cell, 0);
            placedRowBg = true;
        }
        if (!placedRowBg) {
            if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice("GJ_square03.png")) {
                cell->setContentSize({width, 36.f});
                cell->setAnchorPoint({0.f, 0.f});
                cell->setColor(cellTint);
                cell->setOpacity(160);
                row->addChild(cell, 0);
            }
        }

        // Sender en oro o blanco según tipo de mensaje
        auto* sender = makeLabel(message.senderName,
            message.system ? "goldFont.fnt" : "bigFont.fnt", 0.36f,
            {8.f, 24.f},
            message.correct ? theme::kAccentGreen
                : message.nearGuess ? theme::kAccentGold
                : theme::kTextOnDark,
            {0.f, 0.5f});
        sender->limitLabelWidth(width - 16.f, 0.36f, 0.22f);
        row->addChild(sender, 2);

        auto* text = makeLabel(message.text, "chatFont.fnt", 0.55f,
            {8.f, 10.f}, theme::kTextSubtle, {0.f, 0.5f});
        text->limitLabelWidth(width - 16.f, 0.55f, 0.30f);
        row->addChild(text, 2);
        rows.push_back(row);
    }
    fillScroll(m_chatScroll, rows, 36.f, 5.f);
}

void PaiDrawRoomLayer::refreshRoom() {
    auto room = PaiDrawManager::get().snapshot().currentRoom;
    if (m_roomTitle) m_roomTitle->setString(room.config.name.empty() ? "PaiDraw Room" : room.config.name.c_str());
    if (m_roomMeta) {
        m_roomMeta->setString(fmt::format("{} - {} rounds - {}s",
            modeLabel(room.config.mode), room.config.rounds, room.config.roundTimeSeconds).c_str());
    }
    rebuildPlayers();
    rebuildChat();
}

void PaiDrawRoomLayer::onBack(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_roomSub);
    paimon::EventBus::get().unsubscribe(m_chatSub);
    PaiDrawManager::get().leaveRoom();
    CCDirector::sharedDirector()->popScene();
}

void PaiDrawRoomLayer::onReady(CCObject*) {
    PaiDrawManager::get().toggleReady();
}

void PaiDrawRoomLayer::onStart(CCObject*) {
    PaiDrawManager::get().startGame();
    CCDirector::sharedDirector()->pushScene(PaiDrawGameLayer::scene());
}

void PaiDrawRoomLayer::onSendChat(CCObject*) {
    if (!m_chatInput) return;
    auto text = m_chatInput->getString();
    PaiDrawManager::get().sendRoomChat(text);
    m_chatInput->setString("");
}

void PaiDrawRoomLayer::onOpenCreate(CCObject*) {
    CCDirector::sharedDirector()->pushScene(PaiDrawCreateRoomLayer::scene(true));
}

PaiDrawCanvasNode* PaiDrawCanvasNode::create() {
    auto* node = new PaiDrawCanvasNode();
    if (node && node->init()) {
        node->autorelease();
        return node;
    }
    CC_SAFE_DELETE(node);
    return nullptr;
}

bool PaiDrawCanvasNode::init() {
    if (!CCLayer::init()) return false;
    this->setContentSize({kCanvasWidth, kCanvasHeight});
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);

    // ── Lienzo de fondo: blanco con borde GD-canónico ───────────────────
    // El "papel" real donde se dibuja: rounded rect blanco. Mantenemos el
    // CCDrawNode aquí (no `GJ_square01.png` blanco) para que el fondo del
    // canvas siempre sea perfectamente plano, sin gradientes del 9-slice.
    auto* paper = paimon::SpriteHelper::createRoundedRect(kCanvasWidth, kCanvasHeight, 10.f,
        {1.f, 1.f, 1.f, 1.f}, {0.22f, 0.22f, 0.28f, 1.f}, 1.5f);
    paper->setPosition({0.f, 0.f});
    this->addChild(paper, 0);

    // Frame decorativo bevelado tipo GD, 9-slice por encima del papel.
    // Usa `GJ_square05.png` (la cápsula bevelada del editor) sin tintar
    // (blanco puro) para que la mesa de dibujo tenga el mismo lenguaje
    // visual que `LevelInfoLayer`. Preferimos `geode::NineSlice` (el
    // primitivo que usan textureldr y los popups de Geode) — cae a
    // CCScale9Sprite si no está disponible.
    bool placedFrame = false;
    if (auto* frame = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        frame->setContentSize({kCanvasWidth + 6.f, kCanvasHeight + 6.f});
        frame->setAnchorPoint({0.5f, 0.5f});
        frame->setPosition({kCanvasWidth / 2.f, kCanvasHeight / 2.f});
        // Sin setColor: blanco puro GD-canónico.
        frame->setOpacity(230);
        this->addChild(frame, 1);
        placedFrame = true;
    }
    if (!placedFrame) {
        if (auto* frame = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
            frame->setContentSize({kCanvasWidth + 6.f, kCanvasHeight + 6.f});
            frame->setAnchorPoint({0.5f, 0.5f});
            frame->setPosition({kCanvasWidth / 2.f, kCanvasHeight / 2.f});
            frame->setOpacity(230);
            this->addChild(frame, 1);
        }
    }

    // Clipper interno (recorta los strokes al area util del lienzo)
    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(kCanvasWidth - 8.f, kCanvasHeight - 8.f, 8.f);
    m_clipper = CCClippingNode::create(stencil);
    m_clipper->setPosition({4.f, 4.f});
    this->addChild(m_clipper, 2);

    m_strokeLayer = CCNode::create();
    m_strokeLayer->setContentSize({kCanvasWidth - 8.f, kCanvasHeight - 8.f});
    m_clipper->addChild(m_strokeLayer, 0);

    // Capa de previsualizacion en vivo (linea, formas)
    m_previewNode = PaimonDrawNode::create();
    m_clipper->addChild(m_previewNode, 100);

    return true;
}

void PaiDrawCanvasNode::setTool(Tool tool) {
    m_tool = tool;
    clearPreview();
}

void PaiDrawCanvasNode::clearPreview() {
    if (m_previewNode) m_previewNode->clear();
}

void PaiDrawCanvasNode::clearRedoStack() {
    for (auto* node : m_redoStack) {
        if (node) {
            node->removeFromParentAndCleanup(true);
        }
    }
    m_redoStack.clear();
}

void PaiDrawCanvasNode::clearCanvas() {
    if (m_strokeLayer) m_strokeLayer->removeAllChildren();
    m_groupStack.clear();
    clearRedoStack();
    clearPreview();
}

void PaiDrawCanvasNode::beginStrokeGroup() {
    m_currentGroup = CCNode::create();
    m_currentGroup->setContentSize({kCanvasWidth - 8.f, kCanvasHeight - 8.f});
    if (m_strokeLayer) m_strokeLayer->addChild(m_currentGroup, 1);
    clearRedoStack();
}

void PaiDrawCanvasNode::endStrokeGroup() {
    if (m_currentGroup) {
        m_groupStack.push_back(m_currentGroup);
        m_currentGroup = nullptr;
    }
}

void PaiDrawCanvasNode::undoLast() {
    if (m_groupStack.empty()) return;
    auto* group = m_groupStack.back();
    m_groupStack.pop_back();
    if (group) {
        group->retain();
        group->removeFromParentAndCleanup(false);
        m_redoStack.push_back(group);
        group->release();
    }
}

void PaiDrawCanvasNode::redoLast() {
    if (m_redoStack.empty()) return;
    auto* group = m_redoStack.back();
    m_redoStack.pop_back();
    if (group && m_strokeLayer) {
        m_strokeLayer->addChild(group, 1);
        m_groupStack.push_back(group);
    }
}

void PaiDrawCanvasNode::addStroke(StrokeSegment const& stroke) {
    if (!m_strokeLayer) return;
    auto* node = PaimonDrawNode::create();

    // Color con opacidad bakeada (CCDrawNode no soporta setOpacity sobre
    // primitivas ya dibujadas; metemos el alpha en el color de cada draw).
    cocos2d::ccColor4F color;
    if (stroke.eraser) {
        color = cocos2d::ccc4f(0.98f, 0.98f, 1.0f, 1.f);
    } else {
        color = cocos2d::ccc4f(stroke.color.r / 255.f,
                               stroke.color.g / 255.f,
                               stroke.color.b / 255.f,
                               std::clamp(m_currentOpacity, 0.05f, 1.f));
    }

    float x1 = stroke.x1 * (kCanvasWidth - 8.f);
    float y1 = stroke.y1 * (kCanvasHeight - 8.f);
    float x2 = stroke.x2 * (kCanvasWidth - 8.f);
    float y2 = stroke.y2 * (kCanvasHeight - 8.f);

    // Capsule única por segmento: grosor uniforme y sin acumulación de
    // alpha en el centro del trazo. Reemplaza el barrido de N círculos
    // que producía bandas más oscuras donde se solapaban consecutivos.
    node->drawCapsuleSegment({x1, y1}, {x2, y2}, stroke.size * 2.f, color, 28);

    if (m_currentGroup) {
        m_currentGroup->addChild(node, 2);
    } else {
        m_strokeLayer->addChild(node, 2);
    }
}

cocos2d::CCPoint PaiDrawCanvasNode::normalizeTouch(cocos2d::CCTouch* touch) {
    auto pos = this->convertTouchToNodeSpace(touch);
    float x = std::clamp((pos.x - 4.f) / (kCanvasWidth - 8.f), 0.f, 1.f);
    float y = std::clamp((pos.y - 4.f) / (kCanvasHeight - 8.f), 0.f, 1.f);
    return {x, y};
}

void PaiDrawCanvasNode::commitSegment(cocos2d::CCPoint const& nextPoint) {
    StrokeSegment stroke;
    stroke.x1 = m_lastPoint.x;
    stroke.y1 = m_lastPoint.y;
    stroke.x2 = nextPoint.x;
    stroke.y2 = nextPoint.y;
    stroke.color = m_currentColor;
    stroke.size = m_currentSize;
    stroke.eraser = (m_tool == Tool::Eraser);
    addStroke(stroke);
    PaiDrawManager::get().sendStroke(stroke);
    m_lastPoint = nextPoint;
}

void PaiDrawCanvasNode::rebuildPreview(cocos2d::CCPoint const& start, cocos2d::CCPoint const& current) {
    if (!m_previewNode) return;
    m_previewNode->clear();

    cocos2d::ccColor4F color = (m_tool == Tool::Eraser)
        ? cocos2d::ccc4f(0.98f, 0.98f, 1.0f, m_currentOpacity)
        : cocos2d::ccc4f(m_currentColor.r / 255.f,
                         m_currentColor.g / 255.f,
                         m_currentColor.b / 255.f,
                         m_currentOpacity);

    float x1 = start.x * (kCanvasWidth - 8.f);
    float y1 = start.y * (kCanvasHeight - 8.f);
    float x2 = current.x * (kCanvasWidth - 8.f);
    float y2 = current.y * (kCanvasHeight - 8.f);

    m_previewNode->drawCapsuleSegment({x1, y1}, {x2, y2}, m_currentSize * 2.f, color, 28);
}

bool PaiDrawCanvasNode::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_readOnly) return false;
    auto local = this->convertTouchToNodeSpace(touch);
    if (!CCRect(0.f, 0.f, kCanvasWidth, kCanvasHeight).containsPoint(local)) {
        return false;
    }
    m_drawing = true;
    m_lastPoint = normalizeTouch(touch);
    m_strokeStart = m_lastPoint;

    if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
        beginStrokeGroup();
        // Punto inicial (un click corto deja un punto visible)
        commitSegment(m_lastPoint);
    } else if (m_tool == Tool::Line) {
        rebuildPreview(m_strokeStart, m_lastPoint);
    }
    return true;
}

void PaiDrawCanvasNode::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_drawing || m_readOnly) return;
    auto next = normalizeTouch(touch);
    if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
        commitSegment(next);
    } else if (m_tool == Tool::Line) {
        rebuildPreview(m_strokeStart, next);
    }
}

void PaiDrawCanvasNode::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_drawing || m_readOnly) return;
    auto next = normalizeTouch(touch);

    if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
        commitSegment(next);
        endStrokeGroup();
    } else if (m_tool == Tool::Line) {
        beginStrokeGroup();
        m_lastPoint = m_strokeStart;
        commitSegment(next);
        endStrokeGroup();
        clearPreview();
    }
    m_drawing = false;
}

void PaiDrawCanvasNode::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (m_drawing) {
        if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
            endStrokeGroup();
        }
        clearPreview();
    }
    m_drawing = false;
    (void)touch; (void)event;
}

PaiDrawGameLayer* PaiDrawGameLayer::create() {
    auto* layer = new PaiDrawGameLayer();
    if (layer && layer->init()) {
        layer->autorelease();
        return layer;
    }
    CC_SAFE_DELETE(layer);
    return nullptr;
}

CCScene* PaiDrawGameLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(PaiDrawGameLayer::create());
    return scene;
}

bool PaiDrawGameLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    buildLayout();
    refreshState();

    WeakRef<PaiDrawGameLayer> weakSelf = this;
    m_roomSub = paimon::EventBus::get().subscribe<RoomUpdatedEvent>([weakSelf](RoomUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshState();
    });
    m_chatSub = paimon::EventBus::get().subscribe<ChatUpdatedEvent>([weakSelf](ChatUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->rebuildChat();
    });
    m_roundSub = paimon::EventBus::get().subscribe<RoundUpdatedEvent>([weakSelf](RoundUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshState();
    });
    m_strokeSub = paimon::EventBus::get().subscribe<StrokeEvent>([weakSelf](StrokeEvent const& ev) {
        auto self = weakSelf.lock();
        if (!self) return;
        if (self->m_canvas) self->m_canvas->addStroke(ev.stroke);
    });

    // Smooth countdown: tick the timer label locally every 100 ms instead
    // of waiting for server snapshots (which arrive at ~0.9 s in game).
    this->schedule(schedule_selector(PaiDrawGameLayer::tickLocalTimer), 0.1f);
    return true;
}

void PaiDrawGameLayer::keyBackClicked() { onBack(nullptr); }

void PaiDrawGameLayer::buildLayout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu, 10);

    // ── Header GD-canónico ──────────────────────────────────────────────
    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawGameLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

    // Título de ronda en oro GD
    m_header = makeLabel("Round 1 of 6", "goldFont.fnt", 0.55f,
        {win.width / 2.f, win.height - 16.f}, theme::kAccentGold);
    this->addChild(m_header, 5);

    m_wordLabel = makeLabel("_ _ _ _", "bigFont.fnt", 0.42f,
        {win.width / 2.f, win.height - 38.f}, theme::kTextOnDark);
    this->addChild(m_wordLabel, 5);

    // Timer encapsulado en un mini-panel GD (estilo timer del editor)
    if (auto* timerBg = paimon::SpriteHelper::safeCreateNineSlice("GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        timerBg->setContentSize({60.f, 26.f});
        timerBg->setOpacity(220);
        timerBg->setColor(theme::kPanelTint);
        timerBg->setAnchorPoint({0.5f, 0.5f});
        timerBg->setPosition({win.width - 38.f, win.height - 22.f});
        this->addChild(timerBg, 4);
    }

    m_timerLabel = makeLabel("90s", "goldFont.fnt", 0.55f,
        {win.width - 38.f, win.height - 22.f}, theme::kAccentGold);
    this->addChild(m_timerLabel, 5);

    auto* separator = CCLayerColor::create({255, 255, 255, 60});
    separator->setContentSize({win.width - 80.f, 1.f});
    separator->setPosition({40.f, win.height - 54.f});
    this->addChild(separator, 4);

    // ── Layout: rails laterales + área central de dibujo ────────────────
    constexpr float kLeftRailW = 70.f;
    constexpr float kRightRailW = 76.f;
    constexpr float kSidePad = 10.f;
    constexpr float kHeaderH = 60.f;
    constexpr float kBottomPad = 10.f;

    float canvasAreaX = kSidePad + kLeftRailW + kSidePad;
    float canvasAreaY = kBottomPad;
    float canvasAreaW = win.width - canvasAreaX - kRightRailW - kSidePad * 2.f;
    float canvasAreaH = win.height - kHeaderH - kBottomPad;

    // ── Frame del area de dibujo (panel GD blanco) ──────────────────────
    // Sin tinte: dejamos `GJ_square01.png` blanco puro como el popup del
    // editor de GD. Eso hace que el lienzo se sienta "embedded" en un
    // panel canónico, idéntico al style de `LevelInfoLayer`.
    bool placedCanvasFrame = false;
    if (auto* canvasFrame = paimon::SpriteHelper::safeCreateNineSlice("GJ_square01.png")) {
        canvasFrame->setContentSize({canvasAreaW, canvasAreaH});
        canvasFrame->setAnchorPoint({0.f, 0.f});
        canvasFrame->setPosition({canvasAreaX, canvasAreaY});
        canvasFrame->setOpacity(245);
        this->addChild(canvasFrame, -1);
        placedCanvasFrame = true;
    }
    if (!placedCanvasFrame) {
        if (auto* canvasFrame = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
            canvasFrame->setContentSize({canvasAreaW, canvasAreaH});
            canvasFrame->setAnchorPoint({0.f, 0.f});
            canvasFrame->setPosition({canvasAreaX, canvasAreaY});
            canvasFrame->setOpacity(245);
            this->addChild(canvasFrame, -1);
            placedCanvasFrame = true;
        }
    }
    if (!placedCanvasFrame) {
        auto* fallback = paimon::SpriteHelper::createRoundedRect(
            canvasAreaW, canvasAreaH, 8.f,
            {0.04f, 0.04f, 0.08f, 0.95f}, {0.40f, 0.40f, 0.40f, 0.7f}, 1.f);
        fallback->setPosition({canvasAreaX, canvasAreaY});
        this->addChild(fallback, -1);
    }

    cocos2d::CCPoint canvasCenter = {
        canvasAreaX + canvasAreaW / 2.f,
        canvasAreaY + canvasAreaH / 2.f
    };

    float fitW = canvasAreaW - 14.f;
    float fitH = canvasAreaH - 14.f;
    m_canvasBaseScale = std::min(fitW / kCanvasWidth, fitH / kCanvasHeight);
    m_canvasZoom = 1.f;
    // Cachear el rect del canvas para que applyCanvasZoom() pueda
    // recalcular el anchor y tolerar cambios de tamaño/desplazamientos
    // que provoquen otros mods (panels que reposicionan al frame).
    m_canvasFrameRect = cocos2d::CCRect{
        canvasAreaX, canvasAreaY, canvasAreaW, canvasAreaH
    };
    m_canvasAnchor = canvasCenter;

    m_canvas = PaiDrawCanvasNode::create();
    m_canvas->setAnchorPoint({0.5f, 0.5f});
    m_canvas->ignoreAnchorPointForPosition(false);
    m_canvas->setPosition(m_canvasAnchor);
    this->addChild(m_canvas, 2);
    applyCanvasZoom();

    // ── Left rail: paleta + pinceles (panel GD blanco puro) ─────────────
    bool placedLeftRail = false;
    if (auto* leftRail = paimon::SpriteHelper::safeCreateNineSlice("GJ_square02.png")) {
        leftRail->setContentSize({kLeftRailW, canvasAreaH});
        leftRail->setAnchorPoint({0.f, 0.f});
        leftRail->setPosition({kSidePad, canvasAreaY});
        leftRail->setOpacity(235);
        this->addChild(leftRail, 0);
        placedLeftRail = true;
    }
    if (!placedLeftRail) {
        if (auto* leftRail = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
            leftRail->setContentSize({kLeftRailW, canvasAreaH});
            leftRail->setAnchorPoint({0.f, 0.f});
            leftRail->setPosition({kSidePad, canvasAreaY});
            leftRail->setOpacity(235);
            this->addChild(leftRail, 0);
            placedLeftRail = true;
        }
    }
    if (!placedLeftRail) {
        auto* fallback = paimon::SpriteHelper::createRoundedRect(
            kLeftRailW, canvasAreaH, 6.f,
            {0.96f, 0.96f, 0.96f, 0.92f}, {0.30f, 0.30f, 0.30f, 0.7f}, 1.f);
        fallback->setPosition({kSidePad, canvasAreaY});
        this->addChild(fallback, 0);
    }

    // Header de la paleta (drag bar GD canónico)
    {
        auto* paletteHeader = makeSectionStrip("COLOR", kLeftRailW - 8.f);
        paletteHeader->setPosition({kSidePad + 4.f, canvasAreaY + canvasAreaH - 22.f});
        this->addChild(paletteHeader, 6);
    }

    // ── Paleta de 16 colores GD-canónica ─────────────────────────────────
    // Estos colores son los del color picker de GD (los que aparecen en
    // las "rainbow" rows del color editor), garantizando coherencia
    // visual con el resto del juego.
    std::vector<cocos2d::ccColor3B> colors = {
        {  0,   0,   0}, {255, 255, 255},   // negro / blanco
        {255,  35,  35}, {255, 100,   0},   // rojo GD / naranja GD
        {255, 165,   0}, {255, 235,   0},   // ámbar / amarillo GD
        {120, 230,   0}, {  0, 200,  60},   // verde lima GD / verde GD
        {  0, 200, 220}, {  0, 165, 255},   // cyan GD / azul GD
        { 50, 100, 255}, {120,  60, 220},   // azul profundo / índigo GD
        {175,   0, 255}, {255,   0, 175},   // violeta GD / pink GD
        {255, 105, 165}, {165, 100,  60}    // pastel pink / café GD
    };
    constexpr float kSwatch = 22.f;
    constexpr float kSwatchGap = 4.f;
    float colCenterX = kSidePad + kLeftRailW / 2.f;
    float palYStart = canvasAreaY + canvasAreaH - 50.f - kSwatch / 2.f;
    int idx = 0;
    for (auto const& color : colors) {
        // `GJ_colorBtn_001.png` es la cápsula que GD usa para los color
        // pickers (con glow blanco interno). Tintarla con el color base
        // produce un swatch idéntico al picker de niveles.
        cocos2d::CCNode* swatchNode = nullptr;
        if (auto* swatchSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png")) {
            float maxDim = std::max(swatchSpr->getContentSize().width,
                                    swatchSpr->getContentSize().height);
            if (maxDim > 0.f) swatchSpr->setScale(kSwatch / maxDim);
            swatchSpr->setColor(color);
            swatchNode = swatchSpr;
        } else if (auto* fb = paimon::SpriteHelper::safeCreateNineSlice("GJ_square01.png")) {
            fb->setContentSize({kSwatch, kSwatch});
            fb->setColor(color);
            swatchNode = fb;
        } else {
            auto solid = CCLayerColor::create(ccc4(color.r, color.g, color.b, 255));
            solid->setContentSize({kSwatch, kSwatch});
            swatchNode = solid;
        }
        auto* button = CCMenuItemSpriteExtra::create(
            swatchNode, this, menu_selector(PaiDrawGameLayer::onColor));
        button->setTag(idx);
        int row = idx / 2;
        int col = idx % 2;
        button->setPosition({
            colCenterX + (col == 0 ? -1.f : 1.f) * (kSwatch + kSwatchGap) / 2.f,
            palYStart - row * (kSwatch + kSwatchGap)
        });
        m_menu->addChild(button);
        m_colorButtons.push_back(button);
        idx++;
    }

    // 3 pinceles debajo de la paleta — punto negro crece con el size.
    // Sin tinte morado en el fondo: blanco puro GD.
    float brushY = palYStart - 8.f * (kSwatch + kSwatchGap) - 6.f;
    int brushIdx = 0;
    for (float dotR : {3.f, 5.5f, 9.f}) {
        auto* container = CCNode::create();
        container->setContentSize({24.f, 24.f});
        container->setAnchorPoint({0.5f, 0.5f});

        // Mini-fondo `GJ_square05.png` blanco GD canónico
        bool placedBrushBg = false;
        if (auto* bg = paimon::SpriteHelper::safeCreateNineSlice(
                "GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
            bg->setContentSize({22.f, 22.f});
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->setPosition({12.f, 12.f});
            bg->setOpacity(220);
            container->addChild(bg, 0);
            placedBrushBg = true;
        }
        if (!placedBrushBg) {
            if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
                bg->setContentSize({22.f, 22.f});
                bg->setAnchorPoint({0.5f, 0.5f});
                bg->setPosition({12.f, 12.f});
                bg->setOpacity(220);
                container->addChild(bg, 0);
            }
        }

        // Punto del tamaño del pincel — negro para que se vea sobre el
        // fondo blanco GD (antes era blanco sobre morado).
        auto* dot = PaimonDrawNode::create();
        dot->drawSolidCircle({12.f, 12.f}, dotR, cocos2d::ccc4f(0.f, 0.f, 0.f, 1.f), 48);
        container->addChild(dot, 1);

        auto* button = CCMenuItemSpriteExtra::create(
            container, this, menu_selector(PaiDrawGameLayer::onBrush));
        button->setTag(brushIdx);
        button->setPosition({colCenterX + (brushIdx - 1) * 22.f, brushY});
        m_menu->addChild(button);
        m_brushButtons.push_back(button);
        brushIdx++;
    }

    // ── Right rail: herramientas + zoom (panel GD blanco) ──────────────
    float rightRailX = win.width - kSidePad - kRightRailW;
    bool placedRightRail = false;
    if (auto* rightRail = paimon::SpriteHelper::safeCreateNineSlice("GJ_square02.png")) {
        rightRail->setContentSize({kRightRailW, canvasAreaH});
        rightRail->setAnchorPoint({0.f, 0.f});
        rightRail->setPosition({rightRailX, canvasAreaY});
        rightRail->setOpacity(235);
        this->addChild(rightRail, 0);
        placedRightRail = true;
    }
    if (!placedRightRail) {
        if (auto* rightRail = paimon::SpriteHelper::safeCreateScale9("GJ_square02.png")) {
            rightRail->setContentSize({kRightRailW, canvasAreaH});
            rightRail->setAnchorPoint({0.f, 0.f});
            rightRail->setPosition({rightRailX, canvasAreaY});
            rightRail->setOpacity(235);
            this->addChild(rightRail, 0);
            placedRightRail = true;
        }
    }
    if (!placedRightRail) {
        auto* fallback = paimon::SpriteHelper::createRoundedRect(
            kRightRailW, canvasAreaH, 6.f,
            {0.96f, 0.96f, 0.96f, 0.92f}, {0.30f, 0.30f, 0.30f, 0.7f}, 1.f);
        fallback->setPosition({rightRailX, canvasAreaY});
        this->addChild(fallback, 0);
    }

    {
        auto* toolHeader = makeSectionStrip("TOOLS", kRightRailW - 8.f);
        toolHeader->setPosition({rightRailX + 4.f, canvasAreaY + canvasAreaH - 22.f});
        this->addChild(toolHeader, 6);
    }

    float rightCenterX = rightRailX + kRightRailW / 2.f;
    float toolY = canvasAreaY + canvasAreaH - 56.f;

    // ── Botones de herramientas: cápsulas circulares con icono GD ───────
    // Eraser: `edit_eraserBtn_001.png` (la goma del editor de GD).
    // Cae a `GJ_paintBtn_001.png` si no existe el frame.
    // Clear: `GJ_trashBtn_001.png` (papelera del editor).
    // Sin tintes morados: el panel GD-canónico va sin setColor.
    auto addIconButton = [&](char const* iconFrame, char const* fallbackFrame,
        cocos2d::SEL_MenuHandler cb, int tag, float y) -> CCMenuItemSpriteExtra*
    {
        // Try iconFrame first, fall back to a secondary frame, then
        // fall back to a text button if neither exists.
        char const* chosenFrame = nullptr;
        if (paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
            chosenFrame = iconFrame;
        } else if (fallbackFrame && paimon::SpriteHelper::safeCreateWithFrameName(fallbackFrame)) {
            chosenFrame = fallbackFrame;
        }

        if (!chosenFrame) {
            auto* btnSpr = ButtonSprite::create("?", "bigFont.fnt", "GJ_button_04.png", 0.7f);
            btnSpr->setScale(0.55f);
            auto* btn = CCMenuItemSpriteExtra::create(btnSpr, this, cb);
            btn->setTag(tag);
            btn->setPosition({rightCenterX, y});
            m_menu->addChild(btn);
            return btn;
        }

        // Sin tinte (color blanco puro GD); el panel circular envuelve
        // al icono respetando los colores nativos.
        auto* node = makeIconCircleNode(chosenFrame, theme::kPanelTint, 32.f, 0.78f);
        if (!node) {
            // Defensive fallback (shouldn't happen because we already
            // verified chosenFrame existed).
            auto* btnSpr = ButtonSprite::create("?", "bigFont.fnt", "GJ_button_04.png", 0.7f);
            btnSpr->setScale(0.55f);
            auto* btn = CCMenuItemSpriteExtra::create(btnSpr, this, cb);
            btn->setTag(tag);
            btn->setPosition({rightCenterX, y});
            m_menu->addChild(btn);
            return btn;
        }
        auto* btn = CCMenuItemSpriteExtra::create(node, this, cb);
        btn->setTag(tag);
        btn->setPosition({rightCenterX, y});
        m_menu->addChild(btn);
        return btn;
    };

    addIconButton("edit_eraserBtn_001.png", "GJ_paintBtn_001.png",
        menu_selector(PaiDrawGameLayer::onEraser), 0, toolY);
    addIconButton("GJ_trashBtn_001.png", "GJ_deleteBtn_001.png",
        menu_selector(PaiDrawGameLayer::onClearCanvas), 0, toolY - 38.f);

    // ── Zoom (parte inferior del rail) ──────────────────────────────────
    {
        auto* zoomHeader = makeSectionStrip("ZOOM", kRightRailW - 8.f);
        zoomHeader->setPosition({rightRailX + 4.f, canvasAreaY + 92.f});
        this->addChild(zoomHeader, 6);
    }

    auto* minusBtn = addIconButton("GJ_minusBtn_001.png", nullptr,
        menu_selector(PaiDrawGameLayer::onZoom), -1, canvasAreaY + 60.f);
    minusBtn->setPositionX(rightCenterX - 18.f);

    auto* plusBtn = addIconButton("GJ_plusBtn_001.png", nullptr,
        menu_selector(PaiDrawGameLayer::onZoom), 1, canvasAreaY + 60.f);
    plusBtn->setPositionX(rightCenterX + 18.f);

    m_zoomLabel = makeLabel("100%", "goldFont.fnt", 0.50f,
        {rightCenterX, canvasAreaY + 32.f}, theme::kAccentGold);
    this->addChild(m_zoomLabel, 5);

    addIconButton("GJ_replayBtn_001.png", nullptr,
        menu_selector(PaiDrawGameLayer::onZoom), 0, canvasAreaY + 12.f);
}

void PaiDrawGameLayer::applyCanvasZoom() {
    if (!m_canvas) return;
    // Recalcular el anchor desde el rect del canvas cacheado en
    // buildLayout(). Si el rect estaba uninitialized (todo 0) caemos
    // de vuelta al valor original de m_canvasAnchor para no romper nada.
    if (m_canvasFrameRect.size.width > 0.f && m_canvasFrameRect.size.height > 0.f) {
        m_canvasAnchor = ccp(
            m_canvasFrameRect.getMidX(),
            m_canvasFrameRect.getMidY()
        );
    }
    m_canvas->setScale(m_canvasBaseScale * m_canvasZoom);
    m_canvas->setPosition(m_canvasAnchor);
    if (m_zoomLabel) {
        m_zoomLabel->setString(fmt::format("{}%",
            static_cast<int>(m_canvasZoom * 100.f + 0.5f)).c_str());
    }
}

void PaiDrawGameLayer::onZoom(CCObject* sender) {
    int tag = sender->getTag();
    if (tag == 0) m_canvasZoom = 1.f;
    else if (tag > 0) m_canvasZoom = std::min(m_canvasZoom * 1.25f, 4.f);
    else m_canvasZoom = std::max(m_canvasZoom / 1.25f, 0.4f);
    applyCanvasZoom();
}

void PaiDrawGameLayer::refreshHeader() {
    auto state = PaiDrawManager::get().snapshot();
    if (m_header) {
        std::string drawer = state.currentRound.drawingPlayerName.empty()
            ? std::string("Your turn (test)")
            : state.currentRound.drawingPlayerName;
        m_header->setString(fmt::format("Round {}/{} - Drawing: {}",
            std::max(state.currentRound.currentRound, 1),
            std::max(state.currentRound.totalRounds, 1),
            drawer).c_str());
    }
    if (m_wordLabel) {
        std::string word;
        if (state.currentRound.localPlayerIsDrawer && !state.currentRound.drawerWord.empty()) {
            word = fmt::format("{} [{}]", state.currentRound.drawerWord,
                difficultyLabel(PaiDrawManager::get().currentWord().difficulty));
        } else if (!state.currentRound.maskedWord.empty()) {
            word = state.currentRound.maskedWord;
        } else {
            word = "FREE DRAW";
        }
        m_wordLabel->setString(word.c_str());
    }
    if (m_timerLabel) {
        // Prefer the absolute local deadline when available so the label
        // matches what tickLocalTimer is going to render in the next
        // frames, keeping the value smooth across snapshots.
        int seconds = state.currentRound.timeLeftSeconds;
        if (state.currentRound.endsAtLocalMs > 0) {
            uint64_t now = nowMs();
            seconds = state.currentRound.endsAtLocalMs > now
                ? static_cast<int>((state.currentRound.endsAtLocalMs - now) / 1000ULL)
                : 0;
        }
        m_timerLabel->setString(fmt::format("{}s", std::max(seconds, 0)).c_str());
    }
    // Modo test: siempre permitir dibujar
    if (m_canvas) {
        m_canvas->setReadOnly(false);
    }
}

void PaiDrawGameLayer::tickLocalTimer(float) {
    if (!m_timerLabel) return;
    auto state = PaiDrawManager::get().snapshot();
    if (state.currentRound.endsAtLocalMs == 0) return;

    uint64_t now = nowMs();
    int seconds = state.currentRound.endsAtLocalMs > now
        ? static_cast<int>((state.currentRound.endsAtLocalMs - now) / 1000ULL)
        : 0;
    m_timerLabel->setString(fmt::format("{}s", seconds).c_str());
}

CCNode* PaiDrawGameLayer::createScoreRow(PlayerInfo const&, float, float) {
    return CCNode::create();
}

void PaiDrawGameLayer::rebuildScoreboard() {
    // Score eliminado para dejar mas espacio al canvas.
}

void PaiDrawGameLayer::rebuildChat() {
    // Chat eliminado para dejar mas espacio al canvas.
}

void PaiDrawGameLayer::refreshState() {
    refreshHeader();
    if (m_canvas) {
        m_canvas->clearCanvas();
        for (auto const& stroke : PaiDrawManager::get().snapshot().recentStrokes) {
            m_canvas->addStroke(stroke);
        }
    }
}

void PaiDrawGameLayer::onBack(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_roomSub);
    paimon::EventBus::get().unsubscribe(m_chatSub);
    paimon::EventBus::get().unsubscribe(m_roundSub);
    paimon::EventBus::get().unsubscribe(m_strokeSub);
    CCDirector::sharedDirector()->popScene();
}

void PaiDrawGameLayer::onSendGuess(CCObject*) {
    // El input de respuestas se removio para dar mas espacio al canvas.
}

void PaiDrawGameLayer::onColor(CCObject* sender) {
    // Misma paleta que en buildLayout — paleta GD canónica del color
    // picker de niveles.
    static std::vector<cocos2d::ccColor3B> colors = {
        {  0,   0,   0}, {255, 255, 255},
        {255,  35,  35}, {255, 100,   0},
        {255, 165,   0}, {255, 235,   0},
        {120, 230,   0}, {  0, 200,  60},
        {  0, 200, 220}, {  0, 165, 255},
        { 50, 100, 255}, {120,  60, 220},
        {175,   0, 255}, {255,   0, 175},
        {255, 105, 165}, {165, 100,  60}
    };
    int index = sender->getTag();
    if (index >= 0 && index < static_cast<int>(colors.size()) && m_canvas) {
        m_canvas->setDrawColor(colors[static_cast<size_t>(index)]);
        m_canvas->setEraser(false);
    }
}

void PaiDrawGameLayer::onBrush(CCObject* sender) {
    int idx = sender->getTag();
    float size = idx == 0 ? 4.f : idx == 1 ? 8.f : 13.f;
    if (m_canvas) m_canvas->setBrushSize(size);
}

void PaiDrawGameLayer::onEraser(CCObject*) {
    if (m_canvas) m_canvas->setEraser(true);
}

void PaiDrawGameLayer::onClearCanvas(CCObject*) {
    if (m_canvas) m_canvas->clearCanvas();
    PaiDrawManager::get().clearCanvas();
}

PaiDrawResultsLayer* PaiDrawResultsLayer::create() {
    auto* layer = new PaiDrawResultsLayer();
    if (layer && layer->init()) {
        layer->autorelease();
        return layer;
    }
    CC_SAFE_DELETE(layer);
    return nullptr;
}

CCScene* PaiDrawResultsLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(PaiDrawResultsLayer::create());
    return scene;
}

bool PaiDrawResultsLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    buildLayout();
    refreshResults();
    WeakRef<PaiDrawResultsLayer> weakSelf = this;
    m_resultsSub = paimon::EventBus::get().subscribe<ResultsUpdatedEvent>([weakSelf](ResultsUpdatedEvent const&) {
        auto self = weakSelf.lock();
        if (!self) return;
        self->refreshResults();
    });
    return true;
}

void PaiDrawResultsLayer::keyBackClicked() { onBackLobby(nullptr); }

void PaiDrawResultsLayer::buildLayout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    addNativeBackground(this);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    // ── Header GD canónico ──────────────────────────────────────────────
    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawResultsLayer::onBackLobby));
    backButton->setPosition({26.f, win.height - 22.f});
    menu->addChild(backButton);

    if (auto* logoIcon = createPaiDrawIcon(34.f)) {
        logoIcon->setPosition({win.width / 2.f - 130.f, win.height - 22.f});
        this->addChild(logoIcon, 6);
    }

    auto* title = makeLabel("FINAL RESULTS", "goldFont.fnt", 0.92f,
        {win.width / 2.f, win.height - 22.f}, theme::kAccentGold);
    this->addChild(title, 6);

    auto* sub = makeLabel("Match summary and leaderboard", "goldFont.fnt", 0.46f,
        {win.width / 2.f, win.height - 44.f}, theme::kAccentLightGold);
    this->addChild(sub, 6);

    auto* topSep = CCLayerColor::create({255, 255, 255, 60});
    topSep->setContentSize({win.width - 80.f, 1.f});
    topSep->setPosition({40.f, win.height - 56.f});
    this->addChild(topSep, 4);

    // ── Layout: podium arriba + tabla abajo ─────────────────────────────
    constexpr float kSidePad = 20.f;
    constexpr float kPodiumH = 130.f;
    constexpr float kBottomBar = 56.f;
    constexpr float kHeaderArea = 64.f;
    constexpr float kStatsBar = 32.f;

    float podiumW = std::min(win.width - kSidePad * 2.f, 720.f);
    float podiumX = (win.width - podiumW) / 2.f;
    float podiumY = win.height - kHeaderArea - kPodiumH - 8.f;

    // ── Podium panel (GD blanco puro) ───────────────────────────────────
    auto* podium = makeFramedPanel(podiumW, kPodiumH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    podium->setPosition({podiumX, podiumY});
    this->addChild(podium, 0);

    auto* podiumStrip = makeSectionStrip("PODIUM", podiumW - 20.f);
    podiumStrip->setPosition({podiumX + 10.f, podiumY + kPodiumH - 30.f});
    this->addChild(podiumStrip, 5);

    // ── Stats bar GD-canónica entre podium y tabla ──────────────────────
    float statsY = podiumY - kStatsBar / 2.f - 4.f;
    if (auto* statsBg = paimon::SpriteHelper::safeCreateNineSlice("GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        statsBg->setContentSize({podiumW, kStatsBar});
        statsBg->setOpacity(190);
        statsBg->setAnchorPoint({0.5f, 0.5f});
        statsBg->setPosition({win.width / 2.f, statsY});
        this->addChild(statsBg, 1);
    }
    m_statsLabel = makeLabel("-", "chatFont.fnt", 0.65f,
        {win.width / 2.f, statsY}, theme::kTextSubtle);
    this->addChild(m_statsLabel, 5);

    // ── Tabla / leaderboard ──────────────────────────────────────────────
    float tableTop = statsY - kStatsBar / 2.f - 6.f;
    float tableBottom = kBottomBar + 6.f;
    float tableH = tableTop - tableBottom;
    float tableW = podiumW;
    float tableX = podiumX;

    auto* tablePanel = makeFramedPanel(tableW, tableH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    tablePanel->setPosition({tableX, tableBottom});
    this->addChild(tablePanel, 0);

    auto* tableStrip = makeSectionStrip("LEADERBOARD", tableW - 20.f);
    tableStrip->setPosition({tableX + 10.f, tableBottom + tableH - 30.f});
    this->addChild(tableStrip, 5);

    constexpr float kScrollMargin = 12.f;
    float scrollH = tableH - 40.f - kScrollMargin;
    m_tableScroll = geode::ScrollLayer::create({tableW - kScrollMargin * 2.f, scrollH});
    m_tableScroll->setPosition({tableX + kScrollMargin, tableBottom + kScrollMargin});
    this->addChild(m_tableScroll, 3);

    // ── Botones GD-canónicos: Play Again (verde) + Back to Lobby (gris) ─
    float btnY = kBottomBar / 2.f - 4.f;
    auto* againSprite = ButtonSprite::create("Play Again", "bigFont.fnt", "GJ_button_01.png", 0.9f);
    againSprite->setScale(0.78f);
    auto* againBtn = CCMenuItemSpriteExtra::create(
        againSprite, this, menu_selector(PaiDrawResultsLayer::onPlayAgain));
    againBtn->setPosition({win.width / 2.f - 100.f, btnY});
    menu->addChild(againBtn);

    auto* lobbySprite = ButtonSprite::create("Back to Lobby", "bigFont.fnt", "GJ_button_04.png", 0.9f);
    lobbySprite->setScale(0.78f);
    auto* lobbyBtn = CCMenuItemSpriteExtra::create(
        lobbySprite, this, menu_selector(PaiDrawResultsLayer::onBackLobby));
    lobbyBtn->setPosition({win.width / 2.f + 100.f, btnY});
    menu->addChild(lobbyBtn);
}

void PaiDrawResultsLayer::rebuildTable() {
    auto results = PaiDrawManager::get().snapshot().results;
    std::vector<CCNode*> rows;
    float width = m_tableScroll->getContentSize().width;
    for (size_t index = 0; index < results.leaderboard.size(); ++index) {
        auto const& player = results.leaderboard[index];
        auto* row = CCNode::create();
        row->setContentSize({width, 44.f});
        row->setAnchorPoint({0.5f, 0.5f});

        // ── Comment cell GD canónico, top 3 con tinte sutil dorado ─────
        bool placedRowBg = false;
        cocos2d::ccColor3B rowTint = (index < 3) ? theme::kAccentLightGold : theme::kPanelTint;
        if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice(
                "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
            cell->setContentSize({width, 44.f});
            cell->setAnchorPoint({0.f, 0.f});
            cell->setColor(rowTint);
            cell->setOpacity(index < 3 ? 240 : 220);
            row->addChild(cell, 0);
            placedRowBg = true;
        }
        if (!placedRowBg) {
            if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice("GJ_square03.png")) {
                cell->setContentSize({width, 44.f});
                cell->setAnchorPoint({0.f, 0.f});
                cell->setColor(rowTint);
                cell->setOpacity(180);
                row->addChild(cell, 0);
            }
        }

        // Rank con icono especial para top 3 (medallas no existen como
        // sprite frame en GD, así que usamos `GJ_starsIcon_001.png` o
        // `bigStar_001.png` para top 1, marcado con goldFont).
        std::string rankLabel = fmt::format("#{}", index + 1);
        cocos2d::ccColor3B rankColor = theme::kAccentGold;
        if (index == 0) rankColor = theme::kAccentGold;          // oro
        else if (index == 1) rankColor = {220, 220, 220};        // plata
        else if (index == 2) rankColor = {200, 130, 80};         // bronce
        row->addChild(makeLabel(rankLabel, "goldFont.fnt", 0.55f,
            {28.f, 22.f}, rankColor), 2);

        if (auto* icon = makePlayerIcon(player, 26.f)) {
            icon->setPosition({64.f, 22.f});
            row->addChild(icon, 2);
        }
        row->addChild(makeLabel(player.name, "bigFont.fnt", 0.50f,
            {84.f, 22.f}, theme::kTextOnDark, {0.f, 0.5f}), 2);
        row->addChild(makeLabel(fmt::format("{} pts", player.score), "goldFont.fnt", 0.50f,
            {width - 18.f, 22.f}, theme::kAccentGold, {1.f, 0.5f}), 2);
        rows.push_back(row);
    }
    fillScroll(m_tableScroll, rows, 44.f, 6.f);
}

void PaiDrawResultsLayer::refreshResults() {
    auto results = PaiDrawManager::get().snapshot().results;
    if (m_statsLabel) {
        m_statsLabel->setString(fmt::format("Best Drawer: {}    Fastest Guesser: {}    Hardest Word: {}",
            results.bestDrawer, results.fastestGuesser, results.hardestWord).c_str());
    }
    rebuildTable();
}

void PaiDrawResultsLayer::onBackLobby(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_resultsSub);
    CCDirector::sharedDirector()->replaceScene(PaiDrawLobbyLayer::scene());
}

void PaiDrawResultsLayer::onPlayAgain(CCObject*) {
    CCDirector::sharedDirector()->replaceScene(PaiDrawRoomLayer::scene());
}

} // namespace paidraw
