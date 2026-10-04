#include "EmotePickerPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../services/EmoteService.hpp"
#include "../services/EmoteCache.hpp"
#include "../EmoteRenderer.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::emotes {

static constexpr float POPUP_W_MIN = 320.f;
static constexpr float POPUP_H_MIN = 200.f;
static constexpr float POPUP_W_DEF = 400.f;
static constexpr float POPUP_H_DEF = 240.f;
static constexpr float POPUP_W_LARGE = 500.f;
static constexpr float POPUP_H_LARGE = 300.f;

static constexpr float PAD        = 7.f;
static constexpr float TITLE_H    = 24.f;
static constexpr float TOOLBAR_H  = 26.f;
static constexpr float TABSTRIP_H = 22.f;
static constexpr float PREVIEW_H  = 24.f;
static constexpr float CELL_GAP   = 4.f;
static constexpr float CELL_MIN   = 26.f;
static constexpr float CELL_MAX   = 56.f;
static constexpr float RESIZE_SZ  = 22.f;

static constexpr int kDimActionTag  = 8801;
static constexpr int kBodyActionTag = 8802;

static constexpr float ANIM_IN_DUR    = 0.34f;
static constexpr float ANIM_IN_SCALE  = 0.80f;
static constexpr float ANIM_DIM_IN    = 0.24f;
static constexpr float ANIM_OUT_DUR   = 0.17f;
static constexpr float ANIM_OUT_SCALE = 0.65f;
static constexpr float ANIM_DIM_OUT   = 0.16f;

static constexpr auto KEY_POS_X   = "emote-picker-x";
static constexpr auto KEY_POS_Y   = "emote-picker-y";
static constexpr auto KEY_W       = "emote-picker-w";
static constexpr auto KEY_H       = "emote-picker-h";
static constexpr auto KEY_CELL    = "emote-picker-cell";
static constexpr auto KEY_KEEP    = "emote-picker-keep-open";
static constexpr auto KEY_FAVS    = "emote-picker-favorites";
static constexpr auto KEY_RECENTS = "emote-picker-recents";

static constexpr size_t MAX_RECENTS = 24;

static std::vector<std::string> splitList(std::string const& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, '\n')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static std::string joinList(std::vector<std::string> const& v) {
    std::string out;
    for (auto const& s : v) { out += s; out += '\n'; }
    return out;
}

EmotePickerPopup* EmotePickerPopup::create(
        CopyableFunction<std::string()> getText,
        CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit,
        LayoutSize size) {
    auto ret = new EmotePickerPopup();
    if (ret && ret->init(std::move(getText), std::move(onTextChanged), charLimit, size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EmotePickerPopup::init(
        CopyableFunction<std::string()> getText,
        CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit,
        LayoutSize size) {
    m_getText = std::move(getText);
    m_onTextChanged = std::move(onTextChanged);
    m_charLimit = charLimit;
    m_layoutSize = size;

    loadLayout();

    if (!PaimonPopup::init(m_popupW, m_popupH))
        return false;
    paimon::unmarkDynamicPopup(this);

    auto& emoteService = EmoteService::get();
    if (!emoteService.isLoaded()) emoteService.loadCatalogFromDisk();

    loadFavoritesAndRecents();

    if (m_closeBtn) m_closeBtn->setVisible(false);
    if (m_bgSprite) m_bgSprite->setVisible(false);

    this->setKeypadEnabled(true);

    buildChrome();
    buildBody();
    relayout();
    m_lastGridW = m_popupW;
    m_lastGridH = m_popupH;

    switchView(View::All);

    m_dimOpacity = this->getOpacity();
    this->scheduleUpdate();

    if (!emoteService.isLoaded()) {
        WeakRef<EmotePickerPopup> self = this;
        emoteService.fetchAllEmotes([self](bool success) {
            if (!success || paimon::isRuntimeShuttingDown()) return;
            Loader::get()->queueInMainThread([self]() {
                if (auto* popup = self.lock().data(); popup && popup->getParent()) {
                    popup->rebuildTabStrip();
                    popup->refreshGrid();
                }
            });
        });
    }

    return true;
}

void EmotePickerPopup::applyWindowBg() {
    if (auto old = m_mainLayer->getChildByID("paimon-emote-border"_spr)) old->removeFromParent();
    if (auto old = m_mainLayer->getChildByID("paimon-emote-bg"_spr)) old->removeFromParent();
    if (m_titleBar) { m_titleBar->removeFromParent(); m_titleBar = nullptr; }

    auto border = paimon::ui::makeInset({m_popupW + 2.f, m_popupH + 2.f}, 255, {26, 22, 16});
    border->setID("paimon-emote-border"_spr);
    border->setPosition({-1.f, -1.f});
    m_mainLayer->addChild(border, -3);

    auto bg = paimon::ui::makeInset({m_popupW, m_popupH}, 250, {18, 16, 12});
    bg->setID("paimon-emote-bg"_spr);
    bg->setPosition({0.f, 0.f});
    m_mainLayer->addChild(bg, -2);

    // gold title bar; its node bounds are the draggable region.
    m_titleBar = CCNode::create();
    m_titleBar->setID("paimon-emote-titlebar"_spr);
    m_titleBar->setContentSize({m_popupW, TITLE_H});
    m_titleBar->setPosition({0.f, m_popupH - TITLE_H});
    m_mainLayer->addChild(m_titleBar, -1);

    auto titleInset = paimon::ui::makeInset({m_popupW - 4.f, TITLE_H - 2.f}, 220, {58, 44, 18});
    titleInset->setPosition({2.f, 1.f});
    m_titleBar->addChild(titleInset, 0);

    auto grip = CCLabelBMFont::create(":::", "goldFont.fnt");
    grip->setScale(0.3f);
    grip->setAnchorPoint({0.f, 0.5f});
    grip->setOpacity(140);
    grip->setPosition({8.f, TITLE_H / 2.f});
    m_titleBar->addChild(grip, 1);

    auto title = CCLabelBMFont::create("Emotes", "goldFont.fnt");
    title->setScale(0.52f);
    title->setPosition({m_popupW / 2.f, TITLE_H / 2.f});
    m_titleBar->addChild(title, 1);

    std::string cornerBase = "paimon-corner"_spr;
    if (auto c = m_mainLayer->getChildByID(cornerBase + "-bottom-left")) c->removeFromParent();
    if (auto c = m_mainLayer->getChildByID(cornerBase + "-bottom-right")) c->removeFromParent();
    paimon::ui::addCorners(m_mainLayer, {m_popupW, m_popupH}, geode::SideArtStyle::PopupGold, 0.32f, false);
}

void EmotePickerPopup::buildChrome() {
    if (auto old = m_mainLayer->getChildByID("paimon-emote-chrome-menu"_spr)) old->removeFromParent();
    if (m_resizeHandle) { m_resizeHandle->removeFromParent(); m_resizeHandle = nullptr; }
    m_refreshBtn = nullptr;

    applyWindowBg();

    auto chromeMenu = CCMenu::create();
    chromeMenu->setID("paimon-emote-chrome-menu"_spr);
    chromeMenu->setPosition({0.f, 0.f});
    chromeMenu->setContentSize({m_popupW, m_popupH});
    chromeMenu->ignoreAnchorPointForPosition(true);
    m_mainLayer->addChild(chromeMenu, 20);

    auto closeSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_closeBtn_001.png");
    if (closeSpr) {
        closeSpr->setScale(0.42f);
        auto closeBtn = CCMenuItemSpriteExtra::create(
            closeSpr, this, menu_selector(EmotePickerPopup::onClose));
        closeBtn->setID("paimon-emote-close"_spr);
        closeBtn->setPosition({12.f, m_popupH - TITLE_H / 2.f});
        chromeMenu->addChild(closeBtn);
    }

    auto resetSpr = paimon::SpriteHelper::safeCreateWithFrameName("edit_ccwBtn_001.png");
    if (resetSpr) {
        resetSpr->setScale(0.46f);
        auto resetBtn = CCMenuItemSpriteExtra::create(
            resetSpr, this, menu_selector(EmotePickerPopup::onResetLayout));
        resetBtn->setID("paimon-emote-reset"_spr);
        resetBtn->setPosition({m_popupW - 32.f, m_popupH - TITLE_H / 2.f});
        chromeMenu->addChild(resetBtn);
    }

    auto refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
    if (!refreshSpr) refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png");
    if (refreshSpr) {
        refreshSpr->setScale(0.4f);
        m_refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSpr, this, menu_selector(EmotePickerPopup::onRefreshCatalog));
        m_refreshBtn->setID("paimon-emote-refresh"_spr);
        m_refreshBtn->setPosition({m_popupW - 14.f, m_popupH - TITLE_H / 2.f});
        chromeMenu->addChild(m_refreshBtn);
        updateRefreshButtonState();
    }

    // bottom-right grabber for resize; menu holds it for touch priority.
    auto handleSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (!handleSpr) handleSpr = paimon::SpriteHelper::safeCreateWithFrameName("edit_rightBtn_001.png");
    if (handleSpr) {
        handleSpr->setScale(0.4f);
        handleSpr->setOpacity(170);
        handleSpr->setRotation(90.f);
    }
    m_resizeHandle = CCNode::create();
    m_resizeHandle->setID("paimon-emote-resize"_spr);
    m_resizeHandle->setContentSize({RESIZE_SZ, RESIZE_SZ});
    m_resizeHandle->setAnchorPoint({0.5f, 0.5f});
    m_resizeHandle->setPosition({m_popupW - RESIZE_SZ / 2.f, RESIZE_SZ / 2.f});
    if (handleSpr) {
        handleSpr->setPosition({RESIZE_SZ / 2.f, RESIZE_SZ / 2.f});
        m_resizeHandle->addChild(handleSpr);
    }
    m_mainLayer->addChild(m_resizeHandle, 21);
}

void EmotePickerPopup::buildBody() {
    float contentW = m_popupW - PAD * 2;

    auto toolbar = CCMenu::create();
    toolbar->setID("paimon-emote-toolbar"_spr);
    toolbar->setPosition({0.f, 0.f});
    toolbar->setContentSize({m_popupW, m_popupH});
    toolbar->ignoreAnchorPointForPosition(true);
    m_mainLayer->addChild(toolbar, 5);

    float toolbarY = m_popupH - TITLE_H - PAD - TOOLBAR_H;

    float keepW = 24.f;
    float sliderW = 70.f;
    float searchW = contentW - keepW - sliderW - PAD * 2;

    auto searchBg = paimon::ui::makeInset({searchW, TOOLBAR_H}, 255, {12, 12, 12});
    searchBg->setID("paimon-emote-search-bg"_spr);
    searchBg->setPosition({PAD, toolbarY});
    m_mainLayer->addChild(searchBg, 4);

    m_searchInput = TextInput::create(searchW - 16, "Buscar...", "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(40);
    m_searchInput->setAnchorPoint({0.5f, 0.5f});
    m_searchInput->setPosition({PAD + searchW / 2.f, toolbarY + TOOLBAR_H / 2.f});
    m_searchInput->setScale(0.78f);
    m_searchInput->setCallback(
        paimon::ui::safeTextInputCallback<EmotePickerPopup>(
            this, &EmotePickerPopup::onSearchTextChanged));
    m_mainLayer->addChild(m_searchInput, 6);

    m_keepOpenToggle = paimon::ui::makeToggle(m_keepOpen,
        [this](bool v) { m_keepOpen = v; Mod::get()->setSavedValue(KEY_KEEP, v); }, 0.5f);
    m_keepOpenToggle->setPosition({PAD + searchW + PAD + keepW / 2.f, toolbarY + TOOLBAR_H / 2.f});
    toolbar->addChild(m_keepOpenToggle);

    auto keepLbl = CCLabelBMFont::create("fijar", "chatFont.fnt");
    keepLbl->setID("paimon-emote-keep-lbl"_spr);
    keepLbl->setScale(0.26f);
    keepLbl->setColor({180, 180, 180});
    keepLbl->setPosition({PAD + searchW + PAD + keepW / 2.f, toolbarY - 3.f});
    m_mainLayer->addChild(keepLbl, 6);

    m_sizeSlider = Slider::create(this, menu_selector(EmotePickerPopup::onSizeSlider), 0.46f);
    m_sizeSlider->setValue((m_cellSize - CELL_MIN) / (CELL_MAX - CELL_MIN));
    m_sizeSlider->setPosition({PAD + searchW + PAD + keepW + PAD + sliderW / 2.f,
                               toolbarY + TOOLBAR_H / 2.f});
    m_mainLayer->addChild(m_sizeSlider, 6);

    m_tabStrip = ScrollLayer::create({contentW, TABSTRIP_H});
    m_tabStrip->setID("paimon-emote-tabstrip"_spr);
    m_mainLayer->addChild(m_tabStrip, 4);
    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({0.f, 0.f});
    m_tabStrip->m_contentLayer->addChild(m_tabMenu);

    m_scroll = ScrollLayer::create({contentW, 60.f});
    m_scroll->setID("paimon-emote-grid-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 3);
    m_contentNode = CCNode::create();
    m_scroll->m_contentLayer->addChild(m_contentNode);

    m_renderPreviewBg = paimon::ui::makeInset({contentW, PREVIEW_H}, 235, {12, 12, 12});
    m_renderPreviewBg->setID("paimon-emote-preview-bg"_spr);
    m_mainLayer->addChild(m_renderPreviewBg, 3);

    m_previewName = CCLabelBMFont::create("", "chatFont.fnt");
    m_previewName->setScale(0.4f);
    m_previewName->setAnchorPoint({0.f, 0.5f});
    m_previewName->setColor({235, 235, 235});
    m_mainLayer->addChild(m_previewName, 5);

    m_previewCode = CCLabelBMFont::create("", "chatFont.fnt");
    m_previewCode->setScale(0.34f);
    m_previewCode->setAnchorPoint({1.f, 0.5f});
    m_previewCode->setColor({150, 180, 150});
    m_mainLayer->addChild(m_previewCode, 5);

    m_countLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_countLabel->setScale(0.3f);
    m_countLabel->setAnchorPoint({1.f, 1.f});
    m_countLabel->setColor({110, 110, 110});
    m_mainLayer->addChild(m_countLabel, 6);
}

void EmotePickerPopup::relayout() {
    float contentW = m_popupW - PAD * 2;

    if (m_titleBar) m_titleBar->setContentSize({m_popupW, TITLE_H});

    float toolbarY = m_popupH - TITLE_H - PAD - TOOLBAR_H;

    float keepW = 24.f;
    float sliderW = 70.f;
    float searchW = contentW - keepW - sliderW - PAD * 2;

    if (auto sbg = m_mainLayer->getChildByID("paimon-emote-search-bg"_spr)) {
        sbg->removeFromParent();
        auto nb = paimon::ui::makeInset({searchW, TOOLBAR_H}, 255, {12, 12, 12});
        nb->setID("paimon-emote-search-bg"_spr);
        nb->setPosition({PAD, toolbarY});
        m_mainLayer->addChild(nb, 4);
    }
    if (m_searchInput) {
        m_searchInput->setContentSize({searchW - 16, m_searchInput->getContentSize().height});
        m_searchInput->setPosition({PAD + searchW / 2.f, toolbarY + TOOLBAR_H / 2.f});
    }

    float keepCx = PAD + searchW + PAD + keepW / 2.f;
    if (m_keepOpenToggle) m_keepOpenToggle->setPosition({keepCx, toolbarY + TOOLBAR_H / 2.f});
    if (auto kl = m_mainLayer->getChildByID("paimon-emote-keep-lbl"_spr))
        kl->setPosition({keepCx, toolbarY - 3.f});
    if (m_sizeSlider)
        m_sizeSlider->setPosition({PAD + searchW + PAD + keepW + PAD + sliderW / 2.f,
                                   toolbarY + TOOLBAR_H / 2.f});

    float tabY = toolbarY - PAD - TABSTRIP_H;
    if (m_tabStrip) {
        m_tabStrip->setContentSize({contentW, TABSTRIP_H});
        m_tabStrip->m_contentLayer->setContentSize({contentW, TABSTRIP_H});
        m_tabStrip->setPosition({PAD, tabY});
    }

    float previewY = PAD;
    if (m_renderPreviewBg) {
        m_renderPreviewBg->removeFromParent();
        m_renderPreviewBg = paimon::ui::makeInset({contentW, PREVIEW_H}, 235, {12, 12, 12});
        m_renderPreviewBg->setID("paimon-emote-preview-bg"_spr);
        m_renderPreviewBg->setPosition({PAD, previewY});
        m_mainLayer->addChild(m_renderPreviewBg, 3);
    }
    if (m_previewName) m_previewName->setPosition({PAD + 6.f, previewY + PREVIEW_H / 2.f});
    if (m_previewCode) m_previewCode->setPosition({PAD + contentW - 6.f, previewY + PREVIEW_H / 2.f});

    float gridTop = tabY - PAD;
    float gridBottom = previewY + PREVIEW_H + PAD;
    m_gridX = PAD;
    m_gridW = contentW;
    m_gridH = std::max(40.f, gridTop - gridBottom);
    m_botY = gridBottom;

    if (m_scroll) {
        m_scroll->setContentSize({m_gridW, m_gridH});
        m_scroll->m_contentLayer->setContentSize({m_gridW, m_gridH});
        m_scroll->setPosition({m_gridX, m_botY});
    }

    if (m_countLabel) m_countLabel->setPosition({m_popupW - PAD - 2.f, m_popupH - TITLE_H - 2.f});

    if (m_resizeHandle)
        m_resizeHandle->setPosition({m_popupW - RESIZE_SZ / 2.f, RESIZE_SZ / 2.f});
}

void EmotePickerPopup::rebuildTabStrip() {
    if (!m_tabMenu || !m_tabStrip) return;
    m_tabMenu->removeAllChildren();

    struct TabDef { std::string label; View view; std::string cat; };
    std::vector<TabDef> defs;
    defs.push_back({"Favoritos", View::Favorites, ""});
    defs.push_back({"Recientes", View::Recents, ""});
    defs.push_back({"Todos", View::All, ""});
    for (auto const& cat : EmoteService::get().getAllCategories()) {
        defs.push_back({cat, View::Category, cat});
    }

    float gap = 4.f;
    float x = gap;
    float h = TABSTRIP_H - 2.f;

    for (auto const& d : defs) {
        auto lbl = CCLabelBMFont::create(d.label.c_str(), "bigFont.fnt");
        lbl->setScale(0.28f);
        float bw = lbl->getScaledContentWidth() + 14.f;

        auto container = CCNode::create();
        container->setContentSize({bw, h});
        lbl->setPosition({bw / 2.f, h / 2.f});
        container->addChild(lbl, 1);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this, menu_selector(EmotePickerPopup::onTabClicked));
        btn->setAnchorPoint({0.f, 0.5f});
        btn->setPosition({x, TABSTRIP_H / 2.f});

        auto obj = CCArray::create();
        obj->addObject(CCInteger::create(static_cast<int>(d.view)));
        obj->addObject(CCString::create(d.cat));
        btn->setUserObject(obj);
        m_tabMenu->addChild(btn);

        x += bw + gap;
    }

    float totalW = std::max(x, m_tabStrip->getContentSize().width);
    m_tabStrip->m_contentLayer->setContentSize({totalW, TABSTRIP_H});
    m_tabMenu->setContentSize({totalW, TABSTRIP_H});
    m_tabStrip->moveToTop();
    updateTabHighlights();
}

void EmotePickerPopup::updateTabHighlights() {
    if (!m_tabMenu) return;
    for (auto* child : CCArrayExt<CCNode*>(m_tabMenu->getChildren())) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(child);
        auto container = item->getNormalImage();
        if (!container) continue;
        if (auto old = container->getChildByID("paimon-tab-bg"_spr)) old->removeFromParent();

        auto arr = static_cast<CCArray*>(item->getUserObject());
        if (!arr) continue;
        auto viewI = static_cast<CCInteger*>(arr->objectAtIndex(0))->getValue();
        auto cat = static_cast<CCString*>(arr->objectAtIndex(1))->getCString();
        bool active = static_cast<View>(viewI) == m_view &&
            (m_view != View::Category || m_activeCategory == cat);

        float w = container->getContentSize().width;
        float ch = container->getContentSize().height;
        auto hl = active
            ? paimon::ui::makeInset({w, ch}, 235, {52, 120, 70})
            : paimon::ui::makeInset({w, ch}, 150, {34, 34, 34});
        hl->setID("paimon-tab-bg"_spr);
        hl->setPosition({0, 0});
        container->addChild(hl, -1);
    }
}

void EmotePickerPopup::onTabClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto arr = static_cast<CCArray*>(btn->getUserObject());
    if (!arr) return;
    auto viewI = static_cast<CCInteger*>(arr->objectAtIndex(0))->getValue();
    auto cat = static_cast<CCString*>(arr->objectAtIndex(1))->getCString();
    switchView(static_cast<View>(viewI), cat);
}

void EmotePickerPopup::switchView(View view, std::string const& cat) {
    m_view = view;
    m_activeCategory = cat;
    if (m_tabMenu->getChildrenCount() == 0) rebuildTabStrip();
    updateTabHighlights();
    refreshGrid();
}

std::vector<EmoteInfo> EmotePickerPopup::currentEmotes() const {
    auto& svc = EmoteService::get();

    if (!m_searchQuery.empty()) {
        return svc.searchEmotes(m_searchQuery, 128);
    }

    switch (m_view) {
        case View::Favorites: {
            std::vector<EmoteInfo> out;
            for (auto const& name : m_favorites) {
                if (auto e = svc.getEmoteByName(name)) out.push_back(*e);
            }
            std::sort(out.begin(), out.end(),
                [](EmoteInfo const& a, EmoteInfo const& b) { return a.name < b.name; });
            return out;
        }
        case View::Recents: {
            std::vector<EmoteInfo> out;
            for (auto const& name : m_recents) {
                if (auto e = svc.getEmoteByName(name)) out.push_back(*e);
            }
            return out;
        }
        case View::Category:
            return svc.getAllEmotesByCategory(m_activeCategory);
        case View::All:
        default:
            return svc.getAllEmotes();
    }
}

void EmotePickerPopup::refreshGrid() {
    buildEmoteGrid(currentEmotes());
}

static cocos2d::CCNode* makeEmoteCellContainer(float cellSize, CCLayerColor*& outHover) {
    auto container = CCNode::create();
    container->setContentSize({cellSize, cellSize});

    auto cellBg = CCLayerColor::create({34, 34, 34, 200}, cellSize, cellSize);
    cellBg->setPosition({0, 0});
    container->addChild(cellBg, 0);

    auto hoverBg = CCLayerColor::create({115, 199, 242, 0}, cellSize, cellSize);
    hoverBg->setPosition({0, 0});
    hoverBg->setTag(97);
    container->addChild(hoverBg, 1);
    outHover = hoverBg;

    return container;
}

void EmotePickerPopup::buildEmoteGrid(std::vector<EmoteInfo> const& emotes) {
    m_contentNode->removeAllChildren();
    m_hoverCells.clear();
    ++m_gridGeneration;

    float gridW = m_scroll->getContentSize().width;
    float cell = m_cellSize;
    int cols = std::max(1, static_cast<int>((gridW + CELL_GAP) / (cell + CELL_GAP)));
    float used = cols * cell + (cols - 1) * CELL_GAP;
    float marginX = std::max(0.f, (gridW - used) / 2.f);

    int rows = (static_cast<int>(emotes.size()) + cols - 1) / cols;
    float contentH = rows * (cell + CELL_GAP) + CELL_GAP;
    float scrollH = m_scroll->getContentSize().height;
    float totalH = std::max(contentH, scrollH);

    m_contentNode->setContentSize({gridW, totalH});
    m_scroll->m_contentLayer->setContentSize({gridW, totalH});

    if (emotes.empty()) {
        auto empty = CCLabelBMFont::create(
            m_view == View::Favorites ? "Sin favoritos todavia"
            : m_view == View::Recents ? "Nada reciente"
            : "Sin emotes", "chatFont.fnt");
        empty->setScale(0.4f);
        empty->setColor({120, 120, 120});
        empty->setPosition({gridW / 2.f, totalH / 2.f});
        m_contentNode->addChild(empty);
        m_scroll->moveToTop();
        if (m_countLabel) m_countLabel->setString("0");
        return;
    }

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({gridW, totalH});
    m_contentNode->addChild(menu);

    m_hoverCells.reserve(emotes.size());
    for (size_t i = 0; i < emotes.size(); ++i) {
        // manual placement; rowlayout cells vanish inside this scroll.
        int col = static_cast<int>(i % static_cast<size_t>(cols));
        int row = static_cast<int>(i / static_cast<size_t>(cols));
        float x = marginX + col * (cell + CELL_GAP) + cell / 2.f;
        float y = totalH - CELL_GAP - (row * (cell + CELL_GAP) + cell / 2.f);

        CCLayerColor* hoverLayer = nullptr;
        auto container = makeEmoteCellContainer(cell, hoverLayer);

        auto ph = CCLabelBMFont::create("...", "chatFont.fnt");
        ph->setScale(0.3f);
        ph->setPosition({cell / 2, cell / 2});
        ph->setTag(99);
        container->addChild(ph, 5);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this, menu_selector(EmotePickerPopup::onEmoteClicked));
        btn->setUserObject(CCString::create(emotes[i].name));
        btn->setPosition({x, y});
        menu->addChild(btn);

        float starSz = std::clamp(cell * 0.34f, 9.f, 16.f);
        auto starSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starBtn_001.png");
        CCMenuItemSpriteExtra* starBtn = nullptr;
        if (starSpr) {
            starSpr->setScale(starSz / std::max(1.f, starSpr->getContentSize().width));
            starBtn = CCMenuItemSpriteExtra::create(
                starSpr, this, menu_selector(EmotePickerPopup::onStarClicked));
            starBtn->setUserObject(CCString::create(emotes[i].name));
            starBtn->setPosition({x + cell / 2.f - starSz * 0.55f,
                                  y + cell / 2.f - starSz * 0.55f});
            starSpr->setColor(isFavorite(emotes[i].name)
                ? ccColor3B{255, 210, 90} : ccColor3B{90, 90, 90});
            menu->addChild(starBtn, 2);
        }

        HoverCell hc;
        hc.btn = btn;
        hc.hoverLayer = hoverLayer;
        hc.container = container;
        hc.starBtn = starBtn;
        hc.info = emotes[i];
        hc.placeholder = ph;
        m_hoverCells.push_back(std::move(hc));
    }

    m_scroll->moveToTop();
    if (m_countLabel) m_countLabel->setString(fmt::format("{}", emotes.size()).c_str());

    WeakRef<EmotePickerPopup> selfWeak = this;
    Loader::get()->queueInMainThread([selfWeak]() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = selfWeak.lock();
        if (!self) return;
        self->requestVisibleThumbnails();
    });
}

void EmotePickerPopup::onEmoteClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!isInsideVisibleScroll(btn)) return;
    auto nameObj = static_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;
    insertEmoteAtCursor(nameObj->getCString());
}

void EmotePickerPopup::onStarClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!isInsideVisibleScroll(btn)) return;
    auto nameObj = static_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;
    std::string name = nameObj->getCString();
    toggleFavorite(name);

    for (auto& hc : m_hoverCells) {
        if (hc.info.name != name || !hc.starBtn) continue;
        if (auto spr = typeinfo_cast<CCSprite*>(hc.starBtn->getNormalImage())) {
            spr->setColor(isFavorite(name) ? ccColor3B{255, 210, 90} : ccColor3B{90, 90, 90});
        }
    }
    if (m_view == View::Favorites) refreshGrid();
}

void EmotePickerPopup::insertEmoteAtCursor(std::string const& emoteName) {
    std::string emoteText = fmt::format(":{}:", emoteName);
    std::string current = m_getText ? m_getText() : "";
    std::string newText = current + emoteText;

    if (static_cast<int>(newText.size()) > m_charLimit) return;

    pushRecent(emoteName);
    if (m_onTextChanged) m_onTextChanged(newText);

    if (!m_keepOpen) onClose(nullptr);
}

void EmotePickerPopup::insertFirstResult() {
    if (m_hoverCells.empty()) return;
    insertEmoteAtCursor(m_hoverCells.front().info.name);
}

void EmotePickerPopup::onSearchTextChanged(std::string const& text) {
    m_searchQuery = text;
    refreshGrid();
}

void EmotePickerPopup::onSizeSlider(CCObject*) {
    if (!m_sizeSlider) return;
    float v = std::clamp(m_sizeSlider->getThumb()->getValue(), 0.f, 1.f);
    float newCell = CELL_MIN + v * (CELL_MAX - CELL_MIN);
    if (std::abs(newCell - m_cellSize) < 0.5f) return;
    m_cellSize = newCell;
    Mod::get()->setSavedValue<double>(KEY_CELL, m_cellSize);
    refreshGrid();
}

void EmotePickerPopup::onResetLayout(CCObject*) {
    m_popupW = (m_layoutSize == LayoutSize::Large) ? POPUP_W_LARGE : POPUP_W_DEF;
    m_popupH = (m_layoutSize == LayoutSize::Large) ? POPUP_H_LARGE : POPUP_H_DEF;
    m_cellSize = 34.f;

    if (m_mainLayer) {
        m_mainLayer->setContentSize({m_popupW, m_popupH});
        auto winSize = CCDirector::get()->getWinSize();
        m_mainLayer->setPosition({winSize.width * 0.5f, winSize.height * 0.5f});
    }
    if (m_sizeSlider) m_sizeSlider->setValue((m_cellSize - CELL_MIN) / (CELL_MAX - CELL_MIN));

    buildChrome();
    relayout();
    refreshGrid();
    saveLayout();
}

void EmotePickerPopup::updateRefreshButtonState() {
    if (!m_refreshBtn) return;
    bool enabled = !m_isRefreshingCatalog;
    m_refreshBtn->setEnabled(enabled);
    m_refreshBtn->setOpacity(enabled ? 255 : 120);
    if (auto normal = typeinfo_cast<CCSprite*>(m_refreshBtn->getNormalImage()))
        normal->setOpacity(enabled ? 255 : 120);
}

void EmotePickerPopup::onRefreshCatalog(CCObject*) {
    if (m_isRefreshingCatalog) return;
    auto& service = EmoteService::get();
    if (service.isFetching()) {
        PaimonNotify::create("Los emotes ya se estan actualizando.", NotificationIcon::Info)->show();
        return;
    }

    m_isRefreshingCatalog = true;
    updateRefreshButtonState();

    WeakRef<EmotePickerPopup> self = this;
    service.fetchAllEmotes([self](bool success) {
        Loader::get()->queueInMainThread([self, success]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;

            popup->m_isRefreshingCatalog = false;
            popup->updateRefreshButtonState();

            if (success) {
                EmoteCache::get().clearRam();
                popup->rebuildTabStrip();
                popup->refreshGrid();
                EmoteCache::get().preloadAllToDisk();
                PaimonNotify::create("Catalogo de emotes actualizado.", NotificationIcon::Success)->show();
            } else {
                PaimonNotify::create("No se pudo actualizar el catalogo de emotes.", NotificationIcon::Error)->show();
            }
        });
    });
}

bool EmotePickerPopup::isFavorite(std::string const& name) const {
    return m_favorites.find(name) != m_favorites.end();
}

void EmotePickerPopup::toggleFavorite(std::string const& name) {
    if (auto it = m_favorites.find(name); it != m_favorites.end()) {
        m_favorites.erase(it);
    } else {
        m_favorites.insert(name);
    }
    saveFavorites();
}

void EmotePickerPopup::pushRecent(std::string const& name) {
    auto it = std::find(m_recents.begin(), m_recents.end(), name);
    if (it != m_recents.end()) m_recents.erase(it);
    m_recents.insert(m_recents.begin(), name);
    if (m_recents.size() > MAX_RECENTS) m_recents.resize(MAX_RECENTS);
    saveRecents();
}

void EmotePickerPopup::loadFavoritesAndRecents() {
    auto* mod = Mod::get();
    for (auto const& f : splitList(mod->getSavedValue<std::string>(KEY_FAVS, "")))
        m_favorites.insert(f);
    m_recents = splitList(mod->getSavedValue<std::string>(KEY_RECENTS, ""));
}

void EmotePickerPopup::saveFavorites() {
    std::vector<std::string> v(m_favorites.begin(), m_favorites.end());
    Mod::get()->setSavedValue<std::string>(KEY_FAVS, joinList(v));
}

void EmotePickerPopup::saveRecents() {
    Mod::get()->setSavedValue<std::string>(KEY_RECENTS, joinList(m_recents));
}

void EmotePickerPopup::loadLayout() {
    auto* mod = Mod::get();
    float defW = (m_layoutSize == LayoutSize::Large) ? POPUP_W_LARGE : POPUP_W_DEF;
    float defH = (m_layoutSize == LayoutSize::Large) ? POPUP_H_LARGE : POPUP_H_DEF;

    auto winSize = CCDirector::get()->getWinSize();
    m_popupW = std::clamp(static_cast<float>(mod->getSavedValue<double>(KEY_W, defW)),
                          POPUP_W_MIN, winSize.width - 20.f);
    m_popupH = std::clamp(static_cast<float>(mod->getSavedValue<double>(KEY_H, defH)),
                          POPUP_H_MIN, winSize.height - 20.f);
    m_cellSize = std::clamp(static_cast<float>(mod->getSavedValue<double>(KEY_CELL, 34.0)),
                            CELL_MIN, CELL_MAX);
    m_keepOpen = mod->getSavedValue<bool>(KEY_KEEP, false);
}

void EmotePickerPopup::saveLayout() {
    auto* mod = Mod::get();
    mod->setSavedValue<double>(KEY_W, m_popupW);
    mod->setSavedValue<double>(KEY_H, m_popupH);
    mod->setSavedValue<double>(KEY_CELL, m_cellSize);
    if (m_mainLayer) {
        mod->setSavedValue<double>(KEY_POS_X, m_mainLayer->getPositionX());
        mod->setSavedValue<double>(KEY_POS_Y, m_mainLayer->getPositionY());
    }
}

void EmotePickerPopup::onExit() {
    saveLayout();
    paimon::ui::detachGeodeTextInput(m_searchInput);
    m_searchInput = nullptr;
    paimon::popupblur::cleanup(this);
    Popup::onExit();
}

void EmotePickerPopup::update(float dt) {
    if (m_hoverCells.empty() || !m_scroll) return;

    if ((m_hoverFrameSkip++ & 1) != 0) return;
    float effDt = dt * 2.f;

    CCPoint mouseGL = geode::cocos::getMousePos();
    CCPoint scrollWorld = m_scroll->convertToWorldSpace({0, 0});
    CCSize scrollSize = m_scroll->getContentSize();
    CCRect scrollRect(scrollWorld.x, scrollWorld.y, scrollSize.width, scrollSize.height);
    bool mouseInScroll = scrollRect.containsPoint(mouseGL);

    float lerpAmt = std::min(1.f, effDt * 5.5f);
    float cell = m_cellSize;
    float halfCell = cell * 0.5f;

    std::string hoveredName, hoveredCode;

    for (auto const& hc : m_hoverCells) {
        if (!hc.btn || !hc.btn->getParent() || !hc.hoverLayer) continue;

        CCPoint cellWorld = hc.btn->getParent()->convertToWorldSpace(hc.btn->getPosition());
        bool offscreen =
            cellWorld.x + halfCell < scrollRect.getMinX() ||
            cellWorld.x - halfCell > scrollRect.getMaxX() ||
            cellWorld.y + halfCell < scrollRect.getMinY() ||
            cellWorld.y - halfCell > scrollRect.getMaxY();

        // unschedule offscreen gif ticks; this is the main fps win.
        if (hc.isGifSprite) {
            if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(hc.sprite)) {
                if (offscreen && gif->isPlaying()) gif->pause();
                else if (!offscreen && !gif->isPlaying()) gif->play();
            }
        }

        if (offscreen) {
            if (hc.hoverLayer->getOpacity() != 0) hc.hoverLayer->setOpacity(0);
            continue;
        }

        bool hovered = mouseInScroll &&
                       std::abs(mouseGL.x - cellWorld.x) <= halfCell &&
                       std::abs(mouseGL.y - cellWorld.y) <= halfCell;

        if (hovered) {
            hoveredName = hc.info.name;
            hoveredCode = fmt::format(":{}:", hc.info.name);
        }

        float target = hovered ? 200.f : 0.f;
        float current = static_cast<float>(hc.hoverLayer->getOpacity());
        if (std::abs(current - target) < 0.5f) {
            if (current != target) hc.hoverLayer->setOpacity(static_cast<GLubyte>(target));
            continue;
        }
        float next = current + (target - current) * lerpAmt;
        if (std::abs(next - target) < 1.f) next = target;
        hc.hoverLayer->setOpacity(static_cast<GLubyte>(std::clamp(next, 0.f, 255.f)));
    }

    if (m_previewName && m_previewCode) {
        m_previewName->setString(hoveredName.empty() ? "Pasa el raton sobre un emote" : hoveredName.c_str());
        m_previewName->setColor(hoveredName.empty() ? ccColor3B{110, 110, 110} : ccColor3B{235, 235, 235});
        m_previewCode->setString(hoveredCode.c_str());
    }

    if ((m_lazyLoadFrameSkip++ % 3) == 0) requestVisibleThumbnails();
}

void EmotePickerPopup::requestVisibleThumbnails() {
    if (!m_scroll || m_hoverCells.empty()) return;

    CCPoint scrollWorld = m_scroll->convertToWorldSpace({0, 0});
    CCSize scrollSize = m_scroll->getContentSize();

    float prefetch = m_cellSize * 1.5f;
    CCRect viewport(scrollWorld.x - prefetch, scrollWorld.y - prefetch,
                    scrollSize.width + prefetch * 2, scrollSize.height + prefetch * 2);
    float halfCell = m_cellSize * 0.5f;

    for (size_t i = 0; i < m_hoverCells.size(); ++i) {
        auto& hc = m_hoverCells[i];
        if (hc.loadRequested) continue;
        if (!hc.btn || !hc.btn->getParent()) continue;

        CCPoint cellWorld = hc.btn->getParent()->convertToWorldSpace(hc.btn->getPosition());
        bool inView =
            cellWorld.x + halfCell >= viewport.getMinX() &&
            cellWorld.x - halfCell <= viewport.getMaxX() &&
            cellWorld.y + halfCell >= viewport.getMinY() &&
            cellWorld.y - halfCell <= viewport.getMaxY();
        if (!inView) continue;

        loadCellThumbnail(i);
    }
}

void EmotePickerPopup::requestAllThumbnails() {
    for (size_t i = 0; i < m_hoverCells.size(); ++i) loadCellThumbnail(i);
}

void EmotePickerPopup::loadCellThumbnail(size_t cellIdx) {
    if (cellIdx >= m_hoverCells.size()) return;
    auto& hc = m_hoverCells[cellIdx];
    if (hc.loadRequested) return;
    if (!hc.btn || !hc.container) return;

    hc.loadRequested = true;

    WeakRef<EmotePickerPopup> selfWeak = this;
    uint32_t gen = m_gridGeneration;
    EmoteCache::get().loadEmote(hc.info,
        [selfWeak, cellIdx, gen](CCTexture2D* tex, bool isGif,
                                 std::vector<uint8_t> const& gifData) {
            auto self = selfWeak.lock();
            if (!self) return;
            if (self->m_gridGeneration != gen) return;
            self->attachLoadedThumbnail(cellIdx, tex, isGif, gifData);
        });
}

void EmotePickerPopup::attachLoadedThumbnail(size_t cellIdx,
                                              cocos2d::CCTexture2D* tex,
                                              bool isGif,
                                              std::vector<uint8_t> gifData) {
    if (cellIdx >= m_hoverCells.size()) return;
    auto& hc = m_hoverCells[cellIdx];
    if (hc.loaded) return;
    if (!hc.container || !hc.btn || !hc.btn->getParent()) return;

    float cell = m_cellSize;

    if (isGif && !gifData.empty()) {
        WeakRef<EmotePickerPopup> selfWeak = this;
        size_t idx = cellIdx;
        uint32_t gen = m_gridGeneration;
        hc.loaded = true;
        AnimatedGIFSprite::createAsync(gifData, hc.info.filename,
            [selfWeak, idx, gen, cell](AnimatedGIFSprite* gifSprite) {
                auto self = selfWeak.lock();
                if (!self) return;
                if (self->m_gridGeneration != gen) return;
                if (idx >= self->m_hoverCells.size()) return;
                auto& cellRef = self->m_hoverCells[idx];
                if (!cellRef.container || !cellRef.btn || !cellRef.btn->getParent()) return;
                if (!gifSprite) { cellRef.loaded = false; return; }

                float maxD = cell - 6.f;
                float sc = maxD / std::max(gifSprite->getContentSize().width,
                                           gifSprite->getContentSize().height);
                gifSprite->setScale(sc);
                gifSprite->setPosition({cell / 2, cell / 2});
                cellRef.container->addChild(gifSprite, 2);
                cellRef.sprite = gifSprite;
                cellRef.isGifSprite = true;
                if (cellRef.placeholder) cellRef.placeholder->setVisible(false);
            });
        return;
    }

    if (tex) {
        auto sprite = CCSprite::createWithTexture(tex);
        if (sprite) {
            float maxD = cell - 6.f;
            float sc = maxD / std::max(sprite->getContentSize().width,
                                       sprite->getContentSize().height);
            sprite->setScale(sc);
            sprite->setPosition({cell / 2, cell / 2});
            hc.container->addChild(sprite, 2);
            hc.sprite = sprite;
            hc.isGifSprite = false;
            if (hc.placeholder) hc.placeholder->setVisible(false);
            hc.loaded = true;
        }
    }
}

static bool nodeContainsTouch(CCNode* node, CCTouch* touch) {
    if (!node || !node->getParent()) return false;
    auto local = node->getParent()->convertTouchToNodeSpace(touch);
    CCPoint origin = node->getPosition() - CCPoint{
        node->getContentSize().width * node->getAnchorPoint().x,
        node->getContentSize().height * node->getAnchorPoint().y};
    return CCRect(origin.x, origin.y, node->getContentSize().width,
                  node->getContentSize().height).containsPoint(local);
}

bool EmotePickerPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    m_draggingTitle = false;
    m_draggingResize = false;
    m_touchHitOutside = false;

    if (m_resizeHandle && nodeContainsTouch(m_resizeHandle, touch)) {
        m_draggingResize = true;
        m_dragStartTouch = touch->getLocation();
        m_dragStartSize = CCSize{m_popupW, m_popupH};
        m_dragStartPos = m_mainLayer->getPosition();
        return true;
    }
    if (m_titleBar && nodeContainsTouch(m_titleBar, touch)) {
        m_draggingTitle = true;
        m_dragStartTouch = touch->getLocation();
        m_dragStartPos = m_mainLayer->getPosition();
        return true;
    }

    auto local = m_mainLayer->convertToNodeSpace(touch->getLocation());
    auto sz = m_mainLayer->getContentSize();
    m_touchHitOutside = !CCRect(0, 0, sz.width, sz.height).containsPoint(local);
    return true;
}

void EmotePickerPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    auto winSize = CCDirector::get()->getWinSize();
    CCPoint delta = touch->getLocation() - m_dragStartTouch;

    if (m_draggingTitle) {
        CCPoint np = m_dragStartPos + delta;
        float halfW = m_popupW * 0.5f;
        float halfH = m_popupH * 0.5f;
        np.x = std::clamp(np.x, halfW, winSize.width - halfW);
        np.y = std::clamp(np.y, halfH, winSize.height - halfH);
        m_mainLayer->setPosition(np);
        return;
    }

    if (m_draggingResize) {
        float newW = std::clamp(m_dragStartSize.width + delta.x,
                                POPUP_W_MIN, winSize.width - 20.f);
        float newH = std::clamp(m_dragStartSize.height - delta.y,
                                POPUP_H_MIN, winSize.height - 20.f);
        if (std::abs(newW - m_popupW) < 1.f && std::abs(newH - m_popupH) < 1.f) return;

        // keep the top-left corner pinned as the window grows downward/right.
        float dw = newW - m_popupW;
        float dh = newH - m_popupH;
        m_popupW = newW;
        m_popupH = newH;
        m_mainLayer->setContentSize({m_popupW, m_popupH});
        m_mainLayer->setPositionX(m_mainLayer->getPositionX() + dw * 0.5f);
        m_mainLayer->setPositionY(m_mainLayer->getPositionY() - dh * 0.5f);

        buildChrome();
        relayout();
        // reflow the grid in coarse steps; chrome follows every pixel.
        if (std::abs(newW - m_lastGridW) >= 8.f || std::abs(newH - m_lastGridH) >= 8.f) {
            m_lastGridW = newW;
            m_lastGridH = newH;
            refreshGrid();
        } else if (m_scroll) {
            m_scroll->moveToTop();
        }
        return;
    }
}

void EmotePickerPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_draggingTitle || m_draggingResize) {
        bool wasResize = m_draggingResize;
        m_draggingTitle = false;
        m_draggingResize = false;
        if (wasResize) { m_lastGridW = m_popupW; m_lastGridH = m_popupH; refreshGrid(); }
        saveLayout();
        return;
    }
    if (m_touchHitOutside) {
        auto local = m_mainLayer->convertToNodeSpace(touch->getLocation());
        auto sz = m_mainLayer->getContentSize();
        if (!CCRect(0, 0, sz.width, sz.height).containsPoint(local)) {
            onClose(nullptr);
        }
    }
}

void EmotePickerPopup::keyDown(cocos2d::enumKeyCodes key, double p1) {
    using cocos2d::enumKeyCodes;
    if (key == enumKeyCodes::KEY_Escape) { onClose(nullptr); return; }
    if (key == enumKeyCodes::KEY_Enter || key == enumKeyCodes::KEY_NumEnter) {
        insertFirstResult();
        return;
    }
    Popup::keyDown(key, p1);
}

bool EmotePickerPopup::isInsideVisibleScroll(CCNode* item) {
    if (!m_scroll || !item) return false;
    auto scrollWorld = m_scroll->convertToWorldSpace({0, 0});
    auto scrollSize = m_scroll->getContentSize();
    auto itemWorld = item->getParent()->convertToWorldSpace(item->getPosition());
    return itemWorld.x >= scrollWorld.x && itemWorld.x <= scrollWorld.x + scrollSize.width
        && itemWorld.y >= scrollWorld.y && itemWorld.y <= scrollWorld.y + scrollSize.height;
}

void EmotePickerPopup::positionNearBottom(CCNode* anchor, float bottomPadding) {
    (void)anchor;
    auto* mod = Mod::get();
    auto winSize = CCDirector::get()->getWinSize();

    double sx = mod->getSavedValue<double>(KEY_POS_X, -1.0);
    double sy = mod->getSavedValue<double>(KEY_POS_Y, -1.0);
    if (sx >= 0.0 && sy >= 0.0) {
        float halfW = m_popupW * 0.5f, halfH = m_popupH * 0.5f;
        m_mainLayer->setPosition({
            std::clamp(static_cast<float>(sx), halfW, winSize.width - halfW),
            std::clamp(static_cast<float>(sy), halfH, winSize.height - halfH)});
        return;
    }

    float halfH = m_popupH * 0.5f;
    float y = std::clamp(halfH + bottomPadding, halfH, winSize.height - halfH);
    m_mainLayer->setPosition({winSize.width * 0.5f, y});
}

void EmotePickerPopup::positionCentered() {
    auto* mod = Mod::get();
    auto winSize = CCDirector::get()->getWinSize();
    double sx = mod->getSavedValue<double>(KEY_POS_X, -1.0);
    double sy = mod->getSavedValue<double>(KEY_POS_Y, -1.0);
    if (sx >= 0.0 && sy >= 0.0) {
        float halfW = m_popupW * 0.5f, halfH = m_popupH * 0.5f;
        m_mainLayer->setPosition({
            std::clamp(static_cast<float>(sx), halfW, winSize.width - halfW),
            std::clamp(static_cast<float>(sy), halfH, winSize.height - halfH)});
        return;
    }
    m_mainLayer->setPosition({winSize.width * 0.5f, winSize.height * 0.5f});
}

void EmotePickerPopup::show() {
    PaimonPopup::show();
    m_restingScale = m_mainLayer ? m_mainLayer->getScale() : 1.f;

    paimon::popupblur::captureAndApply(this);

    if (!paimon::ui::motionEnabled()) {
        this->setOpacity(m_dimOpacity);
        return;
    }
    this->stopActionByTag(kDimActionTag);
    this->setOpacity(0);
    auto dimIn = CCEaseSineOut::create(CCFadeTo::create(ANIM_DIM_IN, m_dimOpacity));
    dimIn->setTag(kDimActionTag);
    this->runAction(dimIn);

    if (m_mainLayer) {
        m_mainLayer->stopActionByTag(kBodyActionTag);
        m_mainLayer->setScale(ANIM_IN_SCALE * m_restingScale);
        auto bodyIn = CCEaseBackOut::create(CCScaleTo::create(paimon::ui::motionDuration(ANIM_IN_DUR), m_restingScale));
        bodyIn->setTag(kBodyActionTag);
        m_mainLayer->runAction(bodyIn);
    }
}

void EmotePickerPopup::onClose(CCObject*) {
    if (m_closing) return;
    m_closing = true;

    saveLayout();
    paimon::popupblur::cleanupWithFade(this, ANIM_DIM_OUT);

    paimon::ui::detachGeodeTextInput(m_searchInput);

    this->setTouchEnabled(false);
    if (!paimon::ui::motionEnabled()) { finishClose(); return; }

    this->stopActionByTag(kDimActionTag);
    auto dimOut = CCEaseSineIn::create(CCFadeTo::create(ANIM_DIM_OUT, 0));
    dimOut->setTag(kDimActionTag);
    this->runAction(dimOut);

    if (m_mainLayer) {
        m_mainLayer->stopActionByTag(kBodyActionTag);
        auto bodyOut = CCSequence::create(
            CCEaseBackIn::create(CCScaleTo::create(paimon::ui::motionDuration(ANIM_OUT_DUR), ANIM_OUT_SCALE * m_restingScale)),
            CCCallFunc::create(this, callfunc_selector(EmotePickerPopup::finishClose)),
            nullptr);
        bodyOut->setTag(kBodyActionTag);
        m_mainLayer->runAction(bodyOut);
    } else {
        finishClose();
    }
}

void EmotePickerPopup::finishClose() {
    Popup::onClose(nullptr);
}

void EmotePickerPopup::closeAnimated() {
    onClose(nullptr);
}

}
