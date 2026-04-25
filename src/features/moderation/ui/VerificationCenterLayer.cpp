#include "VerificationCenterLayer.hpp"
#include "../../../framework/state/SessionState.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <algorithm>
#include "../../../utils/Localization.hpp"
#include "BanListPopup.hpp"
#include "WhitelistPopup.hpp"
#include "UserReportsPopup.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// forward del popup de thumbnails (definido en LevelInfoLayer.cpp)
extern CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, std::vector<Suggestion> const& suggestions);

// ── factory ──────────────────────────────────────────────

VerificationCenterLayer* VerificationCenterLayer::create() {
    auto ret = new VerificationCenterLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* VerificationCenterLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(VerificationCenterLayer::create());
    return scene;
}

// ── init ─────────────────────────────────────────────────

bool VerificationCenterLayer::init() {
    if (!CCLayer::init()) return false;

    this->setKeypadEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo opaco GD estandar
    auto bg = CCLayerColor::create(ccc4(18, 18, 40, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -2);

    // bordes decorativos (estilo GD)
    auto bottomLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomLeft) {
        bottomLeft->setAnchorPoint({0, 0});
        bottomLeft->setPosition({-2, -2});
        bottomLeft->setOpacity(100);
        this->addChild(bottomLeft, -1);
    }
    auto bottomRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomRight) {
        bottomRight->setAnchorPoint({1, 0});
        bottomRight->setPosition({winSize.width + 2, -2});
        bottomRight->setFlipX(true);
        bottomRight->setOpacity(100);
        this->addChild(bottomRight, -1);
    }

    // titulo
    auto title = CCLabelBMFont::create(
        Localization::get().getString("queue.title").c_str(), "goldFont.fnt"
    );
    title->setPosition({winSize.width / 2, winSize.height - 22.f});
    title->setScale(0.8f);
    this->addChild(title, 2);

    // ── pestanas ──
    m_tabsMenu = CCMenu::create();
    m_tabsMenu->setID("tabs-menu"_spr);
    m_tabsMenu->setPosition({winSize.width / 2, winSize.height - 50.f});

    auto mkTab = [&](char const* label, SEL_MenuHandler sel, PendingCategory cat) {
        auto spr = ButtonSprite::create(label, 80, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.55f);
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setTag(static_cast<int>(cat));
        return btn;
    };

    m_tabsMenu->addChild(mkTab(
        Localization::get().getString("queue.verify_tab").c_str(),
        menu_selector(VerificationCenterLayer::onTabVerify), PendingCategory::Verify));
    m_tabsMenu->addChild(mkTab(
        Localization::get().getString("queue.update_tab").c_str(),
        menu_selector(VerificationCenterLayer::onTabUpdate), PendingCategory::Update));
    m_tabsMenu->addChild(mkTab(
        Localization::get().getString("queue.report_tab").c_str(),
        menu_selector(VerificationCenterLayer::onTabReport), PendingCategory::Report));
    m_tabsMenu->addChild(mkTab("PBG",
        menu_selector(VerificationCenterLayer::onTabProfileBackground), PendingCategory::ProfileBackground));
    m_tabsMenu->addChild(mkTab("PI",
        menu_selector(VerificationCenterLayer::onTabProfileImg), PendingCategory::ProfileImg));

    // btn baneados
    {
        auto spr = ButtonSprite::create(
            Localization::get().getString("queue.banned_btn").c_str(),
            70, true, "bigFont.fnt", "GJ_button_05.png", 28.f, 0.55f);
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onViewBans));
        btn->setID("banned-users-btn"_spr);
        m_tabsMenu->addChild(btn);
    }

    // btn whitelist
    {
        auto spr = ButtonSprite::create(
            "WL", 50, true, "bigFont.fnt", "GJ_button_04.png", 28.f, 0.55f);
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onViewWhitelist));
        btn->setID("whitelist-btn"_spr);
        m_tabsMenu->addChild(btn);
    }

    m_tabsMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center));
    this->addChild(m_tabsMenu, 2);

    // ── panel izquierdo: lista con scroll ──
    float listW = winSize.width * 0.52f;
    float listH = winSize.height - 100.f;
    float listX = 18.f;
    float listY = 35.f;

    // fondo lista
    auto listBg = paimon::SpriteHelper::createDarkPanel(listW, listH, 80);
    listBg->setPosition({listX, listY});
    this->addChild(listBg, 0);

    // contenedor pa scroll + scrollbar
    m_listContainer = CCNode::create();
    m_listContainer->setID("list-container"_spr);
    m_listContainer->setContentSize({listW, listH});
    m_listContainer->setAnchorPoint({0, 0});
    m_listContainer->setPosition({listX, listY});
    this->addChild(m_listContainer, 1);

    m_scrollLayer = ScrollLayer::create({listW - 14.f, listH});
    m_scrollLayer->setPosition({0, 0});
    m_listContainer->addChild(m_scrollLayer);

    m_scrollbar = Scrollbar::create(m_scrollLayer);
    m_scrollbar->setPosition({listW - 8.f, listH / 2});
    m_scrollbar->setContentSize({8.f, listH});
    m_listContainer->addChild(m_scrollbar, 10);

    // ── panel derecho: preview ──
    float previewX = listX + listW + 10.f;
    float previewW = winSize.width - previewX - 18.f;
    float previewH = listH;
    float previewY = listY;

    auto previewBg = paimon::SpriteHelper::createDarkPanel(previewW, previewH, 60);
    previewBg->setPosition({previewX, previewY});
    this->addChild(previewBg, 0);

    m_previewPanel = CCNode::create();
    m_previewPanel->setID("preview-panel"_spr);
    m_previewPanel->setContentSize({previewW, previewH});
    m_previewPanel->setAnchorPoint({0, 0});
    m_previewPanel->setPosition({previewX, previewY});
    this->addChild(m_previewPanel, 1);

    // borde preview
    m_previewBorder = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (m_previewBorder) {
        static_cast<CCScale9Sprite*>(m_previewBorder)->setContentSize({previewW + 4.f, previewH + 4.f});
        m_previewBorder->setAnchorPoint({0, 0});
        m_previewBorder->setPosition({previewX - 2.f, previewY - 2.f});
        this->addChild(m_previewBorder, 2);
    }

    // label "selecciona un item"
    m_previewLabel = CCLabelBMFont::create(
        Localization::get().getString("queue.select_item").c_str(), "bigFont.fnt");
    if (std::string(m_previewLabel->getString()).empty()) {
        m_previewLabel->setString("Select an item");
    }
    m_previewLabel->setScale(0.4f);
    m_previewLabel->setOpacity(120);
    m_previewLabel->setPosition({previewW / 2, previewH / 2});
    m_previewPanel->addChild(m_previewLabel, 5);

    // ── suggestion navigation arrows ──
    m_previewNavMenu = CCMenu::create();
    m_previewNavMenu->setPosition({0, 0});
    m_previewNavMenu->setContentSize({previewW, previewH});
    m_previewPanel->addChild(m_previewNavMenu, 20);

    {
        auto overlay = CCSprite::create();
        overlay->setContentSize({previewW - 60.f, previewH - 40.f});
        overlay->setOpacity(0);
        auto previewClickBtn = CCMenuItemSpriteExtra::create(overlay, this,
            menu_selector(VerificationCenterLayer::onPreviewClick));
        previewClickBtn->setPosition({previewW / 2, previewH / 2});
        previewClickBtn->setContentSize({previewW - 60.f, previewH - 40.f});
        m_previewNavMenu->addChild(previewClickBtn, -1);
    }

    {
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        prevSpr->setScale(0.6f);
        m_prevArrowBtn = CCMenuItemSpriteExtra::create(prevSpr, this,
            menu_selector(VerificationCenterLayer::onPrevSuggestion));
        m_prevArrowBtn->setPosition({22.f, previewH / 2});
        m_prevArrowBtn->setVisible(false);
        m_previewNavMenu->addChild(m_prevArrowBtn);
    }

    {
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        nextSpr->setFlipX(true);
        nextSpr->setScale(0.6f);
        m_nextArrowBtn = CCMenuItemSpriteExtra::create(nextSpr, this,
            menu_selector(VerificationCenterLayer::onNextSuggestion));
        m_nextArrowBtn->setPosition({previewW - 22.f, previewH / 2});
        m_nextArrowBtn->setVisible(false);
        m_previewNavMenu->addChild(m_nextArrowBtn);
    }

    m_suggestionCountLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_suggestionCountLabel->setScale(0.35f);
    m_suggestionCountLabel->setPosition({previewW / 2, 15.f});
    m_suggestionCountLabel->setVisible(false);
    m_previewPanel->addChild(m_suggestionCountLabel, 25);

    // ── btn volver ──
    auto backMenu = CCMenu::create();
    backMenu->setID("back-menu"_spr);
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this,
        menu_selector(VerificationCenterLayer::onBack));
    backBtn->setID("back-btn"_spr);
    backBtn->setPosition({-winSize.width / 2 + 25.f, winSize.height / 2 - 25.f});
    backMenu->addChild(backBtn);
    backMenu->setPosition({winSize.width / 2, winSize.height / 2});
    this->addChild(backMenu, 5);

    // ── filter toggle ──
    {
        auto filterMenu = CCMenu::create();
        filterMenu->setPosition({0, 0});
        auto filterSpr = ButtonSprite::create("All", 55, true, "bigFont.fnt", "GJ_button_04.png", 22.f, 0.5f);
        filterSpr->setScale(0.6f);
        auto filterBtn = CCMenuItemSpriteExtra::create(filterSpr, this,
            menu_selector(VerificationCenterLayer::onToggleFilter));
        filterBtn->setID("filter-btn"_spr);
        filterBtn->setPosition({listX + 38.f, listY - 12.f});
        filterMenu->addChild(filterBtn);

        // ── manual refresh button ──
        auto refreshSpr = ButtonSprite::create("Refresh", 70, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.5f);
        refreshSpr->setScale(0.6f);
        m_refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, this,
            menu_selector(VerificationCenterLayer::onRefresh));
        m_refreshBtn->setID("refresh-btn"_spr);
        m_refreshBtn->setPosition({listX + 110.f, listY - 12.f});
        filterMenu->addChild(m_refreshBtn);

        this->addChild(filterMenu, 5);
    }

    // cargar primera tab
    switchTo(PendingCategory::Verify);
    return true;
}

// ── navegacion ───────────────────────────────────────────

void VerificationCenterLayer::onBack(CCObject*) {
    this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, kPopTransitionFade);
}

void VerificationCenterLayer::keyBackClicked() {
    onBack(nullptr);
}

// ── tabs ─────────────────────────────────────────────────

void VerificationCenterLayer::onTabVerify(CCObject*)    { switchTo(PendingCategory::Verify); }
void VerificationCenterLayer::onTabUpdate(CCObject*)    { switchTo(PendingCategory::Update); }
void VerificationCenterLayer::onTabReport(CCObject*)    { switchTo(PendingCategory::Report); }
void VerificationCenterLayer::onTabProfileBackground(CCObject*) { switchTo(PendingCategory::ProfileBackground); }
void VerificationCenterLayer::onTabProfileImg(CCObject*) { switchTo(PendingCategory::ProfileImg); }

void VerificationCenterLayer::switchTo(PendingCategory cat) {
    m_current = cat;
    m_selectedIndex = -1;
    clearPreview();

    // resaltar tab activa
    if (m_tabsMenu) {
        for (auto* n : CCArrayExt<CCNode*>(m_tabsMenu->getChildren())) {
            auto* it = static_cast<CCMenuItemSpriteExtra*>(n);
            bool active = it->getTag() == static_cast<int>(cat);
            it->setScale(active ? 0.82f : 0.68f);
        }
    }

    // loading
    auto content = m_scrollLayer->m_contentLayer;
    content->removeAllChildren();
    auto loadLbl = CCLabelBMFont::create("Loading...", "goldFont.fnt");
    loadLbl->setScale(0.5f);
    auto scrollSize = m_scrollLayer->getContentSize();
    content->setContentSize(scrollSize);
    loadLbl->setPosition(scrollSize / 2);
    content->addChild(loadLbl);

    // sync server
    WeakRef<VerificationCenterLayer> self = this;
    ThumbnailAPI::get().syncVerificationQueue(cat, [self, cat](bool success, std::vector<PendingItem> const& items) {
        auto layer = self.lock();
        if (!layer) return;

        if (!success) {
            log::warn("[VerificationCenter] Failed to sync from server, using local");
            layer->m_allItems = PendingQueue::get().list(cat);
        } else {
            layer->m_allItems = items;
        }
        layer->applyFilter();
        if (layer->getParent()) {
            layer->rebuildList();
        }
    });
}

// ── lista ────────────────────────────────────────────────

void VerificationCenterLayer::rebuildList() {
    if (!m_scrollLayer) return;
    auto content = m_scrollLayer->m_contentLayer;
    if (!content) return;
    content->removeAllChildren();

        auto scrollSize = m_scrollLayer->getContentSize();
        float rowH = 46.f;
        float listW = scrollSize.width;

        // username pa reclamo
        std::string currentUsername;
        if (auto gm = GameManager::get()) {
            currentUsername = gm->m_playerName;
        }

        if (m_items.empty()) {
            content->setContentSize(scrollSize);
            auto lbl = CCLabelBMFont::create(
                Localization::get().getString("queue.no_items").c_str(), "goldFont.fnt");
            lbl->setScale(0.45f);
            lbl->setPosition(scrollSize / 2);
            content->addChild(lbl);
            return;
        }

        float totalH = rowH * m_items.size() + 10.f;
        content->setContentSize({listW, std::max(scrollSize.height, totalH)});

        for (size_t i = 0; i < m_items.size(); ++i) {
            auto row = createRowForItem(m_items[i], listW, static_cast<int>(i));
            float y = content->getContentSize().height - 8.f - (float)i * rowH;
            row->setPosition({0, y - rowH});
            content->addChild(row);
        }

        m_scrollLayer->scrollToTop();
}

CCNode* VerificationCenterLayer::createRowForItem(const PendingItem& item, float width, int index) {
    auto row = CCNode::create();
    row->setContentSize({width, 42.f});
    row->setAnchorPoint({0, 0});

    // fondo alterno
    auto rowBg = paimon::SpriteHelper::createColorPanel(
        width - 4.f, 40.f,
        index % 2 == 0 ? ccColor3B{30, 30, 50} : ccColor3B{20, 20, 35}, 100);
    rowBg->setPosition({2.f, 21.f - 20.f});
    rowBg->setTag(1000 + index); // pa highlight
    row->addChild(rowBg, -1);

    // username
    std::string currentUsername;
    if (auto gm = GameManager::get()) currentUsername = gm->m_playerName;
    bool isClaimed = !item.claimedBy.empty();
    bool claimedByMe = isClaimed && (item.claimedBy == currentUsername);

    // etiqueta ID
    bool isUserReport = (m_current == PendingCategory::Report && item.type == "user");
    std::string idText;
    if (isUserReport) {
        idText = fmt::format("{} ({} report{})", item.reportedUsername,
            item.reports.size(), item.reports.size() == 1 ? "" : "s");
    } else if (m_current == PendingCategory::ProfileBackground || m_current == PendingCategory::ProfileImg) {
        idText = fmt::format("Account: {}", item.levelID);
    } else {
        idText = fmt::format("ID: {}", item.levelID);
    }
    auto idLbl = CCLabelBMFont::create(idText.c_str(), "goldFont.fnt");
    idLbl->setScale(0.38f);
    idLbl->setAnchorPoint({0, 0.5f});
    idLbl->setPosition({8.f, 26.f});
    if (isUserReport) idLbl->setColor({255, 120, 120});
    row->addChild(idLbl);

    // suggestion count badge
    if (item.suggestions.size() > 1) {
        auto countLbl = CCLabelBMFont::create(
            fmt::format("[{}]", item.suggestions.size()).c_str(), "bigFont.fnt");
        countLbl->setScale(0.28f);
        countLbl->setColor({255, 200, 50});
        countLbl->setAnchorPoint({0, 0.5f});
        countLbl->setPosition({idLbl->getPositionX() + idLbl->getContentSize().width * idLbl->getScale() + 4.f, 26.f});
        row->addChild(countLbl);
    }

    // submitter
    if (!item.submittedBy.empty()) {
        auto subLbl = CCLabelBMFont::create(
            fmt::format("by {}", item.submittedBy).c_str(), "chatFont.fnt");
        subLbl->setScale(0.35f);
        subLbl->setAnchorPoint({0, 0.5f});
        subLbl->setPosition({8.f, 12.f});
        subLbl->setColor({180, 180, 200});
        row->addChild(subLbl);
    }

    // claim status
    if (isClaimed) {
        std::string claimText = claimedByMe
            ? Localization::get().getString("queue.claimed_by_you")
            : fmt::format(fmt::runtime(Localization::get().getString("queue.claimed_by_user")), item.claimedBy);
        auto claimLbl = CCLabelBMFont::create(claimText.c_str(), "chatFont.fnt");
        claimLbl->setScale(0.3f);
        claimLbl->setColor(claimedByMe ? ccColor3B{100, 255, 100} : ccColor3B{255, 100, 100});
        claimLbl->setAnchorPoint({1, 0.5f});
        claimLbl->setPosition({width - 12.f, 12.f});
        row->addChild(claimLbl);
    }

    // menu botones derecha
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    btnMenu->setContentSize(row->getContentSize());

    bool canInteract = claimedByMe;

    auto setupBtn = [&](CCMenuItemSpriteExtra* btn, ButtonSprite* spr) {
        if (!canInteract) {
            btn->setEnabled(false);
            spr->setColor({100, 100, 100});
            spr->setOpacity(150);
        }
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    };

    float btnX = width - 16.f;
    float btnY = 21.f;
    float btnGap = 30.f;

    // rechazar / banear
    if (isUserReport) {
        auto spr = ButtonSprite::create("Ban", 32, true, "bigFont.fnt", "GJ_button_06.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onBanUser));
        btn->setTag(item.levelID);
        btn->setPosition({btnX, btnY});
        setupBtn(btn, spr);
        btnX -= btnGap;
    } else if (m_current == PendingCategory::Report) {
        auto spr = ButtonSprite::create("Del", 28, true, "bigFont.fnt", "GJ_button_06.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onReject));
        btn->setTag(item.levelID);
        btn->setUserObject(CCString::createWithFormat("%d", static_cast<int>(m_current)));
        btn->setPosition({btnX, btnY});
        setupBtn(btn, spr);
        btnX -= btnGap;
    } else {
        auto spr = ButtonSprite::create("X", 22, true, "bigFont.fnt", "GJ_button_06.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onReject));
        btn->setTag(item.levelID);
        btn->setUserObject(CCString::createWithFormat("%d", static_cast<int>(m_current)));
        btn->setPosition({btnX, btnY});
        setupBtn(btn, spr);
        btnX -= btnGap;
    }

    // aceptar (no en reportes)
    if (m_current != PendingCategory::Report) {
        auto spr = ButtonSprite::create("OK", 22, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onAccept));
        btn->setTag(item.levelID);
        setupBtn(btn, spr);
        btn->setPosition({btnX, btnY});
        btnX -= btnGap;
    }

    // ver reporte
    if (m_current == PendingCategory::Report) {
        auto spr = ButtonSprite::create("?", 22, true, "bigFont.fnt", "GJ_button_05.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onViewReport));
        btn->setTag(item.levelID);
        btn->setUserObject(CCString::createWithFormat("%s", item.note.c_str()));
        setupBtn(btn, spr);
        btn->setPosition({btnX, btnY});
        btnX -= btnGap;
    }

    // reclamar
    {
        char const* claimImg = claimedByMe ? "GJ_button_02.png" : "GJ_button_04.png";
        auto spr = ButtonSprite::create("C", 22, true, "bigFont.fnt", claimImg, 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onClaimLevel));
        btn->setTag(item.levelID);
        if (isClaimed && !claimedByMe) {
            btn->setEnabled(false);
            spr->setColor({100, 100, 100});
        }
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
        btn->setPosition({btnX, btnY});
        btnX -= btnGap;
    }

    row->addChild(btnMenu, 5);

    // click en la fila -> seleccionar y preview
    // usamos un boton invisible que cubre el area izquierda
    auto selectSpr = CCSprite::create();
    selectSpr->setContentSize({btnX - 4.f, 40.f}); // area a la izquierda de los botones
    selectSpr->setOpacity(0);
    auto selectBtn = CCMenuItemSpriteExtra::create(selectSpr, this,
        menu_selector(VerificationCenterLayer::onSelectItem));
    selectBtn->setTag(index);
    selectBtn->setAnchorPoint({0, 0.5f});
    selectBtn->setPosition({2.f, btnY});
    selectBtn->setContentSize({btnX - 4.f, 40.f});
    btnMenu->addChild(selectBtn, -1);

    return row;
}

void VerificationCenterLayer::highlightRow(int index) {
    if (!m_scrollLayer) return;
    auto content = m_scrollLayer->m_contentLayer;
    if (!content) return;

    for (auto* child : CCArrayExt<CCNode*>(content->getChildren())) {
        for (auto* sub : CCArrayExt<CCNode*>(child->getChildren())) {
            if (auto bg = typeinfo_cast<CCScale9Sprite*>(sub)) {
                int bgIndex = bg->getTag() - 1000;
                if (bgIndex >= 0) {
                    if (bgIndex == index) {
                        bg->setColor({60, 80, 140});
                        bg->setOpacity(160);
                    } else {
                        bg->setColor(bgIndex % 2 == 0 ? ccColor3B{30, 30, 50} : ccColor3B{20, 20, 35});
                        bg->setOpacity(100);
                    }
                }
            }
        }
    }
}

// ── seleccion y preview ──────────────────────────────────

void VerificationCenterLayer::onSelectItem(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    m_selectedIndex = index;
    m_currentSuggestionIndex = 0;
    highlightRow(index);
    showPreviewForItem(index);
}

void VerificationCenterLayer::clearPreview() {
    if (!m_previewPanel) return;
    // quitar sprite viejo
    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }
    if (m_previewAnimNode) {
        m_previewAnimNode->removeFromParent();
        m_previewAnimNode = nullptr;
    }
    if (m_previewSpinner) {
        m_previewSpinner->dismiss();
        m_previewSpinner = nullptr;
    }
    // hide navigation
    if (m_prevArrowBtn) m_prevArrowBtn->setVisible(false);
    if (m_nextArrowBtn) m_nextArrowBtn->setVisible(false);
    if (m_suggestionCountLabel) m_suggestionCountLabel->setVisible(false);
    // restaurar label
    if (m_previewLabel) {
        m_previewLabel->setVisible(true);
    }
}

void VerificationCenterLayer::setPreviewTexture(CCTexture2D* tex) {
    if (!tex || !m_previewPanel) return;

    clearPreview();
    if (m_previewLabel) m_previewLabel->setVisible(false);

    auto spr = CCSprite::createWithTexture(tex);
    if (!spr) return;

    auto panelSize = m_previewPanel->getContentSize();
    float maxW = panelSize.width - 16.f;
    float maxH = panelSize.height - 16.f;

    float scaleX = maxW / spr->getContentWidth();
    float scaleY = maxH / spr->getContentHeight();
    float scale = std::min(scaleX, scaleY);
    spr->setScale(scale);
    spr->setPosition(panelSize / 2);
    m_previewPanel->addChild(spr, 5);
    m_previewSprite = spr;
}

void VerificationCenterLayer::setPreviewSprite(CCSprite* spr) {
    if (!spr || !m_previewPanel) return;
    clearPreview();
    if (m_previewLabel) m_previewLabel->setVisible(false);

    auto panelSize = m_previewPanel->getContentSize();
    float maxW = panelSize.width - 16.f;
    float maxH = panelSize.height - 16.f;
    float scaleX = maxW / spr->getContentWidth();
    float scaleY = maxH / spr->getContentHeight();
    spr->setScale(std::min(scaleX, scaleY));
    spr->setPosition(panelSize / 2);
    m_previewPanel->addChild(spr, 5);
    m_previewSprite = spr;
}

void VerificationCenterLayer::showPreviewForItem(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;

    clearPreview();
    if (m_previewLabel) m_previewLabel->setVisible(false);

    auto panelSize = m_previewPanel->getContentSize();

    // spinner cargando
    m_previewSpinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    m_previewSpinner->showLocal(m_previewPanel, 10);

    int itemID = m_items[index].levelID;
    WeakRef<VerificationCenterLayer> self = this;
    int savedIndex = index;

    // callback que recibe bytes crudos — soporta GIF animado + cualquier otro formato
    auto onRawLoaded = [self, savedIndex](bool success, std::vector<uint8_t> const& data, int, int) {
        auto layer = self.lock();
        if (!layer) return;
        if (layer->m_selectedIndex != savedIndex) return;

        if (layer->m_previewSpinner) {
            layer->m_previewSpinner->dismiss();
            layer->m_previewSpinner = nullptr;
        }

        if (!success || data.empty()) {
            if (layer->m_previewLabel) {
                layer->m_previewLabel->setString("No preview");
                layer->m_previewLabel->setVisible(true);
            }
            return;
        }

        auto panelSize = layer->m_previewPanel->getContentSize();
        float maxW = panelSize.width - 16.f;
        float maxH = panelSize.height - 16.f;

        // GIF: usar AnimatedGIFSprite para preview animado
        if (ThumbnailTransportClient::isGIFData(data)) {
            auto* gifSpr = AnimatedGIFSprite::create(data.data(), data.size());
            if (gifSpr) {
                float scaleX = maxW / gifSpr->getContentWidth();
                float scaleY = maxH / gifSpr->getContentHeight();
                gifSpr->setScale(std::min(scaleX, scaleY));
                gifSpr->setPosition(panelSize / 2);
                layer->m_previewPanel->addChild(gifSpr, 5);
                layer->m_previewAnimNode = gifSpr;
                if (layer->m_previewLabel) layer->m_previewLabel->setVisible(false);
                return;
            }
            // fallback: decode first frame as static
        }

        // PNG/JPG/WebP (or GIF fallback): static texture
        auto* tex = ThumbnailTransportClient::bytesToTexture(data);
        if (tex) {
            layer->setPreviewTexture(tex);
        } else {
            if (layer->m_previewLabel) {
                layer->m_previewLabel->setString("No preview");
                layer->m_previewLabel->setVisible(true);
            }
        }
    };

    // helper for texture-based callbacks (wraps to raw callback via download)
    auto onLoaded = [self, savedIndex](bool success, CCTexture2D* tex) {
        auto layer = self.lock();
        if (!layer) return;
        if (layer->m_selectedIndex != savedIndex) return;

        if (layer->m_previewSpinner) {
            layer->m_previewSpinner->dismiss();
            layer->m_previewSpinner = nullptr;
        }

        if (success && tex) {
            layer->setPreviewTexture(tex);
        } else {
            if (layer->m_previewLabel) {
                layer->m_previewLabel->setString("No preview");
                layer->m_previewLabel->setVisible(true);
            }
        }
    };

    // descargar segun categoria — raw bytes cuando posible para GIF animado
    auto const& item = m_items[index];
    switch (m_current) {
    case PendingCategory::Verify: {
        int sugIdx = m_currentSuggestionIndex;
        if (sugIdx < 0 || sugIdx >= (int)item.suggestions.size()) sugIdx = 0;
        if (!item.suggestions.empty() && !item.suggestions[sugIdx].filename.empty()) {
            std::string url = HttpClient::get().getServerURL() + "/" + item.suggestions[sugIdx].filename;
            HttpClient::get().downloadFromUrl(url, onRawLoaded);
        } else {
            HttpClient::get().downloadSuggestion(itemID, onRawLoaded);
        }
        break;
    }
    case PendingCategory::ProfileBackground:
        if (!item.suggestions.empty() && !item.suggestions[0].filename.empty()) {
            std::string url = HttpClient::get().getServerURL() + "/" + item.suggestions[0].filename;
            HttpClient::get().downloadFromUrl(url, onRawLoaded);
        } else {
            std::string url = HttpClient::get().getServerURL()
                + "/pending_profilebackground/" + std::to_string(itemID) + "?self=1";
            HttpClient::get().downloadFromUrl(url, onRawLoaded);
        }
        break;
    case PendingCategory::Update:
        HttpClient::get().downloadUpdate(itemID, onRawLoaded);
        break;
    case PendingCategory::Report:
        ThumbnailAPI::get().downloadReported(itemID, onLoaded);
        break;
    case PendingCategory::ProfileImg: {
        std::string url = HttpClient::get().getServerURL()
            + "/profileimgs/" + std::to_string(itemID) + "?pending=1";
        HttpClient::get().downloadFromUrl(url, onRawLoaded);
        break;
    }
    default:
        break;
    }

    updateNavigationArrows();
}

// ── acciones ─────────────────────────────────────────────

void VerificationCenterLayer::onViewBans(CCObject*) {
    if (auto popup = BanListPopup::create()) popup->show();
}

void VerificationCenterLayer::onViewWhitelist(CCObject*) {
    if (auto popup = WhitelistPopup::create()) popup->show();
}

void VerificationCenterLayer::onOpenLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    auto& vctx = paimon::SessionState::get().verification;
    vctx.openFromThumbs = true;
    vctx.openFromReport = (m_current == PendingCategory::Report);
    vctx.openFromQueue  = true;
    vctx.queueCategory  = static_cast<int>(m_current);
    vctx.queueLevelID   = lvl;

    auto glm = GameLevelManager::get();
    GJGameLevel* level = nullptr;
    auto onlineLevels = glm->m_onlineLevels;
    if (onlineLevels) {
        level = static_cast<GJGameLevel*>(onlineLevels->objectForKey(std::to_string(lvl)));
    }

    if (level && !level->m_levelName.empty()) {
        TransitionManager::get().replaceScene(
            LevelInfoLayer::scene(level, false));
    } else {
        PaimonNotify::create("Downloading level...", NotificationIcon::Loading)->show();
        m_downloadCheckCount = 0;
        m_pendingLevelID = lvl;
        glm->downloadLevel(lvl, false, 0);
        this->schedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded), 0.1f);
    }
}

void VerificationCenterLayer::checkLevelDownloaded(float dt) {
    if (m_pendingLevelID <= 0) {
        this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
        return;
    }
    m_downloadCheckCount++;
    if (m_downloadCheckCount > 50) {
        PaimonNotify::create("Error: timed out downloading level", NotificationIcon::Error)->show();
        this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
        m_pendingLevelID = 0;
        return;
    }

    auto glm = GameLevelManager::get();
    if (!glm || !glm->m_onlineLevels) return;

    auto level = static_cast<GJGameLevel*>(glm->m_onlineLevels->objectForKey(std::to_string(m_pendingLevelID)));
    if (!level) return;

    std::string name = level->m_levelName;
    if (name.empty()) return;

    this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
    m_downloadCheckCount = 0;
    int lvl = m_pendingLevelID;
    m_pendingLevelID = 0;

    TransitionManager::get().replaceScene(
        LevelInfoLayer::scene(level, false));
}

void VerificationCenterLayer::onAccept(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    std::string targetFilename;
    // find the item matching this levelID
    for (auto const& it : m_items) {
        if (it.levelID == lvl) {
            int sugIdx = 0;
            // if this item is selected, use the current suggestion index
            if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_items.size()
                && m_items[m_selectedIndex].levelID == lvl) {
                sugIdx = m_currentSuggestionIndex;
            }
            if (sugIdx >= 0 && sugIdx < (int)it.suggestions.size()) {
                targetFilename = it.suggestions[sugIdx].filename;
            }
            break;
        }
    }

    std::string username;
    int accountID = 0;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    }
    if (accountID <= 0) {
        PaimonNotify::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
        return;
    }

    auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    spinner->showLocal(m_previewPanel, 100);
    Ref<PaimonLoadingOverlay> loading = spinner;

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, lvl, username, loading, cat, targetFilename](bool isMod, bool isAdmin) {
        auto layer = self.lock();
        if (!layer) return;

        if (!(isMod || isAdmin)) {
            if (loading) loading->dismiss();
            PaimonNotify::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        ThumbnailAPI::get().acceptQueueItem(lvl, cat, username, [self, lvl, loading, cat](bool success, std::string const& message) {
            auto layer = self.lock();
            if (loading) loading->dismiss();
            if (!layer) return;

            if (success) {
                PaimonNotify::create(Localization::get().getString("queue.accepted").c_str(), NotificationIcon::Success)->show();
                if (layer->getParent()) layer->switchTo(cat);
            } else {
                PaimonNotify::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            }
        }, targetFilename);
    });
}

void VerificationCenterLayer::onReject(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    std::string username;
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    // Determine if this is a user report
    std::string itemType;
    for (auto const& it : m_items) {
        if (it.levelID == lvl && it.type == "user") { itemType = "user"; break; }
    }

    auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    spinner->showLocal(m_previewPanel, 100);
    Ref<PaimonLoadingOverlay> loading = spinner;

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().checkModerator(username, [self, lvl, cat, username, loading, itemType](bool isMod, bool isAdmin) {
        auto layer = self.lock();
        if (!layer) return;

        if (!(isMod || isAdmin)) {
            if (loading) loading->dismiss();
            PaimonNotify::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        ThumbnailAPI::get().rejectQueueItem(lvl, cat, username, "Rechazado por moderador", [self, loading, cat](bool success, std::string const& message) {
            auto layer = self.lock();
            if (loading) loading->dismiss();
            if (!layer) return;

            if (success) {
                PaimonNotify::create(Localization::get().getString("queue.rejected").c_str(), NotificationIcon::Warning)->show();
                if (layer->getParent()) layer->switchTo(cat);
            } else {
                PaimonNotify::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            }
        }, itemType);
    });
}

void VerificationCenterLayer::onClaimLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    std::string username;
    int accountID = 0;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    }

    if (username.empty() || accountID <= 0) {
        PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
        return;
    }

    // Determine if this is a user report
    std::string itemType;
    for (auto const& it : m_items) {
        if (it.levelID == lvl && it.type == "user") { itemType = "user"; break; }
    }

    PaimonNotify::create(Localization::get().getString("queue.claiming").c_str(), NotificationIcon::Info)->show();

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, lvl, username, cat, itemType](bool isMod, bool isAdmin) {
        auto layer = self.lock();
        if (!layer) return;

        if (!(isMod || isAdmin)) {
            PaimonNotify::create(
                fmt::format(fmt::runtime(Localization::get().getString("queue.claim_error")), "Moderator auth required").c_str(),
                NotificationIcon::Error)->show();
            return;
        }

        ThumbnailAPI::get().claimQueueItem(lvl, cat, username, [self, lvl, username](bool success, std::string const& message) {
            auto layer = self.lock();
            if (!layer) return;

            if (success) {
                PaimonNotify::create(Localization::get().getString("queue.claimed").c_str(), NotificationIcon::Success)->show();
                for (auto& item : layer->m_items) {
                    if (item.levelID == lvl) {
                        item.claimedBy = username;
                        break;
                    }
                }
                if (layer->getParent()) layer->rebuildList();
            } else {
                PaimonNotify::create(
                    fmt::format(fmt::runtime(Localization::get().getString("queue.claim_error")), message).c_str(),
                    NotificationIcon::Error)->show();
            }
        }, itemType);
    });
}

void VerificationCenterLayer::onViewReport(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    // Find the item
    for (auto const& it : m_items) {
        if (it.levelID == lvl) {
            // User report with multiple entries
            if (it.type == "user" && !it.reports.empty()) {
                auto popup = UserReportsPopup::create(it.reportedUsername, it.reports);
                if (popup) popup->show();
                return;
            }
            // Regular level report
            std::string note = it.note;
            if (note.empty()) note = "No details provided";
            FLAlertLayer::create(
                Localization::get().getString("queue.report_reason").c_str(),
                note.c_str(),
                Localization::get().getString("general.close").c_str())->show();
            return;
        }
    }
}

void VerificationCenterLayer::onBanUser(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();

    // Find the reported username
    std::string reportedUsername;
    for (auto const& it : m_items) {
        if (it.levelID == accountID && it.type == "user") {
            reportedUsername = it.reportedUsername;
            break;
        }
    }
    if (reportedUsername.empty()) {
        PaimonNotify::create("User report not found", NotificationIcon::Error)->show();
        return;
    }

    // Confirmation popup
    WeakRef<VerificationCenterLayer> self = this;
    int capturedAccountID = accountID;
    std::string capturedUsername = reportedUsername;

    geode::createQuickPopup(
        "Ban User",
        fmt::format("Are you sure you want to <cr>ban</c> <cy>{}</c>?", reportedUsername),
        "Cancel", "Ban",
        [self, capturedAccountID, capturedUsername](auto*, bool confirm) {
            if (!confirm) return;
            auto layer = self.lock();
            if (!layer) return;

            std::string modUsername;
            if (auto gm = GameManager::get()) modUsername = gm->m_playerName;

            ThumbnailAPI::get().acceptQueueItem(
                capturedAccountID, PendingCategory::Report, modUsername,
                [self, capturedUsername](bool success, std::string const& message) {
                    auto layer = self.lock();
                    if (!layer) return;
                    if (success) {
                        PaimonNotify::create(
                            fmt::format("{} has been banned", capturedUsername).c_str(),
                            NotificationIcon::Success)->show();
                        if (layer->getParent()) layer->switchTo(PendingCategory::Report);
                    } else {
                        PaimonNotify::create(
                            fmt::format("Failed to ban: {}", message).c_str(),
                            NotificationIcon::Error)->show();
                    }
                },
                "", "user"
            );
        }
    );
}

void VerificationCenterLayer::onViewThumb(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    bool canAccept = (m_current == PendingCategory::Verify || m_current == PendingCategory::Update);

    paimon::SessionState::get().verification.fromReportPopup      = (m_current == PendingCategory::Report);
    paimon::SessionState::get().verification.verificationCategory  = static_cast<int>(m_current);

    std::vector<Suggestion> suggestions;
    for (auto const& item : m_items) {
        if (item.levelID == lvl) {
            suggestions = item.suggestions;
            break;
        }
    }

    auto pop = createThumbnailViewPopup(lvl, canAccept, suggestions);
    if (pop) {
        if (auto alertLayer = typeinfo_cast<FLAlertLayer*>(pop)) {
            alertLayer->show();
        }
    } else {
        PaimonNotify::create(Localization::get().getString("queue.cant_open").c_str(), NotificationIcon::Error)->show();
    }
}

void VerificationCenterLayer::onOpenProfile(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();
    ProfilePage::create(accountID, false)->show();
}

void VerificationCenterLayer::onViewProfileBackground(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();

    auto loading = PaimonNotify::create("Loading profile background...", NotificationIcon::Loading);
    loading->show();

    WeakRef<VerificationCenterLayer> self = this;
    ThumbnailAPI::get().downloadPendingProfile(accountID, [self, loading](bool success, CCTexture2D* texture) {
        loading->hide();
        auto layer = self.lock();
        if (!layer) return;
        if (success && texture) {
            layer->setPreviewTexture(texture);
        } else {
            PaimonNotify::create("Failed to load profile background", NotificationIcon::Error)->show();
        }
    });
}

// ── suggestion navigation ────────────────────────────────

void VerificationCenterLayer::onPreviewClick(CCObject*) {
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_items.size()) return;
    auto& item = m_items[m_selectedIndex];

    if (m_current == PendingCategory::ProfileBackground || m_current == PendingCategory::ProfileImg) {
        ProfilePage::create(item.levelID, false)->show();
    } else {
        auto dummy = CCNode::create();
        dummy->setTag(item.levelID);
        onOpenLevel(dummy);
    }
}

void VerificationCenterLayer::onPrevSuggestion(CCObject*) {
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_items.size()) return;
    if (m_currentSuggestionIndex > 0) {
        m_currentSuggestionIndex--;
        showPreviewForItem(m_selectedIndex);
    }
}

void VerificationCenterLayer::onNextSuggestion(CCObject*) {
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_items.size()) return;
    auto& item = m_items[m_selectedIndex];
    if (m_currentSuggestionIndex < (int)item.suggestions.size() - 1) {
        m_currentSuggestionIndex++;
        showPreviewForItem(m_selectedIndex);
    }
}

void VerificationCenterLayer::updateNavigationArrows() {
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_items.size()) {
        if (m_prevArrowBtn) m_prevArrowBtn->setVisible(false);
        if (m_nextArrowBtn) m_nextArrowBtn->setVisible(false);
        if (m_suggestionCountLabel) m_suggestionCountLabel->setVisible(false);
        return;
    }

    auto& item = m_items[m_selectedIndex];
    int count = (int)item.suggestions.size();

    if (count <= 1) {
        if (m_prevArrowBtn) m_prevArrowBtn->setVisible(false);
        if (m_nextArrowBtn) m_nextArrowBtn->setVisible(false);
        if (m_suggestionCountLabel) m_suggestionCountLabel->setVisible(false);
        return;
    }

    if (m_prevArrowBtn) m_prevArrowBtn->setVisible(m_currentSuggestionIndex > 0);
    if (m_nextArrowBtn) m_nextArrowBtn->setVisible(m_currentSuggestionIndex < count - 1);

    if (m_suggestionCountLabel) {
        auto& sug = item.suggestions[m_currentSuggestionIndex];
        std::string counterText = fmt::format("{}/{}", m_currentSuggestionIndex + 1, count);
        if (!sug.submittedBy.empty()) {
            counterText += " by " + sug.submittedBy;
        }
        m_suggestionCountLabel->setString(counterText.c_str());
        m_suggestionCountLabel->setVisible(true);
    }
}

void VerificationCenterLayer::loadCurrentSuggestionPreview() {
    if (m_selectedIndex >= 0) showPreviewForItem(m_selectedIndex);
}

// ── filter + auto-refresh ────────────────────────────────

void VerificationCenterLayer::onToggleFilter(CCObject* sender) {
    m_filterUnclaimed = !m_filterUnclaimed;

    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (btn) {
        auto spr = ButtonSprite::create(
            m_filterUnclaimed ? "Unclaimed" : "All",
            m_filterUnclaimed ? 80 : 55, true, "bigFont.fnt",
            m_filterUnclaimed ? "GJ_button_02.png" : "GJ_button_04.png",
            22.f, 0.5f);
        spr->setScale(0.6f);
        btn->setNormalImage(spr);
    }

    m_selectedIndex = -1;
    m_currentSuggestionIndex = 0;
    clearPreview();
    applyFilter();
    rebuildList();
}

void VerificationCenterLayer::applyFilter() {
    if (m_filterUnclaimed) {
        m_items.clear();
        for (auto const& item : m_allItems) {
            if (item.claimedBy.empty()) {
                m_items.push_back(item);
            }
        }
    } else {
        m_items = m_allItems;
    }
}

void VerificationCenterLayer::onRefresh(CCObject*) {
    if (m_refreshBtn) {
        m_refreshBtn->setEnabled(false);
        m_refreshBtn->setColor({128, 128, 128});
    }

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().syncVerificationQueue(cat, [self, cat](bool success, std::vector<PendingItem> const& items) {
        auto layer = self.lock();
        if (!layer || !layer->getParent()) return;

        if (layer->m_refreshBtn) {
            layer->m_refreshBtn->setEnabled(true);
            layer->m_refreshBtn->setColor({255, 255, 255});
        }

        if (!success || layer->m_current != cat) return;

        int selectedLevelID = -1;
        if (layer->m_selectedIndex >= 0 && layer->m_selectedIndex < (int)layer->m_items.size()) {
            selectedLevelID = layer->m_items[layer->m_selectedIndex].levelID;
        }

        layer->m_allItems = items;
        layer->applyFilter();
        layer->rebuildList();

        if (selectedLevelID > 0) {
            for (int i = 0; i < (int)layer->m_items.size(); i++) {
                if (layer->m_items[i].levelID == selectedLevelID) {
                    layer->m_selectedIndex = i;
                    layer->highlightRow(i);
                    break;
                }
            }
        }
    });
}

void VerificationCenterLayer::autoRefreshClaims(float dt) {
    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().syncVerificationQueue(cat, [self, cat](bool success, std::vector<PendingItem> const& items) {
        auto layer = self.lock();
        if (!layer || !layer->getParent() || layer->m_current != cat || !success) return;

        int selectedLevelID = -1;
        if (layer->m_selectedIndex >= 0 && layer->m_selectedIndex < (int)layer->m_items.size()) {
            selectedLevelID = layer->m_items[layer->m_selectedIndex].levelID;
        }

        layer->m_allItems = items;
        layer->applyFilter();
        layer->rebuildList();

        if (selectedLevelID > 0) {
            for (int i = 0; i < (int)layer->m_items.size(); i++) {
                if (layer->m_items[i].levelID == selectedLevelID) {
                    layer->m_selectedIndex = i;
                    layer->highlightRow(i);
                    break;
                }
            }
        }
    });
}
