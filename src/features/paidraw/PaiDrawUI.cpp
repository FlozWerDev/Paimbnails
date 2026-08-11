#include "PaiDrawUI.hpp"

#include "PaiDrawIcon.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../utils/DynamicPopupRegistry.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/MenuGameLayer.hpp>
#include <algorithm>
#include <array>

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

namespace theme {
    constexpr cocos2d::ccColor3B kPanelTint        {255, 255, 255};
    constexpr cocos2d::ccColor3B kPanelInnerTint   {255, 255, 255};
    constexpr cocos2d::ccColor3B kDragBarTint      {255, 255, 255};

    constexpr cocos2d::ccColor3B kAccentGold       {255, 217, 119};
    constexpr cocos2d::ccColor3B kAccentLightGold  {255, 240, 170};
    constexpr cocos2d::ccColor3B kAccentGreen      {102, 255, 102};
    constexpr cocos2d::ccColor3B kAccentRed        {255,  71,  71};
    constexpr cocos2d::ccColor3B kAccentAqua       {125, 200, 255};
    constexpr cocos2d::ccColor3B kAccentOrange     {255, 175,  90};

    constexpr cocos2d::ccColor3B kTextOnDark       {255, 255, 255};
    constexpr cocos2d::ccColor3B kTextSubtle       {200, 210, 230};
    constexpr cocos2d::ccColor3B kTextMuted        {150, 160, 180};

    constexpr cocos2d::ccColor3B kChipDarkFill     { 35,  40,  60};
    constexpr cocos2d::ccColor3B kChipDangerFill   { 80,  20,  20};
    constexpr cocos2d::ccColor3B kChipOkFill       { 20,  60,  20};
    constexpr cocos2d::ccColor3B kChipWarnFill     { 70,  50,  10};
    constexpr cocos2d::ccColor3B kChipGoldFill     { 60,  45,  15};
}

constexpr std::array<cocos2d::ccColor3B, 16> kDrawPalette = {{
    {0, 0, 0}, {255, 255, 255}, {255, 35, 35}, {255, 100, 0},
    {255, 165, 0}, {255, 235, 0}, {120, 230, 0}, {0, 200, 60},
    {0, 200, 220}, {0, 165, 255}, {50, 100, 255}, {120, 60, 220},
    {175, 0, 255}, {255, 0, 175}, {255, 105, 165}, {165, 100, 60},
}};

void addNativeBackground(CCLayer* layer, cocos2d::ccColor4B /*fallbackColor*/ = ccc4(0, 0, 0, 255)) {
    if (auto* bg = MenuGameLayer::create()) {
        layer->addChild(bg, -10);
        return;
    }
    auto win = CCDirector::get()->getWinSize();
    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        auto bgSize = bg->getTextureRect().size;
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX((win.width  + 10.f) / std::max(bgSize.width,  1.f));
        bg->setScaleY((win.height + 10.f) / std::max(bgSize.height, 1.f));
        bg->setPosition({-5.f, -5.f});
        layer->addChild(bg, -10);
        return;
    }
    auto solid = CCLayerColor::create(ccc4(0, 0, 0, 255));
    solid->setContentSize(win);
    layer->addChild(solid, -10);
}

CCNode* makeFramedPanel(float width, float height,
    cocos2d::ccColor3B fillColor = {0, 0, 0},
    cocos2d::ccColor3B borderColor = {0, 0, 0},
    GLubyte opacity = 150) {
    auto* node = CCNode::create();
    node->setContentSize({width, height});
    node->setAnchorPoint({0.f, 0.f});

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

CCNode* makeSectionStrip(std::string const& title, float width,
    cocos2d::ccColor3B stripTint = theme::kDragBarTint,
    cocos2d::ccColor3B titleColor = theme::kAccentGold,
    float fontScale = 0.62f) {
    auto* container = CCNode::create();

    float stripH = 26.f;
    bool placed = false;
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
        row->setAnchorPoint({0.5f, 0.5f});
        row->setPosition({width / 2.f, y});
        layer->addChild(row);
        y -= rowHeight + padding;
    }
    layer->setPositionY(0.f);
}

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

}

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
    if (!paimon::modules::isEnabled("paimbnails.paidraw.menu")) return nullptr;

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
    auto win = CCDirector::get()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu, 10);

    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawLobbyLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

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

    auto* topSep = CCLayerColor::create({255, 255, 255, 60});
    topSep->setContentSize({win.width - 80.f, 1.f});
    topSep->setPosition({40.f, win.height - 56.f});
    this->addChild(topSep, 4);

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

    auto* dragHeader = makeSectionStrip("ONLINE PLAYERS", panelW - 20.f);
    dragHeader->setPosition({panelX + 10.f, panelTop - 30.f});
    this->addChild(dragHeader, 5);

    m_titleLabel = makeLabel("0 PLAYERS", "goldFont.fnt", 0.40f,
        {panelX + panelW - 26.f, panelTop - 17.f}, theme::kAccentLightGold,
        {1.f, 0.5f});
    this->addChild(m_titleLabel, 6);

    m_statusLabel = makeLabel("Conectando...", "chatFont.fnt", 0.55f,
        {panelX + panelW / 2.f, panelTop - 50.f}, theme::kTextSubtle);
    this->addChild(m_statusLabel, 5);

    constexpr float kScrollMargin = 12.f;
    float scrollTop = panelTop - kHeaderInside - 20.f;
    float scrollBottom = panelY + kScrollMargin;

    m_onlineScroll = geode::ScrollLayer::create({
        panelW - kScrollMargin * 2.f,
        scrollTop - scrollBottom
    });
    m_onlineScroll->setPosition({panelX + kScrollMargin, scrollBottom});
    this->addChild(m_onlineScroll, 3);

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

    if (auto* refreshSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png")) {
        refreshSprite->setScale(0.55f);
        auto* refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSprite, this, menu_selector(PaiDrawLobbyLayer::onRefresh));
        refreshBtn->setPosition({win.width - 26.f, win.height - 22.f});
        m_menu->addChild(refreshBtn);
    }
}

void PaiDrawLobbyLayer::onCreateRoom(CCObject*) {
    CCDirector::get()->pushScene(PaiDrawCreateRoomLayer::scene());
}

void PaiDrawLobbyLayer::onJoinRoom(CCObject*) {
    CCDirector::get()->pushScene(PaiDrawRoomsLayer::scene());
}

void PaiDrawLobbyLayer::onRefresh(CCObject*) {
    PaiDrawManager::get().refreshLobby();
    refreshLists();
}

void PaiDrawLobbyLayer::onBack(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_connectionSub);
    paimon::EventBus::get().unsubscribe(m_lobbySub);
    CCDirector::get()->popScene();
}

void PaiDrawLobbyLayer::onExit() {
    paimon::EventBus::get().unsubscribe(m_connectionSub);
    paimon::EventBus::get().unsubscribe(m_lobbySub);
    CCLayer::onExit();
}

void PaiDrawLobbyLayer::updateHeader() {
    auto state = PaiDrawManager::get().snapshot();
    if (m_titleLabel) {
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

    if (auto* icon = makePlayerIcon(player, 32.f)) {
        icon->setPosition({28.f, height / 2.f});
        row->addChild(icon, 2);
    }

    float textX = 56.f;
    auto* name = makeLabel(player.name, "bigFont.fnt", 0.50f,
        {textX, height / 2.f + 8.f}, theme::kTextOnDark, {0.f, 0.5f});
    name->limitLabelWidth(width - textX - 110.f, 0.50f, 0.28f);
    row->addChild(name, 3);

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
    auto win = CCDirector::get()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 10);

    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawRoomsLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

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

    auto* dragHeader = makeSectionStrip("ROOM LIST", panelW - 20.f);
    dragHeader->setPosition({panelX + 10.f, panelTop - 30.f});
    this->addChild(dragHeader, 5);

    m_titleLabel = makeLabel("0 ROOMS", "goldFont.fnt", 0.40f,
        {panelX + panelW - 26.f, panelTop - 17.f}, theme::kAccentLightGold,
        {1.f, 0.5f});
    this->addChild(m_titleLabel, 6);

    if (auto* refreshSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png")) {
        refreshSprite->setScale(0.55f);
        auto* refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSprite, this, menu_selector(PaiDrawRoomsLayer::onRefresh));
        refreshBtn->setPosition({win.width - 26.f, win.height - 22.f});
        m_menu->addChild(refreshBtn);
    }

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

    m_emptyLabel = makeLabel("No active rooms. Create one!",
        "bigFont.fnt", 0.55f,
        {panelX + panelW / 2.f, panelY + panelH / 2.f},
        theme::kTextSubtle);
    m_emptyLabel->setVisible(false);
    this->addChild(m_emptyLabel, 4);

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
    CCDirector::get()->popScene();
}

void PaiDrawRoomsLayer::onExit() {
    paimon::EventBus::get().unsubscribe(m_connectionSub);
    paimon::EventBus::get().unsubscribe(m_lobbySub);
    CCLayer::onExit();
}

void PaiDrawRoomsLayer::onRefresh(CCObject*) {
    PaiDrawManager::get().refreshLobby();
    refreshLists();
}

void PaiDrawRoomsLayer::onCreateRoom(CCObject*) {
    CCDirector::get()->pushScene(PaiDrawCreateRoomLayer::scene());
}

void PaiDrawRoomsLayer::onJoinRoom(CCObject* sender) {
    auto roomId = static_cast<uint32_t>(sender->getTag());
    PaiDrawManager::get().joinRoom(roomId);
    CCDirector::get()->pushScene(PaiDrawRoomLayer::scene());
}

CCNode* PaiDrawRoomsLayer::createRoomRow(RoomInfo const& room, float width, float height) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    bool inGame = room.state == RoomState::InGame;
    bool full = room.playerCount() >= room.config.maxPlayers;

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

    char const* joinText = inGame ? "Playing" : full ? "Full" : room.hasPassword ? "Locked" : "Join";
    char const* joinBg = (inGame || full || room.hasPassword) ? "GJ_button_06.png" : "GJ_button_01.png";
    auto* joinSprite = ButtonSprite::create(
        joinText, "bigFont.fnt", joinBg, 0.7f);
    joinSprite->setScale(0.62f);
    auto* joinBtn = CCMenuItemSpriteExtra::create(
        joinSprite, this, menu_selector(PaiDrawRoomsLayer::onJoinRoom));
    joinBtn->setPosition({width - kJoinW / 2.f - 8.f, height / 2.f});
    joinBtn->setTag(static_cast<int>(room.id));
    joinBtn->setEnabled(!inGame && !full);
    menu->addChild(joinBtn);

    float textX = 14.f;

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

    auto* stateChip = makeChip(roomStateLabel(room.state),
        inGame ? theme::kAccentRed : theme::kAccentGreen,
        inGame ? theme::kChipDangerFill : theme::kChipOkFill,
        7.f, 0.34f);
    float chipX = textX + nameLabel->getScaledContentSize().width + 10.f
                  + stateChip->getContentSize().width / 2.f;
    chipX = std::min(chipX, textRight - stateChip->getContentSize().width / 2.f);
    stateChip->setPosition({chipX, height - 16.f});
    row->addChild(stateChip, 3);

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
    auto win = CCDirector::get()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 10);

    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawCreateRoomLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

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

    auto* dragHeader = makeSectionStrip("ROOM SETTINGS", panelW - 20.f);
    dragHeader->setPosition({panelX + 10.f, panelTop - 30.f});
    this->addChild(dragHeader, 5);

    constexpr float kPad = 16.f;
    float innerW = panelW - kPad * 2.f;
    constexpr float kColGap = 18.f;
    float colW = (innerW - kColGap) / 2.f;
    float leftColX = panelX + kPad;
    float rightColX = leftColX + colW + kColGap;
    float innerTop = panelY + panelH - kPad - 28.f;

    auto sectionLabel = [&](char const* text, float x, float y, cocos2d::ccColor3B color) {
        auto* lbl = makeLabel(text, "goldFont.fnt", 0.42f,
            {x, y}, color, {0.f, 0.5f});
        this->addChild(lbl, 5);
        return lbl;
    };

    constexpr float kLeftRow = 64.f;
    float yL = innerTop - 12.f;

    sectionLabel("ROOM NAME", leftColX, yL, theme::kAccentGold);
    m_nameInput = geode::TextInput::create(colW, "Room name");
    m_nameInput->setPosition({leftColX + colW / 2.f, yL - 24.f});
    m_nameInput->setMaxCharCount(32);
    m_nameInput->setCommonFilter(geode::CommonFilter::Name);
    this->addChild(m_nameInput, 5);

    yL -= kLeftRow;
    sectionLabel("PASSWORD", leftColX, yL, theme::kAccentGold);
    m_passwordInput = geode::TextInput::create(colW,
        m_editMode ? "Set on create" : "Optional");
    m_passwordInput->setPosition({leftColX + colW / 2.f, yL - 24.f});
    m_passwordInput->setMaxCharCount(24);
    m_passwordInput->setPasswordMode(true);
    this->addChild(m_passwordInput, 5);

    yL -= kLeftRow;
    sectionLabel("MAX PLAYERS", leftColX, yL, theme::kAccentAqua);

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

    m_playerSlider = Slider::create(this,
        menu_selector(PaiDrawCreateRoomLayer::onPlayersChanged), 0.55f);
    m_playerSlider->setPosition({leftColX + colW / 2.f, yL - 26.f});
    this->addChild(m_playerSlider, 5);

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
    CCDirector::get()->popScene();
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
    config.name = m_nameInput ? sanitizeRoomName(std::string(m_nameInput->getString())) : std::string();
    config.password = (!m_editMode && m_passwordInput) ? std::string(m_passwordInput->getString()) : std::string();
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
        CCDirector::get()->popScene();
        return;
    }

    PaiDrawManager::get().createRoom(config);
    CCDirector::get()->replaceScene(PaiDrawRoomLayer::scene());
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
    auto win = CCDirector::get()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu, 10);

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

    float btnY = kBottomBar / 2.f - 4.f;
    m_readyButton = makeTextButton(this, "Ready", menu_selector(PaiDrawRoomLayer::onReady),
        {leftX + 50.f, btnY}, m_menu, 0.65f, "GJ_button_01.png");
    m_startButton = makeTextButton(this, "Start", menu_selector(PaiDrawRoomLayer::onStart),
        {leftX + 138.f, btnY}, m_menu, 0.65f, "GJ_button_02.png");

    if (auto* gear = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png")) {
        gear->setScale(0.55f);
        m_settingsButton = CCMenuItemSpriteExtra::create(
            gear, this, menu_selector(PaiDrawRoomLayer::onOpenCreate));
        m_settingsButton->setPosition({leftX + 220.f, btnY});
        m_menu->addChild(m_settingsButton);
    } else {
        m_settingsButton = makeTextButton(this, "Settings", menu_selector(PaiDrawRoomLayer::onOpenCreate),
            {leftX + 220.f, btnY}, m_menu, 0.58f, "GJ_button_06.png");
    }
}

CCNode* PaiDrawRoomLayer::createPlayerRow(PlayerInfo const& player, float width, float height) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    bool placedRowBg = false;
    if (auto* cell = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
        cell->setContentSize({width, height});
        cell->setAnchorPoint({0.f, 0.f});
        cell->setOpacity(player.host ? 240 : 220);
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

    if (auto* icon = makePlayerIcon(player, 26.f)) {
        icon->setPosition({22.f, height / 2.f});
        row->addChild(icon, 2);
    }

    float textX = 42.f;
    auto* name = makeLabel(player.name, "bigFont.fnt", 0.45f,
        {textX, height / 2.f + 7.f}, theme::kTextOnDark, {0.f, 0.5f});
    name->limitLabelWidth(width - textX - 80.f, 0.45f, 0.24f);
    row->addChild(name, 3);

    auto* ping = makeLabel(fmt::format("{} ms", player.pingMs),
        "chatFont.fnt", 0.55f,
        {textX, height / 2.f - 9.f}, theme::kTextSubtle, {0.f, 0.5f});
    row->addChild(ping, 3);

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
    auto state = PaiDrawManager::get().snapshot();
    auto const& room = state.currentRoom;
    if (m_roomTitle) m_roomTitle->setString(room.config.name.empty() ? "PaiDraw Room" : room.config.name.c_str());
    if (m_roomMeta) {
        m_roomMeta->setString(fmt::format("{}  |  {} rounds  |  {}s  |  {}/{} players",
            modeLabel(room.config.mode), room.config.rounds, room.config.roundTimeSeconds,
            room.playerCount(), room.config.maxPlayers).c_str());
    }

    auto local = std::find_if(room.players.begin(), room.players.end(), [&](PlayerInfo const& player) {
        return player.accountID == state.localAccountID;
    });
    bool isHost = local != room.players.end() && local->host;
    bool isReady = local != room.players.end() && local->ready;
    if (m_readyButton) updateButtonState(m_readyButton, isReady);
    if (m_startButton) m_startButton->setVisible(isHost);
    if (m_settingsButton) m_settingsButton->setVisible(isHost);

    if (room.state == RoomState::InGame && !m_enteringGame) {
        m_enteringGame = true;
        CCDirector::get()->pushScene(PaiDrawGameLayer::scene());
    }
    rebuildPlayers();
    rebuildChat();
}

void PaiDrawRoomLayer::onBack(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_roomSub);
    paimon::EventBus::get().unsubscribe(m_chatSub);
    PaiDrawManager::get().leaveRoom();
    CCDirector::get()->popScene();
}

void PaiDrawRoomLayer::onExit() {
    paimon::EventBus::get().unsubscribe(m_roomSub);
    paimon::EventBus::get().unsubscribe(m_chatSub);
    CCLayer::onExit();
}

void PaiDrawRoomLayer::onReady(CCObject*) {
    PaiDrawManager::get().toggleReady();
}

void PaiDrawRoomLayer::onStart(CCObject*) {
    if (m_enteringGame) return;
    m_enteringGame = true;
    PaiDrawManager::get().startGame();
    CCDirector::get()->pushScene(PaiDrawGameLayer::scene());
}

void PaiDrawRoomLayer::onSendChat(CCObject*) {
    if (!m_chatInput) return;
    auto text = m_chatInput->getString();
    PaiDrawManager::get().sendRoomChat(text);
    m_chatInput->setString("");
}

void PaiDrawRoomLayer::onOpenCreate(CCObject*) {
    CCDirector::get()->pushScene(PaiDrawCreateRoomLayer::scene(true));
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

    auto* paper = paimon::SpriteHelper::createRoundedRect(kCanvasWidth, kCanvasHeight, 10.f,
        {1.f, 1.f, 1.f, 1.f}, {0.22f, 0.22f, 0.28f, 1.f}, 1.5f);
    paper->setPosition({0.f, 0.f});
    this->addChild(paper, 0);

    bool placedFrame = false;
    if (auto* frame = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        frame->setContentSize({kCanvasWidth + 6.f, kCanvasHeight + 6.f});
        frame->setAnchorPoint({0.5f, 0.5f});
        frame->setPosition({kCanvasWidth / 2.f, kCanvasHeight / 2.f});
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

    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(kCanvasWidth - 8.f, kCanvasHeight - 8.f, 8.f);
    m_clipper = CCClippingNode::create(stencil);
    m_clipper->setPosition({4.f, 4.f});
    this->addChild(m_clipper, 2);

    m_strokeLayer = CCNode::create();
    m_strokeLayer->setContentSize({kCanvasWidth - 8.f, kCanvasHeight - 8.f});
    m_clipper->addChild(m_strokeLayer, 0);

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
        if (node) node->release();
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
    }
}

PaiDrawCanvasNode::~PaiDrawCanvasNode() {
    for (auto* node : m_redoStack) {
        if (node) node->release();
    }
    m_redoStack.clear();
}

void PaiDrawCanvasNode::redoLast() {
    if (m_redoStack.empty()) return;
    auto* group = m_redoStack.back();
    m_redoStack.pop_back();
    if (group && m_strokeLayer) {
        m_strokeLayer->addChild(group, 1);
        m_groupStack.push_back(group);
    }
    if (group) group->release();
}

void PaiDrawCanvasNode::addStroke(StrokeSegment const& stroke) {
    if (!m_strokeLayer) return;
    auto* node = PaimonDrawNode::create();

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

    this->schedule(schedule_selector(PaiDrawGameLayer::tickLocalTimer), 0.1f);
    return true;
}

void PaiDrawGameLayer::keyBackClicked() { onBack(nullptr); }

void PaiDrawGameLayer::buildLayout() {
    auto win = CCDirector::get()->getWinSize();
    addNativeBackground(this);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 10);

    auto* backSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto* backButton = CCMenuItemSpriteExtra::create(
        backSprite, this, menu_selector(PaiDrawGameLayer::onBack));
    backButton->setPosition({26.f, win.height - 22.f});
    m_menu->addChild(backButton);

    constexpr float kHeaderH = 54.f;
    constexpr float kSidePad = 8.f;
    constexpr float kPanelGap = 8.f;
    constexpr float kDockH = 42.f;
    constexpr float kDockGap = 7.f;

    m_header = makeLabel("ROUND 1/6", "goldFont.fnt", 0.42f,
        {58.f, win.height - 16.f}, theme::kAccentGold, {0.f, 0.5f});
    m_header->limitLabelWidth(win.width * 0.32f, 0.42f, 0.28f);
    this->addChild(m_header, 5);

    if (auto* wordBg = paimon::SpriteHelper::safeCreateNineSlice("GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        wordBg->setContentSize({std::min(win.width * 0.40f, 220.f), 31.f});
        wordBg->setAnchorPoint({0.5f, 0.5f});
        wordBg->setPosition({win.width / 2.f, win.height - 28.f});
        wordBg->setOpacity(235);
        this->addChild(wordBg, 4);
    }

    m_wordLabel = makeLabel("_ _ _ _", "bigFont.fnt", 0.50f,
        {win.width / 2.f, win.height - 28.f}, theme::kTextOnDark);
    m_wordLabel->limitLabelWidth(std::min(win.width * 0.36f, 200.f), 0.50f, 0.30f);
    this->addChild(m_wordLabel, 5);

    if (auto* timerBg = paimon::SpriteHelper::safeCreateNineSlice("GJ_square05.png", {6.f, 6.f, 6.f, 6.f})) {
        timerBg->setContentSize({56.f, 30.f});
        timerBg->setOpacity(220);
        timerBg->setColor(theme::kPanelTint);
        timerBg->setAnchorPoint({0.5f, 0.5f});
        timerBg->setPosition({win.width - 37.f, win.height - 27.f});
        this->addChild(timerBg, 4);
    }

    m_timerLabel = makeLabel("90s", "goldFont.fnt", 0.55f,
        {win.width - 37.f, win.height - 27.f}, theme::kAccentGold);
    this->addChild(m_timerLabel, 5);

    auto* separator = CCLayerColor::create({255, 255, 255, 60});
    separator->setContentSize({win.width - 80.f, 1.f});
    separator->setPosition({40.f, win.height - kHeaderH});
    this->addChild(separator, 4);

    float playBottom = kDockH + kDockGap + 2.f;
    float playTop = win.height - kHeaderH - 4.f;
    float playH = playTop - playBottom;
    float leftW = std::clamp(win.width * 0.18f, 86.f, 116.f);
    float rightW = std::clamp(win.width * 0.23f, 104.f, 146.f);
    float canvasX = kSidePad + leftW + kPanelGap;
    float canvasW = win.width - kSidePad * 2.f - kPanelGap * 2.f - leftW - rightW;
    float rightX = canvasX + canvasW + kPanelGap;

    auto* playerPanel = makeFramedPanel(leftW, playH,
        theme::kPanelTint, theme::kPanelInnerTint, 245);
    playerPanel->setPosition({kSidePad, playBottom});
    this->addChild(playerPanel, 0);

    auto* playerHeader = makeSectionStrip("PLAYERS", leftW - 12.f, theme::kDragBarTint,
        theme::kAccentGold, 0.48f);
    playerHeader->setPosition({kSidePad + 6.f, playTop - 30.f});
    this->addChild(playerHeader, 5);

    constexpr float kBrushAreaH = 38.f;
    m_scoreScroll = geode::ScrollLayer::create({leftW - 14.f, playH - 44.f - kBrushAreaH});
    m_scoreScroll->setPosition({kSidePad + 7.f, playBottom + 8.f + kBrushAreaH});
    this->addChild(m_scoreScroll, 3);

    auto* brushLabel = makeLabel("BRUSH SIZE", "goldFont.fnt", 0.32f,
        {kSidePad + leftW / 2.f, playBottom + 32.f}, theme::kAccentLightGold);
    this->addChild(brushLabel, 5);

    int brushIndex = 0;
    for (char const* label : {"S", "M", "L"}) {
        auto* sprite = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.65f);
        sprite->setScale(0.42f);
        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(PaiDrawGameLayer::onBrush));
        button->setTag(brushIndex);
        button->setPosition({kSidePad + leftW / 2.f + (brushIndex - 1) * 28.f, playBottom + 14.f});
        m_menu->addChild(button);
        m_brushButtons.push_back(button);
        m_drawButtons.push_back(button);
        ++brushIndex;
    }

    auto* chatPanel = makeFramedPanel(rightW, playH,
        theme::kPanelTint, theme::kPanelInnerTint, 245);
    chatPanel->setPosition({rightX, playBottom});
    this->addChild(chatPanel, 0);

    auto* chatHeader = makeSectionStrip("LIVE CHAT", rightW - 12.f, theme::kDragBarTint,
        theme::kAccentGold, 0.46f);
    chatHeader->setPosition({rightX + 6.f, playTop - 30.f});
    this->addChild(chatHeader, 5);

    constexpr float kGuessAreaH = 38.f;
    m_chatScroll = geode::ScrollLayer::create({rightW - 14.f, playH - 44.f - kGuessAreaH});
    m_chatScroll->setPosition({rightX + 7.f, playBottom + 8.f + kGuessAreaH});
    this->addChild(m_chatScroll, 3);

    float guessInputW = std::max(rightW - 58.f, 50.f);
    m_guessInput = geode::TextInput::create(guessInputW, "Type your guess...");
    m_guessInput->setMaxCharCount(64);
    m_guessInput->setPosition({rightX + 8.f + guessInputW / 2.f, playBottom + 20.f});
    this->addChild(m_guessInput, 5);

    auto* guessSprite = ButtonSprite::create("Guess", "bigFont.fnt", "GJ_button_01.png", 0.65f);
    guessSprite->setScale(0.43f);
    m_guessButton = CCMenuItemSpriteExtra::create(
        guessSprite, this, menu_selector(PaiDrawGameLayer::onSendGuess));
    m_guessButton->setPosition({rightX + rightW - 24.f, playBottom + 20.f});
    m_menu->addChild(m_guessButton);

    auto* canvasPanel = makeFramedPanel(canvasW, playH,
        theme::kPanelTint, theme::kPanelInnerTint, 245);
    canvasPanel->setPosition({canvasX, playBottom});
    this->addChild(canvasPanel, 0);

    CCRect canvasRect = {canvasX + 7.f, playBottom + 7.f, canvasW - 14.f, playH - 14.f};
    float canvasScale = std::min(
        canvasRect.size.width / kCanvasWidth,
        canvasRect.size.height / kCanvasHeight
    );

    m_canvas = PaiDrawCanvasNode::create();
    m_canvas->setAnchorPoint({0.5f, 0.5f});
    m_canvas->ignoreAnchorPointForPosition(false);
    m_canvas->setScale(canvasScale);
    m_canvas->setPosition(ccp(canvasRect.getMidX(), canvasRect.getMidY()));
    this->addChild(m_canvas, 2);

    auto* dock = makeFramedPanel(canvasW, kDockH,
        theme::kPanelTint, theme::kPanelInnerTint, 245);
    dock->setPosition({canvasX, 4.f});
    this->addChild(dock, 0);

    float paletteW = std::min(126.f, canvasW * 0.45f);
    constexpr float kPaletteGap = 2.f;
    float swatchSize = std::clamp((paletteW - kPaletteGap * 7.f) / 8.f, 10.f, 15.f);
    float paletteStartX = canvasX + 7.f + swatchSize / 2.f;
    float paletteStartY = 14.f;
    for (size_t index = 0; index < kDrawPalette.size(); ++index) {
        auto color = kDrawPalette[index];
        cocos2d::CCNode* swatchNode = nullptr;
        if (auto* swatch = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png")) {
            float maxDim = std::max(swatch->getContentSize().width, swatch->getContentSize().height);
            if (maxDim > 0.f) swatch->setScale(swatchSize / maxDim);
            swatch->setColor(color);
            swatchNode = swatch;
        } else {
            auto* fallback = CCLayerColor::create(ccc4(color.r, color.g, color.b, 255));
            fallback->setContentSize({swatchSize, swatchSize});
            swatchNode = fallback;
        }

        auto* button = CCMenuItemSpriteExtra::create(
            swatchNode, this, menu_selector(PaiDrawGameLayer::onColor));
        button->setTag(static_cast<int>(index));
        int row = static_cast<int>(index / 8);
        int col = static_cast<int>(index % 8);
        button->setPosition({
            paletteStartX + col * (swatchSize + kPaletteGap),
            paletteStartY + row * (swatchSize + kPaletteGap)
        });
        m_menu->addChild(button);
        m_colorButtons.push_back(button);
        m_drawButtons.push_back(button);
    }

    struct DockButton {
        char const* label;
        cocos2d::SEL_MenuHandler callback;
        int tag;
        bool tool;
    };
    std::array<DockButton, 4> dockButtons = {{
        {"Pen", menu_selector(PaiDrawGameLayer::onTool), static_cast<int>(PaiDrawCanvasNode::Tool::Pencil), true},
        {"Line", menu_selector(PaiDrawGameLayer::onTool), static_cast<int>(PaiDrawCanvasNode::Tool::Line), true},
        {"Erase", menu_selector(PaiDrawGameLayer::onTool), static_cast<int>(PaiDrawCanvasNode::Tool::Eraser), true},
        {"Clear", menu_selector(PaiDrawGameLayer::onClearCanvas), 0, false},
    }};

    float toolsX = canvasX + 12.f + paletteW;
    float toolsW = canvasX + canvasW - 7.f - toolsX;
    float toolSlotW = toolsW / static_cast<float>(dockButtons.size());
    for (size_t index = 0; index < dockButtons.size(); ++index) {
        auto const& entry = dockButtons[index];
        auto* sprite = ButtonSprite::create(entry.label, "bigFont.fnt", "GJ_button_04.png", 0.58f);
        float scale = std::min(0.42f, (toolSlotW - 2.f) / std::max(sprite->getContentSize().width, 1.f));
        sprite->setScale(scale);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, entry.callback);
        button->setTag(entry.tag);
        button->setPosition({toolsX + toolSlotW * (static_cast<float>(index) + 0.5f), 25.f});
        m_menu->addChild(button);
        m_drawButtons.push_back(button);
        if (entry.tool) m_toolButtons.push_back(button);
    }

    refreshToolButtons();
    refreshPalette();
}

void PaiDrawGameLayer::refreshHeader() {
    auto state = PaiDrawManager::get().snapshot();
    if (m_header) {
        std::string drawer = state.currentRound.drawingPlayerName.empty()
            ? std::string("Waiting for drawer")
            : state.currentRound.drawingPlayerName;
        m_header->setString(fmt::format("ROUND {}/{}  |  {} DRAWS",
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
        int seconds = state.currentRound.timeLeftSeconds;
        if (state.currentRound.endsAtLocalMs > 0) {
            uint64_t now = nowMs();
            seconds = state.currentRound.endsAtLocalMs > now
                ? static_cast<int>((state.currentRound.endsAtLocalMs - now) / 1000ULL)
                : 0;
        }
        m_timerLabel->setString(fmt::format("{}s", std::max(seconds, 0)).c_str());
        m_timerLabel->setColor(seconds <= 10 ? theme::kAccentRed : theme::kAccentGold);
    }

    bool canDraw = state.currentRound.localPlayerIsDrawer || state.currentRound.drawingPlayerId == 0;
    if (m_canvas) {
        m_canvas->setReadOnly(!canDraw);
    }
    if (m_guessInput) {
        m_guessInput->setEnabled(!canDraw);
        m_guessInput->setPlaceholder(canDraw ? "You are drawing" : "Type your guess...");
    }
    if (m_guessButton) m_guessButton->setEnabled(!canDraw);
    for (auto* button : m_drawButtons) {
        button->setEnabled(canDraw);
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
    m_timerLabel->setColor(seconds <= 10 ? theme::kAccentRed : theme::kAccentGold);
}

CCNode* PaiDrawGameLayer::createScoreRow(PlayerInfo const& player, float width, float height) {
    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    if (auto* bg = paimon::SpriteHelper::safeCreateNineSlice(
            "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
        bg->setContentSize({width, height});
        bg->setAnchorPoint({0.f, 0.f});
        bg->setColor(player.guessed ? theme::kAccentGreen : theme::kPanelTint);
        bg->setOpacity(player.guessed ? 235 : 205);
        row->addChild(bg, 0);
    }

    if (auto* icon = makePlayerIcon(player, 21.f)) {
        icon->setPosition({15.f, height / 2.f});
        row->addChild(icon, 2);
    }

    auto* name = makeLabel(player.name, "bigFont.fnt", 0.34f,
        {29.f, height / 2.f + 6.f}, theme::kTextOnDark, {0.f, 0.5f});
    name->limitLabelWidth(width - 36.f, 0.34f, 0.22f);
    row->addChild(name, 2);

    auto* score = makeLabel(fmt::format("{} pts", player.score), "goldFont.fnt", 0.36f,
        {29.f, height / 2.f - 8.f}, player.guessed ? theme::kAccentGreen : theme::kAccentGold,
        {0.f, 0.5f});
    row->addChild(score, 2);
    return row;
}

void PaiDrawGameLayer::rebuildScoreboard() {
    if (!m_scoreScroll) return;
    auto players = PaiDrawManager::get().snapshot().currentRoom.players;
    std::stable_sort(players.begin(), players.end(), [](PlayerInfo const& lhs, PlayerInfo const& rhs) {
        return lhs.score > rhs.score;
    });

    std::vector<CCNode*> rows;
    rows.reserve(players.size());
    float width = m_scoreScroll->getContentSize().width;
    for (auto const& player : players) {
        rows.push_back(createScoreRow(player, width, 35.f));
    }
    fillScroll(m_scoreScroll, rows, 35.f, 4.f);
}

void PaiDrawGameLayer::rebuildChat() {
    if (!m_chatScroll) return;
    auto messages = PaiDrawManager::get().snapshot().roomChat;
    std::vector<CCNode*> rows;
    float width = m_chatScroll->getContentSize().width;
    size_t first = messages.size() > 20 ? messages.size() - 20 : 0;

    for (size_t index = messages.size(); index > first; --index) {
        auto const& message = messages[index - 1];
        auto* row = CCNode::create();
        row->setContentSize({width, 31.f});
        row->setAnchorPoint({0.5f, 0.5f});

        cocos2d::ccColor3B tint = theme::kPanelTint;
        if (message.correct) tint = theme::kAccentGreen;
        else if (message.nearGuess) tint = theme::kAccentGold;
        if (auto* bg = paimon::SpriteHelper::safeCreateNineSlice(
                "GJ_commentCell_001.png", {6.f, 6.f, 6.f, 6.f})) {
            bg->setContentSize({width, 31.f});
            bg->setAnchorPoint({0.f, 0.f});
            bg->setColor(tint);
            bg->setOpacity(message.correct || message.nearGuess ? 225 : 195);
            row->addChild(bg, 0);
        }

        auto senderColor = message.system ? theme::kAccentGold : theme::kTextOnDark;
        auto* sender = makeLabel(message.system ? "PAIDRAW" : message.senderName,
            "bigFont.fnt", 0.29f, {6.f, 21.f}, senderColor, {0.f, 0.5f});
        sender->limitLabelWidth(width - 12.f, 0.29f, 0.20f);
        row->addChild(sender, 2);

        auto* text = makeLabel(message.text, "chatFont.fnt", 0.48f,
            {6.f, 8.f}, theme::kTextSubtle, {0.f, 0.5f});
        text->limitLabelWidth(width - 12.f, 0.48f, 0.28f);
        row->addChild(text, 2);
        rows.push_back(row);
    }
    fillScroll(m_chatScroll, rows, 31.f, 4.f);
}

void PaiDrawGameLayer::refreshState() {
    auto state = PaiDrawManager::get().snapshot();
    if (state.currentRoom.state == RoomState::InGame) m_seenActiveRound = true;
    if (!m_showingResults && m_seenActiveRound && state.currentRoom.state != RoomState::InGame &&
        !state.results.leaderboard.empty()) {
        m_showingResults = true;
        CCDirector::get()->replaceScene(PaiDrawResultsLayer::scene());
        return;
    }

    refreshHeader();
    rebuildScoreboard();
    rebuildChat();
    if (m_canvas) {
        m_canvas->clearCanvas();
        for (auto const& stroke : state.recentStrokes) {
            m_canvas->addStroke(stroke);
        }
    }
    refreshToolButtons();
    refreshPalette();
}

void PaiDrawGameLayer::onExit() {
    this->unschedule(schedule_selector(PaiDrawGameLayer::tickLocalTimer));
    paimon::EventBus::get().unsubscribe(m_roomSub);
    paimon::EventBus::get().unsubscribe(m_chatSub);
    paimon::EventBus::get().unsubscribe(m_roundSub);
    paimon::EventBus::get().unsubscribe(m_strokeSub);
    CCLayer::onExit();
}

void PaiDrawGameLayer::onBack(CCObject*) {
    CCDirector::get()->popScene();
}

void PaiDrawGameLayer::onSendGuess(CCObject*) {
    if (!m_guessInput) return;
    if (localCanDraw()) return;

    std::string text = m_guessInput->getString();
    geode::utils::string::trimIP(text);
    if (text.empty()) return;
    PaiDrawManager::get().sendGuess(text);
    m_guessInput->setString("");
}

void PaiDrawGameLayer::onColor(CCObject* sender) {
    if (!localCanDraw()) return;
    int index = sender->getTag();
    if (index >= 0 && index < static_cast<int>(kDrawPalette.size()) && m_canvas) {
        m_canvas->setDrawColor(kDrawPalette[static_cast<size_t>(index)]);
        m_canvas->setTool(PaiDrawCanvasNode::Tool::Pencil);
        refreshPalette();
        refreshToolButtons();
    }
}

void PaiDrawGameLayer::onBrush(CCObject* sender) {
    if (!localCanDraw()) return;
    int idx = sender->getTag();
    float size = idx == 0 ? 4.f : idx == 1 ? 8.f : 13.f;
    if (m_canvas) {
        m_canvas->setBrushSize(size);
        refreshToolButtons();
    }
}

void PaiDrawGameLayer::onTool(CCObject* sender) {
    if (!m_canvas || !localCanDraw()) return;
    m_canvas->setTool(static_cast<PaiDrawCanvasNode::Tool>(sender->getTag()));
    refreshToolButtons();
}

void PaiDrawGameLayer::onClearCanvas(CCObject*) {
    if (!localCanDraw()) return;
    WeakRef<PaiDrawGameLayer> weakSelf = this;
    geode::createQuickPopup("Clear canvas", "Erase the whole drawing?", "Cancel", "Clear",
        [weakSelf](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            auto self = weakSelf.lock();
            if (!self || !self->localCanDraw()) return;
            if (self->m_canvas) self->m_canvas->clearCanvas();
            PaiDrawManager::get().clearCanvas();
        });
}

void PaiDrawGameLayer::refreshToolButtons() {
    if (!m_canvas) return;
    int activeTool = static_cast<int>(m_canvas->getTool());
    for (auto* button : m_toolButtons) {
        updateButtonState(button, button->getTag() == activeTool);
    }

    float size = m_canvas->getBrushSize();
    int activeBrush = size < 6.f ? 0 : size < 11.f ? 1 : 2;
    for (auto* button : m_brushButtons) {
        updateButtonState(button, button->getTag() == activeBrush);
    }
}

void PaiDrawGameLayer::refreshPalette() {
    if (!m_canvas) return;
    auto active = m_canvas->getDrawColor();
    for (size_t index = 0; index < m_colorButtons.size() && index < kDrawPalette.size(); ++index) {
        auto color = kDrawPalette[index];
        bool selected = active.r == color.r && active.g == color.g && active.b == color.b;
        m_colorButtons[index]->setScale(selected ? 1.18f : 1.f);
    }
}

bool PaiDrawGameLayer::localCanDraw() const {
    auto round = PaiDrawManager::get().snapshot().currentRound;
    return round.localPlayerIsDrawer || round.drawingPlayerId == 0;
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
    auto win = CCDirector::get()->getWinSize();
    addNativeBackground(this);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

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

    constexpr float kSidePad = 20.f;
    constexpr float kPodiumH = 130.f;
    constexpr float kBottomBar = 56.f;
    constexpr float kHeaderArea = 64.f;
    constexpr float kStatsBar = 32.f;

    float podiumW = std::min(win.width - kSidePad * 2.f, 720.f);
    float podiumX = (win.width - podiumW) / 2.f;
    float podiumY = win.height - kHeaderArea - kPodiumH - 8.f;

    auto* podium = makeFramedPanel(podiumW, kPodiumH,
        theme::kPanelTint, theme::kPanelInnerTint, 255);
    podium->setPosition({podiumX, podiumY});
    this->addChild(podium, 0);

    m_podiumLayer = CCNode::create();
    m_podiumLayer->setContentSize({podiumW, kPodiumH});
    m_podiumLayer->setPosition({podiumX, podiumY});
    this->addChild(m_podiumLayer, 3);

    auto* podiumStrip = makeSectionStrip("PODIUM", podiumW - 20.f);
    podiumStrip->setPosition({podiumX + 10.f, podiumY + kPodiumH - 30.f});
    this->addChild(podiumStrip, 5);

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
    if (m_podiumLayer) {
        m_podiumLayer->removeAllChildren();
        auto podiumSize = m_podiumLayer->getContentSize();
        std::array<size_t, 3> order = {1, 0, 2};
        std::array<float, 3> xPositions = {
            podiumSize.width * 0.25f,
            podiumSize.width * 0.50f,
            podiumSize.width * 0.75f,
        };
        std::array<float, 3> stepHeights = {34.f, 48.f, 28.f};
        std::array<cocos2d::ccColor3B, 3> rankColors = {{
            {220, 220, 220}, theme::kAccentGold, {200, 130, 80},
        }};
        float stepW = std::min(podiumSize.width / 3.f - 14.f, 108.f);

        for (size_t slot = 0; slot < order.size(); ++slot) {
            size_t playerIndex = order[slot];
            if (playerIndex >= results.leaderboard.size()) continue;
            auto const& player = results.leaderboard[playerIndex];
            float x = xPositions[slot];
            float stepH = stepHeights[slot];

            auto* step = makeFramedPanel(stepW, stepH, rankColors[slot], rankColors[slot], 220);
            step->setPosition({x - stepW / 2.f, 8.f});
            m_podiumLayer->addChild(step, 1);

            auto* rank = makeLabel(fmt::format("#{}", playerIndex + 1), "goldFont.fnt", 0.52f,
                {x, 8.f + stepH / 2.f + 5.f}, rankColors[slot]);
            m_podiumLayer->addChild(rank, 3);

            if (auto* icon = makePlayerIcon(player, 24.f)) {
                icon->setPosition({x, 8.f + stepH + 13.f});
                m_podiumLayer->addChild(icon, 3);
            }

            auto* name = makeLabel(player.name, "bigFont.fnt", 0.34f,
                {x, 8.f + stepH + 31.f}, theme::kTextOnDark);
            name->limitLabelWidth(stepW, 0.34f, 0.22f);
            m_podiumLayer->addChild(name, 3);

            auto* score = makeLabel(fmt::format("{} pts", player.score), "chatFont.fnt", 0.52f,
                {x, 14.f}, theme::kTextOnDark);
            m_podiumLayer->addChild(score, 3);
        }
    }

    std::vector<CCNode*> rows;
    float width = m_tableScroll->getContentSize().width;
    for (size_t index = 0; index < results.leaderboard.size(); ++index) {
        auto const& player = results.leaderboard[index];
        auto* row = CCNode::create();
        row->setContentSize({width, 44.f});
        row->setAnchorPoint({0.5f, 0.5f});

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

        std::string rankLabel = fmt::format("#{}", index + 1);
        cocos2d::ccColor3B rankColor = theme::kAccentGold;
        if (index == 0) rankColor = theme::kAccentGold;
        else if (index == 1) rankColor = {220, 220, 220};
        else if (index == 2) rankColor = {200, 130, 80};
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

void PaiDrawResultsLayer::onExit() {
    paimon::EventBus::get().unsubscribe(m_resultsSub);
    CCLayer::onExit();
}

void PaiDrawResultsLayer::onBackLobby(CCObject*) {
    paimon::EventBus::get().unsubscribe(m_resultsSub);
    CCDirector::get()->replaceScene(PaiDrawLobbyLayer::scene());
}

void PaiDrawResultsLayer::onPlayAgain(CCObject*) {
    CCDirector::get()->replaceScene(PaiDrawRoomLayer::scene());
}

}
