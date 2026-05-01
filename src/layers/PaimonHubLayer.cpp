#include "PaimonHubLayer.hpp"
#include "PaiConfigLayer.hpp"
#include "PaimonSupportLayer.hpp"
#include "../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../features/backgrounds/ui/BackgroundConfigPopup.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/forum/services/ForumApi.hpp"
#include "../features/forum/ui/CreatePostPopup.hpp"
#include "../features/forum/ui/PostDetailPopup.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../features/updates/ui/UpdateProgressPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/Localization.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

namespace {
std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') {
        return fallback;
    }
    return value;
}
}

PaimonHubLayer* PaimonHubLayer::create() {
    auto ret = new PaimonHubLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaimonHubLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaimonHubLayer::create());
    return scene;
}

PaimonHubLayer::~PaimonHubLayer() {
    if (m_createPostOverlay && m_createPostOverlay->getParent()) {
        m_createPostOverlay->removeFromParent();
    }
    if (m_createTagOverlay && m_createTagOverlay->getParent()) {
        m_createTagOverlay->removeFromParent();
    }
    if (m_predefPickerOverlay && m_predefPickerOverlay->getParent()) {
        m_predefPickerOverlay->removeFromParent();
    }
}

bool PaimonHubLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    auto bg = CCLayerColor::create(ccc4(25, 25, 45, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -2);

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("paimon-hub-main-menu"_spr);
    m_mainMenu->setPosition({0, 0});
    this->addChild(m_mainMenu, 10);

    auto title = CCLabelBMFont::create(tr("pai.hub.title", "Paimbnails").c_str(), "goldFont.fnt");
    title->setPosition({cx, top - 20});
    title->setScale(0.7f);
    this->addChild(title);

    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaimonHubLayer::onBack));
    backBtn->setPosition({25, top - 20});
    m_mainMenu->addChild(backBtn);

    float tabY = top - 50.f;
    std::vector<std::string> tabNames = {
        tr("pai.hub.tab.home", "Home"),
        tr("pai.hub.tab.news", "News"),
        tr("pai.hub.tab.forum", "Forum")
    };

    auto tabBar = CCMenu::create();
    tabBar->setID("paimon-hub-tab-bar"_spr);
    tabBar->setPosition({cx, tabY});
    tabBar->setContentSize({240.f, 28.f});
    tabBar->setAnchorPoint({0.5f, 0.5f});
    tabBar->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    this->addChild(tabBar, 10);

    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i].c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonHubLayer::onTabSwitch));
        btn->setTag(i);
        tabBar->addChild(btn);
        m_tabBtns.push_back(btn);
    }
    tabBar->updateLayout();

    auto sep = CCLayerColor::create({255, 255, 255, 40});
    sep->setContentSize({winSize.width - 30, 1});
    sep->setPosition({15, tabY - 18.f});
    this->addChild(sep, 5);

    m_homeTab = CCLayer::create();
    m_homeTab->setID("home-tab"_spr);
    this->addChild(m_homeTab, 5);
    m_homeMenu = CCMenu::create();
    m_homeMenu->setID("home-menu"_spr);
    m_homeMenu->setPosition({0, 0});
    this->addChild(m_homeMenu, 11);

    m_newsTab = CCLayer::create();
    m_newsTab->setID("news-tab"_spr);
    m_newsTab->setVisible(false);
    this->addChild(m_newsTab, 5);
    m_newsMenu = CCMenu::create();
    m_newsMenu->setID("news-menu"_spr);
    m_newsMenu->setPosition({0, 0});
    m_newsMenu->setVisible(false);
    this->addChild(m_newsMenu, 11);

    m_forumTab = CCLayer::create();
    m_forumTab->setID("forum-tab"_spr);
    m_forumTab->setVisible(false);
    this->addChild(m_forumTab, 5);
    m_forumMenu = CCMenu::create();
    m_forumMenu->setID("forum-menu"_spr);
    m_forumMenu->setPosition({0, 0});
    m_forumMenu->setVisible(false);
    this->addChild(m_forumMenu, 11);

    m_forumTags = {
        "Guide", "Tip", "Question", "Bug", "Suggestion",
        "Showcase", "Discussion", "Help", "News", "Update",
        "Level", "Video", "Art", "Music", "Story",
        "Theory", "Challenge", "Competition", "Feedback", "Other"
    };

    buildHomeTab();
    buildNewsTab();
    buildForumTab();

    switchTab(0);

    return true;
}

void PaimonHubLayer::keyBackClicked() {
    // MenuLayer::scene(false) hace el setup completo (musica, fondos, hooks).
    // Antes usabamos CCScene::create() + MenuLayer::create() manualmente,
    // lo que dejaba la escena sin inicializar => pantalla negra al apretar Escape.
    CCDirector::sharedDirector()->replaceScene(MenuLayer::scene(false));
}

void PaimonHubLayer::onTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchTab(idx);
}

void PaimonHubLayer::switchTab(int idx) {
    m_currentTab = idx;

    m_homeTab->setVisible(idx == 0);
    m_homeMenu->setVisible(idx == 0);
    m_newsTab->setVisible(idx == 1);
    m_newsMenu->setVisible(idx == 1);
    m_forumTab->setVisible(idx == 2);
    m_forumMenu->setVisible(idx == 2);

    for (int i = 0; i < (int)m_tabBtns.size(); i++) {
        auto spr = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(i == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
    }

    if (idx == 2) {
        refreshForumPosts();
    }
}

void PaimonHubLayer::buildHomeTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentTop = winSize.height - 75.f;
    float contentBot = 45.f;
    float contentH = contentTop - contentBot;

    auto panel = paimon::SpriteHelper::createDarkPanel(winSize.width - 30, contentH, 55);
    panel->setPosition({cx - (winSize.width - 30) / 2, contentBot});
    m_homeTab->addChild(panel, 0);

    // ── header (logo + welcome + descripcion) ──
    float headerTop = contentTop - 30.f;

    // Logo del mod (Logo.png en raiz, bundled via mod.json)
    auto logoSpr = CCSprite::create("Logo.png"_spr);
    if (!logoSpr || logoSpr->isUsingFallback()) {
        // fallback al sprite anterior si Logo.png no esta disponible
        logoSpr = CCSprite::create("paim_Paimon.png"_spr);
    }
    if (logoSpr) {
        // escalar para que entre bien en el header (usa el ancho de la textura)
        auto sz = logoSpr->getContentSize();
        if (sz.height > 0) {
            float targetH = 50.f;
            logoSpr->setScale(targetH / sz.height);
        } else {
            logoSpr->setScale(0.32f);
        }
        logoSpr->setPosition({cx, headerTop});
        m_homeTab->addChild(logoSpr, 1);
    }

    auto welcomeLbl = CCLabelBMFont::create(
        tr("pai.hub.welcome", "Welcome to Paimbnails!").c_str(),
        "goldFont.fnt"
    );
    welcomeLbl->setScale(0.45f);
    welcomeLbl->setPosition({cx, headerTop - 30.f});
    m_homeTab->addChild(welcomeLbl, 1);

    auto descLbl = CCLabelBMFont::create(
        tr("pai.hub.description", "Customize your Geometry Dash experience").c_str(),
        "bigFont.fnt"
    );
    descLbl->setScale(0.28f);
    descLbl->setColor({180, 180, 180});
    descLbl->setPosition({cx, headerTop - 48.f});
    m_homeTab->addChild(descLbl, 1);

    // ── botones en columna usando ColumnLayout (estilo PaiConfigLayer) ──
    struct HomeBtn {
        char const* key;
        char const* fallback;
        char const* sprite;
        SEL_MenuHandler handler;
    };
    // "Extras" se removio porque iba al mismo PaiConfigLayer que "Settings"
    std::vector<HomeBtn> homeBtns = {
        {"pai.hub.btn.config",      "Settings",       "GJ_button_01.png", menu_selector(PaimonHubLayer::onOpenConfig)},
        {"pai.hub.btn.profiles",    "Profiles",       "GJ_button_02.png", menu_selector(PaimonHubLayer::onOpenProfiles)},
        {"pai.hub.btn.backgrounds", "Backgrounds",    "GJ_button_03.png", menu_selector(PaimonHubLayer::onOpenBackgrounds)},
        {"pai.hub.btn.support",     "Support",        "GJ_button_06.png", menu_selector(PaimonHubLayer::onOpenSupport)},
    };

    float btnAreaTop = headerTop - 65.f;
    float btnAreaH   = btnAreaTop - (contentBot + 10.f);

    auto btnMenu = CCMenu::create();
    btnMenu->setID("home-buttons"_spr);
    btnMenu->setContentSize({200.f, btnAreaH});
    btnMenu->setAnchorPoint({0.5f, 0.5f});
    btnMenu->setPosition({cx, btnAreaTop - btnAreaH / 2.f});
    btnMenu->setLayout(
        ColumnLayout::create()
            ->setGap(4.f)
            ->setAxisReverse(true)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisLineAlignment(AxisAlignment::Center)
    );
    m_homeMenu->addChild(btnMenu);

    for (auto const& b : homeBtns) {
        auto spr = ButtonSprite::create(
            tr(b.key, b.fallback).c_str(),
            "goldFont.fnt", b.sprite, .8f
        );
        spr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, b.handler);
        btnMenu->addChild(btn);
    }

    // ── Boton de actualizacion (debajo de los otros) ────────────────────────────
    {
        auto spr = ButtonSprite::create(
            tr("pai.hub.btn.update", "Check for update").c_str(),
            "goldFont.fnt", "GJ_button_01.png", .8f
        );
        spr->setScale(0.5f);
        m_updateBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonHubLayer::onCheckUpdate));
        btnMenu->addChild(m_updateBtn);
    }

    btnMenu->updateLayout();

    // ── Etiqueta de version actual (debajo de todo) ────────────────────────────────
    {
        auto& chk = paimon::updates::UpdateChecker::get();
        std::string verText = fmt::format(
            fmt::runtime(tr("pai.hub.version", "Version: {}")),
            chk.localVersion()
        );
        m_versionLabel = CCLabelBMFont::create(verText.c_str(), "bigFont.fnt");
        m_versionLabel->setScale(0.3f);
        m_versionLabel->setColor({180, 180, 180});
        // Posicion: debajo del menu, centrado
        float verY = btnAreaTop - btnAreaH - 18.f;
        m_versionLabel->setPosition({cx, verY});
        m_homeTab->addChild(m_versionLabel, 2);
    }

    // Pintar el boton de update si corresponde
    this->refreshUpdateButton();
}

void PaimonHubLayer::buildNewsTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentTop = winSize.height - 75.f;
    float contentBot = 45.f;
    float contentH = contentTop - contentBot;

    auto panel = paimon::SpriteHelper::createDarkPanel(winSize.width - 30, contentH, 55);
    panel->setPosition({cx - (winSize.width - 30) / 2, contentBot});
    m_newsTab->addChild(panel, 0);

    float headerY = contentTop - 18.f;

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.news.title", "Latest News").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.45f);
    titleLbl->setPosition({cx, headerY});
    m_newsTab->addChild(titleLbl, 1);

    auto refreshSpr = ButtonSprite::create(
        tr("pai.hub.news.refresh", "Refresh").c_str(),
        "bigFont.fnt", "GJ_button_06.png", .8f
    );
    refreshSpr->setScale(0.4f);
    auto refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, this, menu_selector(PaimonHubLayer::onRefreshNews));
    refreshBtn->setPosition({winSize.width - 40.f, headerY});
    m_newsMenu->addChild(refreshBtn);

    std::vector<std::pair<std::string, std::string>> newsItems = {
        {tr("pai.hub.news.item1.title", "Welcome to Paimon Hub!"),
         tr("pai.hub.news.item1.desc", "Check out our new hub with news and forum sections.")},
        {tr("pai.hub.news.item2.title", "Version 1.0.1 Released"),
         tr("pai.hub.news.item2.desc", "New features and bug fixes are now available.")},
        {tr("pai.hub.news.item3.title", "Custom Profiles"),
         tr("pai.hub.news.item3.desc", "Create and share your custom profile pictures.")},
    };

    float itemW = winSize.width - 60.f;
    float itemH = 55.f;
    float itemGap = 8.f;
    float listTop = headerY - 28.f;

    for (size_t i = 0; i < newsItems.size(); i++) {
        float itemBottomY = listTop - (i + 1) * itemH - i * itemGap;

        auto itemPanel = paimon::SpriteHelper::createDarkPanel(itemW, itemH, 70);
        itemPanel->setPosition({cx - itemW / 2, itemBottomY});
        m_newsTab->addChild(itemPanel, 0);

        // hijos del panel en coords locales para que esten alineados al panel
        auto itemTitle = CCLabelBMFont::create(newsItems[i].first.c_str(), "goldFont.fnt");
        itemTitle->setScale(0.32f);
        itemTitle->setAnchorPoint({0.f, 0.5f});
        itemTitle->setPosition({12.f, itemH - 16.f});
        itemPanel->addChild(itemTitle, 1);

        auto itemDesc = CCLabelBMFont::create(newsItems[i].second.c_str(), "bigFont.fnt");
        itemDesc->setScale(0.22f);
        itemDesc->setColor({160, 160, 160});
        itemDesc->setAnchorPoint({0.f, 0.5f});
        itemDesc->setPosition({12.f, 14.f});
        itemPanel->addChild(itemDesc, 1);

        auto newBadge = CCLabelBMFont::create("NEW", "bigFont.fnt");
        newBadge->setScale(0.22f);
        newBadge->setColor({100, 255, 100});
        newBadge->setAnchorPoint({1.f, 0.5f});
        newBadge->setPosition({itemW - 10.f, itemH - 16.f});
        itemPanel->addChild(newBadge, 1);
    }
}

void PaimonHubLayer::buildForumTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentTop = winSize.height - 75.f;
    float contentBot = 45.f;
    float contentH = contentTop - contentBot;

    auto panel = paimon::SpriteHelper::createDarkPanel(winSize.width - 30, contentH, 55);
    panel->setPosition({cx - (winSize.width - 30) / 2, contentBot});
    m_forumTab->addChild(panel, 0);

    float headerY = contentTop - 12.f;

    // ── sub-tab bar: Browse | Create ──
    auto subBar = CCMenu::create();
    subBar->setID("forum-subtabs"_spr);
    subBar->setPosition({winSize.width - 88.f, headerY});
    subBar->setContentSize({160.f, 28.f});
    subBar->setAnchorPoint({0.5f, 0.5f});
    subBar->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_forumMenu->addChild(subBar, 12);

    std::vector<std::pair<const char*, const char*>> subNames = {
        {"pai.hub.forum.browse", "Browse"},
        {"pai.hub.forum.create", "Create"},
    };
    m_forumSubTabBtns.clear();
    for (size_t i = 0; i < subNames.size(); i++) {
        auto spr = ButtonSprite::create(
            tr(subNames[i].first, subNames[i].second).c_str(),
            "bigFont.fnt", "GJ_button_04.png", .8f
        );
        spr->setScale(0.32f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(PaimonHubLayer::onForumSubTabSwitch)
        );
        btn->setTag(static_cast<int>(i));
        subBar->addChild(btn);
        m_forumSubTabBtns.push_back(btn);
    }
    subBar->updateLayout();

    // ── Browse section container (hidden when Create is active) ──
    m_forumBrowseNode = CCNode::create();
    m_forumBrowseNode->setPosition({0, 0});
    m_forumBrowseNode->setContentSize(winSize);
    m_forumTab->addChild(m_forumBrowseNode, 1);

    auto browseMenu = CCMenu::create();
    browseMenu->setPosition({0, 0});
    browseMenu->setContentSize(winSize);
    browseMenu->setID("forum-browse-menu"_spr);
    m_forumBrowseNode->addChild(browseMenu, 1);

    // ── Toolbar combinada: Tags (Izquierda) + Sort (Derecha) ──
    float ctrlRowY = headerY - 18.f;

    // --- Tags ---
    auto tagTitle = CCLabelBMFont::create(
        tr("pai.hub.forum.tags", "Tags:").c_str(),
        "bigFont.fnt"
    );
    tagTitle->setScale(0.3f);
    tagTitle->setAnchorPoint({0.f, 0.5f});
    tagTitle->setPosition({22.f, ctrlRowY});
    m_forumBrowseNode->addChild(tagTitle, 1);

    auto pickPredefSpr = ButtonSprite::create(
        tr("pai.hub.forum.predef", "Predef").c_str(),
        "bigFont.fnt", "GJ_button_05.png", .8f
    );
    pickPredefSpr->setScale(0.32f);
    auto pickPredefBtn = CCMenuItemSpriteExtra::create(
        pickPredefSpr, this, menu_selector(PaimonHubLayer::onOpenPredefPicker)
    );
    pickPredefBtn->setPosition({85.f, ctrlRowY});
    browseMenu->addChild(pickPredefBtn);

    auto createTagSpr = ButtonSprite::create(
        "+", "bigFont.fnt", "GJ_button_06.png", .8f
    );
    createTagSpr->setScale(0.4f);
    auto createTagBtn = CCMenuItemSpriteExtra::create(createTagSpr, this, menu_selector(PaimonHubLayer::onCreateTag));
    createTagBtn->setPosition({125.f, ctrlRowY});
    browseMenu->addChild(createTagBtn);

    // --- Sort ---
    auto sortLabel = CCLabelBMFont::create(
        tr("pai.hub.forum.sort", "Sort:").c_str(),
        "bigFont.fnt"
    );
    sortLabel->setScale(0.3f);
    sortLabel->setAnchorPoint({1.f, 0.5f});
    sortLabel->setPosition({winSize.width - 180.f, ctrlRowY});
    m_forumBrowseNode->addChild(sortLabel, 1);

    auto sortMenu = CCMenu::create();
    sortMenu->setID("forum-sort"_spr);
    sortMenu->setContentSize({160.f, 22.f});
    sortMenu->setAnchorPoint({0.f, 0.5f});
    sortMenu->setPosition({winSize.width - 175.f, ctrlRowY});
    sortMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Start)
    );
    m_forumBrowseNode->addChild(sortMenu, 1);

    std::vector<std::pair<const char*, const char*>> sortOptions = {
        {"pai.hub.forum.sort.recent", "Recent"},
        {"pai.hub.forum.sort.top",    "Top Rated"},
        {"pai.hub.forum.sort.liked",  "Most Liked"},
    };
    m_sortBtns.clear();
    for (size_t i = 0; i < sortOptions.size(); i++) {
        auto spr = ButtonSprite::create(
            tr(sortOptions[i].first, sortOptions[i].second).c_str(),
            "bigFont.fnt", "GJ_button_04.png", .8f
        );
        spr->setScale(0.3f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonHubLayer::onSortChanged));
        btn->setTag(static_cast<int>(i));
        sortMenu->addChild(btn);
        m_sortBtns.push_back(btn);
    }
    sortMenu->updateLayout();

    // resaltar sort activo
    for (size_t i = 0; i < m_sortBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_sortBtns[i]->getNormalImage())) {
            spr->setColor(static_cast<int>(m_sortMode) == (int)i
                ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }

    // ── area de tags seleccionados (mas compacta) ──
    float tagAreaW = winSize.width - 50.f;
    float tagAreaH = 24.f;
    float tagAreaY = ctrlRowY - 14.f - tagAreaH / 2.f;

    m_tagMenu = CCMenu::create();
    m_tagMenu->setID("forum-tags"_spr);
    m_tagMenu->setContentSize({tagAreaW, tagAreaH});
    m_tagMenu->setAnchorPoint({0.5f, 0.5f});
    m_tagMenu->setPosition({cx, tagAreaY});
    m_tagMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_forumBrowseNode->addChild(m_tagMenu, 1);

    // hint cuando no hay tags activos
    m_emptyTagsHint = CCLabelBMFont::create(
        tr("pai.hub.forum.tags.empty", "Tap [Predef] or [+] to add tags").c_str(),
        "bigFont.fnt"
    );
    m_emptyTagsHint->setScale(0.28f);
    m_emptyTagsHint->setColor({140, 140, 160});
    m_emptyTagsHint->setPosition({cx, tagAreaY});
    m_forumBrowseNode->addChild(m_emptyTagsHint, 1);

    refreshTagButtons();

    // ── lista de posts ──
    float listTop = tagAreaY - tagAreaH / 2.f - 8.f;

    m_noPostsLabel = CCLabelBMFont::create(
        tr("pai.hub.forum.no_posts", "No posts yet. Be the first to create one!").c_str(),
        "bigFont.fnt"
    );
    m_noPostsLabel->setScale(0.32f);
    m_noPostsLabel->setColor({150, 150, 150});
    m_noPostsLabel->setPosition({cx, (listTop + contentBot) / 2.f});
    m_forumBrowseNode->addChild(m_noPostsLabel, 1);

    m_forumPostList = CCNode::create();
    m_forumPostList->setPosition({cx, listTop});
    m_forumPostList->setContentSize({winSize.width - 60.f, listTop - contentBot - 8.f});
    m_forumBrowseNode->addChild(m_forumPostList, 2);

    refreshForumPosts();

    // ── Inline Create Post form (shown when Create sub-tab is active) ──
    m_forumCreateNode = CCNode::create();
    m_forumCreateNode->setPosition({0, 0});
    m_forumCreateNode->setContentSize(winSize);
    m_forumCreateNode->setVisible(false);
    m_forumTab->addChild(m_forumCreateNode, 10);

    // fondo oscuro para tapar la seccion Browse debajo
    auto createBg = paimon::SpriteHelper::createDarkPanel(winSize.width - 30, contentH, 65);
    createBg->setPosition({cx - (winSize.width - 30) / 2, contentBot});
    m_forumCreateNode->addChild(createBg, 0);

    float createTop = contentTop - 24.f;
    float createCX = cx;

    auto createTitleLbl = CCLabelBMFont::create("Create Post", "goldFont.fnt");
    createTitleLbl->setScale(0.45f);
    createTitleLbl->setPosition({createCX, createTop});
    m_forumCreateNode->addChild(createTitleLbl, 1);

    // Title input
    auto lblTitle = CCLabelBMFont::create("Title", "bigFont.fnt");
    lblTitle->setScale(0.32f);
    lblTitle->setAnchorPoint({0.f, 0.5f});
    lblTitle->setPosition({30.f, createTop - 28.f});
    lblTitle->setColor({200, 200, 220});
    m_forumCreateNode->addChild(lblTitle, 1);

    m_createTitleInput = TextInput::create(winSize.width - 80.f, "Post title...", "chatFont.fnt");
    m_createTitleInput->setCommonFilter(CommonFilter::Any);
    m_createTitleInput->setMaxCharCount(80);
    m_createTitleInput->setPosition({createCX, createTop - 48.f});
    m_createTitleInput->setScale(0.85f);
    m_forumCreateNode->addChild(m_createTitleInput, 1);

    // Description input
    auto lblDesc = CCLabelBMFont::create("Description", "bigFont.fnt");
    lblDesc->setScale(0.32f);
    lblDesc->setAnchorPoint({0.f, 0.5f});
    lblDesc->setPosition({30.f, createTop - 76.f});
    lblDesc->setColor({200, 200, 220});
    m_forumCreateNode->addChild(lblDesc, 1);

    m_createDescInput = TextInput::create(winSize.width - 80.f, "Description...", "chatFont.fnt");
    m_createDescInput->setCommonFilter(CommonFilter::Any);
    m_createDescInput->setMaxCharCount(500);
    m_createDescInput->setPosition({createCX, createTop - 96.f});
    m_createDescInput->setScale(0.85f);
    m_forumCreateNode->addChild(m_createDescInput, 1);

    // Tags hint
    auto lblTags = CCLabelBMFont::create("Tags (tap to add)", "bigFont.fnt");
    lblTags->setScale(0.30f);
    lblTags->setAnchorPoint({0.f, 0.5f});
    lblTags->setPosition({30.f, createTop - 124.f});
    lblTags->setColor({200, 200, 220});
    m_forumCreateNode->addChild(lblTags, 1);

    m_createTagMenu = CCMenu::create();
    m_createTagMenu->setID("inline-create-tags"_spr);
    m_createTagMenu->setContentSize({winSize.width - 80.f, 80.f});
    m_createTagMenu->setAnchorPoint({0.5f, 1.f});
    m_createTagMenu->setPosition({createCX, createTop - 138.f});
    m_createTagMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_forumCreateNode->addChild(m_createTagMenu, 1);

    m_createTagsHint = CCLabelBMFont::create("Tap a predefined tag below to add it", "bigFont.fnt");
    m_createTagsHint->setScale(0.26f);
    m_createTagsHint->setColor({140, 140, 160});
    m_createTagsHint->setPosition({createCX, createTop - 156.f});
    m_forumCreateNode->addChild(m_createTagsHint, 1);

    // Predefined tag chips for inline form
    auto predefMenu = CCMenu::create();
    predefMenu->setContentSize({winSize.width - 60.f, 80.f});
    predefMenu->setAnchorPoint({0.5f, 1.f});
    predefMenu->setPosition({createCX, createTop - 164.f});
    predefMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_forumCreateNode->addChild(predefMenu, 1);

    for (size_t i = 0; i < m_forumTags.size(); i++) {
        auto chipSpr = ButtonSprite::create(
            m_forumTags[i].c_str(), "bigFont.fnt", "GJ_button_05.png", .8f
        );
        chipSpr->setScale(0.28f);
        auto chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonHubLayer::onCreateToggleTag)
        );
        chipBtn->setTag(static_cast<int>(i));
        predefMenu->addChild(chipBtn);
    }
    predefMenu->updateLayout();

    // Submit + Cancel buttons
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    btnMenu->setContentSize(winSize);
    m_forumCreateNode->addChild(btnMenu, 1);

    auto submitSpr = ButtonSprite::create("Post", "goldFont.fnt", "GJ_button_01.png", 0.9f);
    submitSpr->setScale(0.55f);
    auto submitBtn = CCMenuItemSpriteExtra::create(submitSpr, this, menu_selector(PaimonHubLayer::onCreateSubmit));
    submitBtn->setPosition({createCX - 50.f, contentBot + 22.f});
    btnMenu->addChild(submitBtn);

    auto cancelSpr = ButtonSprite::create("Cancel", "bigFont.fnt", "GJ_button_06.png", 0.8f);
    cancelSpr->setScale(0.45f);
    auto cancelBtn = CCMenuItemSpriteExtra::create(cancelSpr, this, menu_selector(PaimonHubLayer::onForumSubTabSwitch));
    cancelBtn->setTag(0); // switch back to Browse
    cancelBtn->setPosition({createCX + 50.f, contentBot + 22.f});
    btnMenu->addChild(cancelBtn);

    // highlight default sub-tab (Browse)
    for (size_t i = 0; i < m_forumSubTabBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_forumSubTabBtns[i]->getNormalImage())) {
            spr->setColor(i == 0 ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }
}

void PaimonHubLayer::onOpenConfig(CCObject*) {
    // pushScene directo (sin TransitionManager) para evitar wrapping doble
    // de transiciones que dejaba la pantalla negra al volver con popSceneWithTransition.
    auto scene = PaiConfigLayer::scene();
    if (scene) CCDirector::sharedDirector()->pushScene(scene);
}

void PaimonHubLayer::onOpenProfiles(CCObject*) {
    auto popup = ProfilePicEditorPopup::create();
    popup->show();
}

void PaimonHubLayer::onOpenBackgrounds(CCObject*) {
    auto popup = BackgroundConfigPopup::create();
    popup->show();
}

void PaimonHubLayer::onOpenExtras(CCObject*) {
    auto scene = PaiConfigLayer::scene();
    if (scene) CCDirector::sharedDirector()->pushScene(scene);
}

void PaimonHubLayer::onOpenSupport(CCObject*) {
    // PaimonSupportLayer::onBack hace popSceneWithTransition; pushScene directo deja
    // el Hub correctamente en el stack para que el pop lo restaure sin pantalla negra.
    auto scene = PaimonSupportLayer::scene();
    if (scene) CCDirector::sharedDirector()->pushScene(scene);
}

void PaimonHubLayer::onBack(CCObject*) {
    CCDirector::sharedDirector()->replaceScene(MenuLayer::scene(false));
}

void PaimonHubLayer::update(float dt) {
    CCLayer::update(dt);
    // Refresca el color del boton de update si cambio el estado async
    if (m_currentTab == 0 && m_updateBtn) {
        this->refreshUpdateButton();
    }
}

void PaimonHubLayer::onCheckUpdate(CCObject*) {
    auto& checker = paimon::updates::UpdateChecker::get();
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto flash = [&](char const* text, cocos2d::ccColor3B color) {
        auto msg = CCLabelBMFont::create(text, "bigFont.fnt");
        msg->setScale(0.45f);
        msg->setPosition({winSize.width / 2, winSize.height / 2});
        msg->setColor(color);
        this->addChild(msg, 100);
        msg->runAction(CCSequence::create(
            CCDelayTime::create(1.8f),
            CCRemoveSelf::create(),
            nullptr
        ));
    };

    switch (checker.state()) {
        case paimon::updates::UpdateChecker::State::Idle:
        case paimon::updates::UpdateChecker::State::Failed:
            checker.checkAsync();
            flash(tr("pai.update.checking", "Checking for updates...").c_str(), {100, 200, 255});
            return;

        case paimon::updates::UpdateChecker::State::Checking:
            flash(tr("pai.update.checking", "Checking for updates...").c_str(), {100, 200, 255});
            return;

        case paimon::updates::UpdateChecker::State::UpdateAvailable:
            if (checker.downloadUrl().empty()) {
                flash(tr("pai.update.failed", "Error: no download URL").c_str(), {255, 110, 110});
                return;
            }
            {
                auto popup = paimon::updates::UpdateProgressPopup::create();
                if (popup) popup->show();
            }
            return;

        case paimon::updates::UpdateChecker::State::UpToDate:
        default:
            flash(tr("pai.update.uptodate", "You're up to date!").c_str(), {120, 255, 120});
            return;
    }
}

void PaimonHubLayer::refreshUpdateButton() {
    if (!m_updateBtn) return;

    auto& checker = paimon::updates::UpdateChecker::get();
    bool hasUpdate = checker.hasUpdate();

    auto spr = typeinfo_cast<ButtonSprite*>(m_updateBtn->getNormalImage());
    if (!spr) return;

    if (hasUpdate) {
        // Celeste cuando hay actualizacion
        spr->setColor({100, 200, 255}); // cyan claro
    } else {
        // Blanco por defecto
        spr->setColor({255, 255, 255});
    }
}

void PaimonHubLayer::onRefreshNews(CCObject*) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto successLbl = CCLabelBMFont::create(
        tr("pai.hub.news.refreshed", "News refreshed!").c_str(),
        "bigFont.fnt"
    );
    successLbl->setScale(0.35f);
    successLbl->setPosition({winSize.width / 2, winSize.height / 2});
    successLbl->setColor({100, 255, 100});
    this->addChild(successLbl, 100);

    successLbl->runAction(CCSequence::create(
        CCDelayTime::create(1.5f),
        CCRemoveSelf::create(),
        nullptr
    ));
}

void PaimonHubLayer::onCreatePost(CCObject*) {
    // Compone lista de tags disponibles: predef + custom
    std::vector<std::string> available;
    for (auto const& t : m_forumTags)  available.push_back(t);
    for (auto const& t : m_customTags) {
        if (std::find(available.begin(), available.end(), t) == available.end())
            available.push_back(t);
    }

    auto popup = CreatePostPopup::create(
        std::move(available),
        [this](paimon::forum::Post const& p) {
            // sincroniza tags nuevos al hub
            for (auto const& t : p.tags) {
                bool inPredef = std::find(m_forumTags.begin(), m_forumTags.end(), t) != m_forumTags.end();
                bool inCustom = std::find(m_customTags.begin(), m_customTags.end(), t) != m_customTags.end();
                if (!inPredef && !inCustom) m_customTags.push_back(t);
            }
            refreshTagButtons();
            refreshForumPosts();
        }
    );
    if (popup) popup->show();
}

void PaimonHubLayer::onCloseCreatePost(CCObject*) {
    // legacy no-op; CreatePostPopup maneja su propio ciclo de vida
}

void PaimonHubLayer::onSubmitPost(CCObject*) {
    // legacy no-op; CreatePostPopup invoca ForumApi::createPost directamente
}

void PaimonHubLayer::onFilterByTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx >= 0 && idx < (int)m_visibleTags.size()) {
        std::string tag = m_visibleTags[idx];
        auto it = std::find(m_selectedTags.begin(), m_selectedTags.end(), tag);
        if (it != m_selectedTags.end()) {
            m_selectedTags.erase(it);
        } else {
            m_selectedTags.push_back(tag);
        }
    }
    refreshTagButtons();
    refreshForumPosts();
}

void PaimonHubLayer::onCreateTag(CCObject*) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto overlay = CCLayerColor::create(ccc4(0, 0, 0, 180));
    overlay->setContentSize(winSize);
    overlay->setPosition({0, 0});
    overlay->setID("create-tag-overlay"_spr);
    this->addChild(overlay, 50);
    m_createTagOverlay = overlay;

    auto panel = paimon::SpriteHelper::createDarkPanel(250, 120, 60);
    panel->setPosition({winSize.width / 2 - 125, winSize.height / 2 - 60});
    overlay->addChild(panel);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.create_tag", "Create Custom Tag").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.35f);
    titleLbl->setPosition({winSize.width / 2, winSize.height / 2 + 35});
    overlay->addChild(titleLbl, 1);

    m_newTagInput = TextInput::create(180, tr("pai.hub.forum.new_tag", "Tag name"));
    m_newTagInput->setPosition({winSize.width / 2 - 90, winSize.height / 2});
    m_newTagInput->setScale(0.6f);
    overlay->addChild(m_newTagInput, 51);

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    btnMenu->setContentSize(winSize);
    btnMenu->setID("create-tag-menu"_spr);
    overlay->addChild(btnMenu, 52);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.5f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonHubLayer::onCloseCreateTag));
    closeBtn->setPosition({winSize.width / 2 + 105, winSize.height / 2 + 45});
    btnMenu->addChild(closeBtn);

    auto createSpr = ButtonSprite::create(
        tr("pai.hub.forum.create", "Create").c_str(),
        "goldFont.fnt", "GJ_button_01.png", .8f
    );
    createSpr->setScale(0.4f);
    auto createBtn = CCMenuItemSpriteExtra::create(createSpr, this, menu_selector(PaimonHubLayer::onSubmitTag));
    createBtn->setPosition({winSize.width / 2, winSize.height / 2 - 35});
    btnMenu->addChild(createBtn);
}

void PaimonHubLayer::onCloseCreateTag(CCObject*) {
    if (m_createTagOverlay) {
        m_createTagOverlay->removeFromParent();
        m_createTagOverlay = nullptr;
        m_newTagInput = nullptr;
    }
}

void PaimonHubLayer::onSubmitTag(CCObject*) {
    std::string tag = m_newTagInput->getString();
    if (!tag.empty()) {
        bool exists = false;
        for (const auto& t : m_forumTags) {
            if (t == tag) {
                exists = true;
                break;
            }
        }
        for (const auto& t : m_customTags) {
            if (t == tag) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_customTags.push_back(tag);
            refreshTagButtons();
        }
    }
    if (m_createTagOverlay) {
        m_createTagOverlay->removeFromParent();
        m_createTagOverlay = nullptr;
        m_newTagInput = nullptr;
    }
}

void PaimonHubLayer::onViewPost(CCObject*) {
}

void PaimonHubLayer::showForumLoading() {
    if (m_forumLoadingSpinner) return;
    m_forumLoadingSpinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    m_forumLoadingSpinner->show(m_forumTab, 100);
}

void PaimonHubLayer::hideForumLoading() {
    if (m_forumLoadingSpinner) {
        m_forumLoadingSpinner->dismiss();
        m_forumLoadingSpinner = nullptr;
    }
}

void PaimonHubLayer::refreshForumPosts() {
    if (!m_forumPostList) return;
    m_forumPostList->removeAllChildren();
    showForumLoading();

    // Construye filtro desde estado actual del hub
    paimon::forum::ListFilter filter;
    switch (m_sortMode) {
        case SortMode::TopRated:  filter.sort = paimon::forum::SortMode::TopRated;  break;
        case SortMode::MostLiked: filter.sort = paimon::forum::SortMode::MostLiked; break;
        case SortMode::Recent:
        default:                  filter.sort = paimon::forum::SortMode::Recent;    break;
    }
    filter.tags = m_selectedTags;
    filter.limit = 50;

    WeakRef<PaimonHubLayer> self = this;
    paimon::forum::ForumApi::get().listPosts(filter,
        [self](paimon::forum::Result<std::vector<paimon::forum::Post>> res) {
            auto layer = self.lock();
            if (!layer || !layer->m_forumPostList) return;
            layer->hideForumLoading();
            layer->renderPosts(res.ok ? res.data : std::vector<paimon::forum::Post>{});
        });
}

void PaimonHubLayer::renderPosts(std::vector<paimon::forum::Post> const& posts) {
    if (!m_forumPostList) return;
    m_forumPostList->removeAllChildren();

    bool hasPosts = !posts.empty();
    if (m_noPostsLabel) m_noPostsLabel->setVisible(!hasPosts);
    if (!hasPosts) return;

    auto* gm = GameManager::get();

    float listW = m_forumPostList->getContentSize().width;
    float listH = m_forumPostList->getContentSize().height;
    if (listH <= 0.f) listH = 180.f;

    float postH = 72.f;
    float postGap = 9.f;
    float totalH = static_cast<float>(posts.size()) * (postH + postGap) + 6.f;
    if (totalH < listH) totalH = listH;

    // ScrollLayer dentro del nodo de lista
    auto scroll = ScrollLayer::create({listW, listH});
    scroll->setPosition({-listW / 2.f, -listH});
    scroll->setID("forum-scroll"_spr);
    m_forumPostList->addChild(scroll, 1);
    scroll->m_contentLayer->setContentSize({listW, totalH});

    auto cardsMenu = CCMenu::create();
    cardsMenu->setContentSize({listW, totalH});
    cardsMenu->setPosition({0, 0});
    cardsMenu->ignoreAnchorPointForPosition(false);
    cardsMenu->setAnchorPoint({0.f, 0.f});
    scroll->m_contentLayer->addChild(cardsMenu);

    float cardW = listW - 8.f;
    float cardX = 4.f;
    float iconSize = 24.f;          // tamano visual del icono
    float iconColX = cardX + 8.f;   // izquierda dentro del card
    float textColX = cardX + 8.f + iconSize + 12.f;

    float y = totalH;
    for (auto const& post : posts) {
        y -= postH;

        // ── fondo de card con bordes redondeados visibles ──
        auto bg = paimon::SpriteHelper::createRoundedRect(
            cardW, postH, 6.f,
            {0.10f, 0.11f, 0.18f, 0.92f},      // fill oscuro semi-transparente
            {0.45f, 0.55f, 0.85f, 0.65f},      // borde azulado
            1.f
        );
        if (bg) {
            bg->setPosition({cardX, y});
            scroll->m_contentLayer->addChild(bg, 0);
        }

        // ── icono autor (chico) ──
        int iconID = std::max(1, post.author.iconID);
        if (auto* player = SimplePlayer::create(iconID)) {
            if (post.author.iconType > 0) {
                player->updatePlayerFrame(iconID, static_cast<IconType>(post.author.iconType));
            }
            if (gm) {
                auto col1 = gm->colorForIdx(post.author.color1);
                auto col2 = gm->colorForIdx(post.author.color2);
                player->setColor(col1);
                player->setSecondColor(col2);
                if (post.author.glowEnabled) player->setGlowOutline(col2);
                else                          player->disableGlowOutline();
            }
            float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
            // SimplePlayer contentSize es poco confiable: incluye glow/hitbox/areas
            // vacias. Usamos un tamaño de referencia empirico (~30px base) y
            // solo confiamos en contentSize cuando cae en un rango razonable.
            float gdRefSize = 30.f;
            float scale = (maxDim > 10.f && maxDim < 80.f) ? (iconSize / maxDim) : (iconSize / gdRefSize);
            player->setScale(std::max(scale, 0.55f));
            player->setPosition({iconColX + iconSize / 2.f, y + postH - 18.f});
            scroll->m_contentLayer->addChild(player, 5);
        }

        // ── username (al lado del icono) ──
        auto nameLbl = CCLabelBMFont::create(
            post.author.username.empty() ? "Anonymous" : post.author.username.c_str(),
            "goldFont.fnt"
        );
        nameLbl->setScale(0.34f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({textColX, y + postH - 18.f});
        scroll->m_contentLayer->addChild(nameLbl);

        // ── fecha (derecha arriba) ──
        auto dateLbl = CCLabelBMFont::create(
            paimon::forum::formatRelativeTime(post.createdAt).c_str(),
            "chatFont.fnt"
        );
        dateLbl->setScale(0.42f);
        dateLbl->setColor({150, 150, 170});
        dateLbl->setAnchorPoint({1.f, 0.5f});
        dateLbl->setPosition({cardX + cardW - 10.f, y + postH - 18.f});
        scroll->m_contentLayer->addChild(dateLbl);

        // ── titulo ──
        auto titleLbl = CCLabelBMFont::create(
            post.title.empty() ? "(untitled)" : post.title.c_str(),
            "bigFont.fnt"
        );
        titleLbl->setScale(0.40f);
        titleLbl->setAnchorPoint({0.f, 0.5f});
        titleLbl->setPosition({cardX + 10.f, y + postH - 34.f});
        float titleMaxW = cardW - 20.f;
        if (titleLbl->getScaledContentSize().width > titleMaxW) {
            titleLbl->setScale(titleLbl->getScale() * titleMaxW / titleLbl->getScaledContentSize().width);
        }
        scroll->m_contentLayer->addChild(titleLbl);

        // ── preview de descripcion ──
        std::string preview = post.description;
        if (preview.size() > 100) preview = preview.substr(0, 97) + "...";
        if (!preview.empty()) {
            auto pre = CCLabelBMFont::create(preview.c_str(), "chatFont.fnt");
            pre->setScale(0.40f);
            pre->setColor({200, 200, 220});
            pre->setAnchorPoint({0.f, 0.5f});
            pre->setPosition({cardX + 10.f, y + postH - 50.f});
            float preMaxW = cardW - 20.f;
            if (pre->getScaledContentSize().width > preMaxW) {
                pre->setScale(pre->getScale() * preMaxW / pre->getScaledContentSize().width);
            }
            scroll->m_contentLayer->addChild(pre);
        }

        // ── tags (izquierda) + stats (derecha) ──
        float metaY = y + 11.f;
        float tagX = cardX + 10.f;
        for (auto const& t : post.tags) {
            auto chip = ButtonSprite::create(t.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.7f);
            chip->setScale(0.20f);
            chip->setAnchorPoint({0.f, 0.5f});
            chip->setPosition({tagX, metaY});
            scroll->m_contentLayer->addChild(chip);
            tagX += chip->getScaledContentSize().width + 4.f;
            if (tagX > cardX + cardW - 100.f) break;
        }

        auto stats = CCLabelBMFont::create(
            fmt::format("Likes {}  Replies {}", post.likes, post.replyCount).c_str(),
            "bigFont.fnt"
        );
        stats->setScale(0.28f);
        stats->setColor({200, 220, 255});
        stats->setAnchorPoint({1.f, 0.5f});
        stats->setPosition({cardX + cardW - 56.f, metaY});
        scroll->m_contentLayer->addChild(stats);

        // ── Reply button (opens post detail) ──
        WeakRef<PaimonHubLayer> selfRef = this;
        std::string postId = post.id;
        auto replySpr = ButtonSprite::create("Reply", "bigFont.fnt", "GJ_button_04.png", 0.7f);
        replySpr->setScale(0.28f);
        auto replyBtn = CCMenuItemExt::createSpriteExtra(replySpr, [selfRef, postId](CCMenuItemSpriteExtra*) {
            auto layer = selfRef.lock();
            if (!layer) return;
            paimon::forum::ForumApi::get().getPost(postId,
                [selfRef](paimon::forum::Result<paimon::forum::Post> res) {
                    auto layer = selfRef.lock();
                    if (!layer || !res.ok) return;
                    auto popup = PostDetailPopup::create(res.data, [selfRef]() {
                        auto layer = selfRef.lock();
                        if (layer) layer->refreshForumPosts();
                    });
                    if (popup) popup->show();
                });
        });
        replyBtn->setPosition({cardX + cardW - 26.f, metaY});
        cardsMenu->addChild(replyBtn);

        y -= postGap;
    }

    scroll->scrollToTop();
}

void PaimonHubLayer::refreshTagButtons() {
    if (!m_tagMenu) return;
    m_tagMenu->removeAllChildren();
    m_visibleTags.clear();

    for (auto const& t : m_activePredefTags) m_visibleTags.push_back(t);
    for (auto const& t : m_customTags)       m_visibleTags.push_back(t);

    if (m_emptyTagsHint) {
        m_emptyTagsHint->setVisible(m_visibleTags.empty());
    }

    for (size_t i = 0; i < m_visibleTags.size(); i++) {
        bool isSelected = std::find(m_selectedTags.begin(), m_selectedTags.end(),
            m_visibleTags[i]) != m_selectedTags.end();

        auto tagSpr = ButtonSprite::create(
            m_visibleTags[i].c_str(), "bigFont.fnt",
            isSelected ? "GJ_button_01.png" : "GJ_button_04.png", .8f
        );
        tagSpr->setScale(0.32f);
        auto tagBtn = CCMenuItemSpriteExtra::create(
            tagSpr, this, menu_selector(PaimonHubLayer::onFilterByTag)
        );
        tagBtn->setTag(static_cast<int>(i));
        m_tagMenu->addChild(tagBtn);
    }
    m_tagMenu->updateLayout();
}

void PaimonHubLayer::onSortChanged(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx > 2) return;
    m_sortMode = static_cast<SortMode>(idx);

    for (size_t i = 0; i < m_sortBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_sortBtns[i]->getNormalImage())) {
            spr->setColor(idx == (int)i
                ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }

    refreshForumPosts();
}

void PaimonHubLayer::onOpenPredefPicker(CCObject*) {
    if (m_predefPickerOverlay) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    m_predefPickerOverlay = CCLayerColor::create(ccc4(0, 0, 0, 180));
    static_cast<CCLayerColor*>(m_predefPickerOverlay)->setContentSize(winSize);
    m_predefPickerOverlay->setPosition({0, 0});
    m_predefPickerOverlay->setID("predef-picker-overlay"_spr);
    this->addChild(m_predefPickerOverlay, 50);

    float panelW = 380.f;
    float panelH = 220.f;
    float panelX = winSize.width / 2.f - panelW / 2.f;
    float panelY = winSize.height / 2.f - panelH / 2.f;

    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 60);
    panel->setPosition({panelX, panelY});
    m_predefPickerOverlay->addChild(panel);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.predef.title", "Pick Predefined Tags").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.4f);
    titleLbl->setPosition({winSize.width / 2.f, panelY + panelH - 20.f});
    m_predefPickerOverlay->addChild(titleLbl, 1);

    auto hintLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.predef.hint", "Tap to enable/disable").c_str(),
        "bigFont.fnt"
    );
    hintLbl->setScale(0.3f);
    hintLbl->setColor({170, 170, 190});
    hintLbl->setPosition({winSize.width / 2.f, panelY + panelH - 40.f});
    m_predefPickerOverlay->addChild(hintLbl, 1);

    // CCMenu para el boton de cerrar (los CCMenuItemSpriteExtra requieren CCMenu padre)
    auto closeMenu = CCMenu::create();
    closeMenu->setPosition({0, 0});
    closeMenu->setContentSize(winSize);
    closeMenu->setID("predef-picker-close"_spr);
    m_predefPickerOverlay->addChild(closeMenu, 52);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.55f);
    auto closeBtn = CCMenuItemSpriteExtra::create(
        closeSpr, this, menu_selector(PaimonHubLayer::onClosePredefPicker)
    );
    closeBtn->setPosition({panelX + panelW - 12.f, panelY + panelH - 12.f});
    closeMenu->addChild(closeBtn);

    // grid de chips toggleables
    auto chipsMenu = CCMenu::create();
    chipsMenu->setContentSize({panelW - 24.f, panelH - 80.f});
    chipsMenu->setAnchorPoint({0.5f, 0.5f});
    chipsMenu->setPosition({winSize.width / 2.f, panelY + (panelH - 60.f) / 2.f});
    chipsMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_predefPickerOverlay->addChild(chipsMenu, 51);

    for (size_t i = 0; i < m_forumTags.size(); i++) {
        bool isActive = std::find(m_activePredefTags.begin(), m_activePredefTags.end(),
            m_forumTags[i]) != m_activePredefTags.end();

        auto chipSpr = ButtonSprite::create(
            m_forumTags[i].c_str(), "bigFont.fnt",
            isActive ? "GJ_button_01.png" : "GJ_button_05.png", .8f
        );
        chipSpr->setScale(0.34f);
        auto chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonHubLayer::onTogglePredefTag)
        );
        chipBtn->setTag(static_cast<int>(i));
        chipsMenu->addChild(chipBtn);
    }
    chipsMenu->updateLayout();
}

void PaimonHubLayer::onClosePredefPicker(CCObject*) {
    if (m_predefPickerOverlay) {
        m_predefPickerOverlay->removeFromParent();
        m_predefPickerOverlay = nullptr;
    }
}

void PaimonHubLayer::onTogglePredefTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_forumTags.size()) return;

    std::string const& tag = m_forumTags[idx];
    auto it = std::find(m_activePredefTags.begin(), m_activePredefTags.end(), tag);
    if (it != m_activePredefTags.end()) {
        m_activePredefTags.erase(it);
        // si estaba seleccionado para filtrar, des-seleccionar
        auto sel = std::find(m_selectedTags.begin(), m_selectedTags.end(), tag);
        if (sel != m_selectedTags.end()) m_selectedTags.erase(sel);
    } else {
        m_activePredefTags.push_back(tag);
    }

    // actualizar visual del chip
    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            bool isActive = std::find(m_activePredefTags.begin(), m_activePredefTags.end(),
                tag) != m_activePredefTags.end();
            spr->updateBGImage(isActive ? "GJ_button_01.png" : "GJ_button_05.png");
        }
    }

    refreshTagButtons();
}

void PaimonHubLayer::onForumSubTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchForumSubTab(idx);
}

void PaimonHubLayer::switchForumSubTab(int idx) {
    m_forumSubTab = idx;

    for (size_t i = 0; i < m_forumSubTabBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_forumSubTabBtns[i]->getNormalImage())) {
            spr->setColor(i == (size_t)idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }

    if (m_forumBrowseNode) m_forumBrowseNode->setVisible(idx == 0);
    if (m_forumCreateNode) m_forumCreateNode->setVisible(idx == 1);

    if (idx == 0) {
        // volver a Browse: limpiar formulario inline
        if (m_createTitleInput) m_createTitleInput->setString("");
        if (m_createDescInput) m_createDescInput->setString("");
        m_createSelectedTags.clear();
        if (m_createTagMenu) m_createTagMenu->removeAllChildren();
        refreshForumPosts();
    }
}

void PaimonHubLayer::onCreateToggleTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_forumTags.size()) return;
    std::string const& tag = m_forumTags[idx];

    auto it = std::find(m_createSelectedTags.begin(), m_createSelectedTags.end(), tag);
    bool selected = (it != m_createSelectedTags.end());
    if (selected) {
        m_createSelectedTags.erase(it);
    } else {
        m_createSelectedTags.push_back(tag);
    }

    // actualizar visual del chip
    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(selected ? "GJ_button_05.png" : "GJ_button_01.png");
        }
    }

    if (m_createTagsHint) {
        if (m_createSelectedTags.empty()) {
            m_createTagsHint->setString("Tap a predefined tag below to add it");
        } else {
            std::string joined;
            for (size_t i = 0; i < m_createSelectedTags.size(); ++i) {
                if (i > 0) joined += ", ";
                joined += m_createSelectedTags[i];
            }
            m_createTagsHint->setString(("Selected: " + joined).c_str());
        }
    }
}

void PaimonHubLayer::onCreateSubmit(CCObject*) {
    std::string title = m_createTitleInput ? m_createTitleInput->getString() : "";
    std::string desc  = m_createDescInput  ? m_createDescInput->getString()  : "";

    if (title.empty()) {
        PaimonNotify::create("Please enter a title", NotificationIcon::Warning)->show();
        return;
    }

    paimon::forum::CreatePostRequest req;
    req.title = title;
    req.description = desc;
    req.tags = m_createSelectedTags;

    WeakRef<PaimonHubLayer> self = this;
    paimon::forum::ForumApi::get().createPost(req, [self](paimon::forum::Result<paimon::forum::Post> res) {
        auto layer = self.lock();
        if (!layer) return;
        if (!res.ok) {
            PaimonNotify::create(("Failed: " + res.error).c_str(), NotificationIcon::Error)->show();
            return;
        }
        PaimonNotify::create("Post published!", NotificationIcon::Success)->show();
        layer->switchForumSubTab(0);
    });
}

CCMenuItemSpriteExtra* PaimonHubLayer::makeBtn(char const* text, CCPoint pos,
    SEL_MenuHandler handler, CCNode* parent, float scale) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}
