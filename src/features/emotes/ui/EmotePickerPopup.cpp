#include "EmotePickerPopup.hpp"
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

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::emotes {

static constexpr float POPUP_W   = 380.f;
static constexpr float POPUP_H   = 192.f;
static constexpr float POPUP_W_LARGE = 500.f;
static constexpr float POPUP_H_LARGE = 280.f;
static constexpr float CORNER_R  = 12.f;
static constexpr float PAD       = 6.f;
static constexpr float INPUT_H   = 24.f;
static constexpr float PREVIEW_H = 36.f;
static constexpr float SIDEBAR_W = 78.f;
static constexpr float CELL_SIZE = 32.f;
static constexpr float CELL_GAP  = 3.f;
static constexpr float TAB_H            = 18.f;
static constexpr float CAT_HDR_H        = 18.f;
static constexpr float CAT_GAP          = 6.f;
static constexpr float INPUT_ACTION_W   = 22.f;
static constexpr float INPUT_ACTION_GAP = 6.f;

static constexpr ccColor4F COL_BORDER      = {0.18f, 0.18f, 0.18f, 1.0f};
static constexpr ccColor4F COL_BG          = {0.07f, 0.07f, 0.07f, 0.97f};
static constexpr ccColor4F COL_INPUT_BG    = {0.05f, 0.05f, 0.05f, 1.0f};
static constexpr ccColor4F COL_PREVIEW_BG  = {0.06f, 0.06f, 0.06f, 0.9f};
static constexpr ccColor4F COL_BOTTOM_BG   = {0.09f, 0.09f, 0.09f, 1.0f};
static constexpr ccColor4F COL_TAB_ACTIVE  = {0.22f, 0.22f, 0.22f, 1.0f};
static constexpr ccColor4F COL_TAB_INACTIVE= {0.13f, 0.13f, 0.13f, 0.8f};
static constexpr ccColor4F COL_CELL_BG     = {0.14f, 0.14f, 0.14f, 0.8f};
static constexpr ccColor4F COL_CELL_HOVER  = {0.45f, 0.78f, 0.95f, 0.85f};
static constexpr ccColor4F COL_CAT_HL      = {0.20f, 0.20f, 0.20f, 0.7f};
static constexpr ccColor4F COL_DIVIDER     = {0.22f, 0.22f, 0.22f, 0.5f};
static constexpr ccColor4F COL_SEPARATOR   = {0.18f, 0.18f, 0.18f, 0.6f};

// Tags for the entrance/exit animations so they can be cancelled cleanly.
static constexpr int kDimActionTag  = 8801;
static constexpr int kBodyActionTag = 8802;

// Animation tuning — smooth single-overshoot zoom + backdrop cross-fade.
static constexpr float ANIM_IN_DUR    = 0.34f;
static constexpr float ANIM_IN_SCALE  = 0.80f;
static constexpr float ANIM_DIM_IN    = 0.24f;
static constexpr float ANIM_OUT_DUR   = 0.17f;
static constexpr float ANIM_OUT_SCALE = 0.65f;
static constexpr float ANIM_DIM_OUT   = 0.16f;

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
    m_popupW = (size == LayoutSize::Large) ? POPUP_W_LARGE : POPUP_W;
    m_popupH = (size == LayoutSize::Large) ? POPUP_H_LARGE : POPUP_H;

    if (!Popup::init(m_popupW, m_popupH))
        return false;

    if (m_closeBtn) m_closeBtn->setVisible(false);

    if (m_bgSprite) m_bgSprite->setVisible(false);

    auto border = paimon::SpriteHelper::createRoundedRect(
        m_popupW + 2, m_popupH + 2, CORNER_R + 1, COL_BORDER);
    border->setPosition({-1.f, -1.f});
    m_mainLayer->addChild(border, -2);

    auto bg = paimon::SpriteHelper::createRoundedRect(
        m_popupW, m_popupH, CORNER_R, COL_BG);
    bg->setPosition({0.f, 0.f});
    m_mainLayer->addChild(bg, -1);

    float contentW = m_popupW - PAD * 2;

    float inputY = m_popupH - PAD - INPUT_H;
    float inputActionsW = INPUT_ACTION_W * 2 + INPUT_ACTION_GAP;
    float inputActionTotal = inputActionsW + INPUT_ACTION_GAP;
    float inputBoxW = contentW - inputActionTotal;

    auto inputBg = paimon::SpriteHelper::createRoundedRect(
        inputBoxW, INPUT_H, 6.f, COL_INPUT_BG);
    inputBg->setPosition({PAD, inputY});
    m_mainLayer->addChild(inputBg, 1);

    m_textInput = TextInput::create(inputBoxW - 16, "Type your comment...", "chatFont.fnt");
    m_textInput->setCommonFilter(CommonFilter::Any);
    m_textInput->setMaxCharCount(m_charLimit);
    m_textInput->setAnchorPoint({0.5f, 0.5f});
    m_textInput->setPosition({PAD + inputBoxW / 2.f, inputY + INPUT_H / 2.f});
    m_textInput->setScale(0.78f);
    m_textInput->setCallback(
        paimon::ui::safeTextInputCallback<EmotePickerPopup>(
            this, &EmotePickerPopup::onInputTextChanged
        )
    );
    m_mainLayer->addChild(m_textInput, 2);

    auto inputActionBg = paimon::SpriteHelper::createRoundedRect(
        inputActionsW, INPUT_H, 6.f, COL_INPUT_BG);
    inputActionBg->setPosition({PAD + inputBoxW + INPUT_ACTION_GAP, inputY});
    m_mainLayer->addChild(inputActionBg, 1);

    auto actionsMenu = CCMenu::create();
    actionsMenu->setID("emote-input-actions-menu"_spr);
    actionsMenu->setContentSize({inputActionsW, INPUT_H});
    actionsMenu->setAnchorPoint({0.f, 0.f});
    actionsMenu->setPosition({PAD + inputBoxW + INPUT_ACTION_GAP, inputY});
    actionsMenu->ignoreAnchorPointForPosition(false);
    actionsMenu->setLayout(
        RowLayout::create()
            ->setGap(INPUT_ACTION_GAP)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
    );
    m_mainLayer->addChild(actionsMenu, 3);

    auto searchSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png");
    if (!searchSpr) searchSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_searchBtn_001.png");
    if (searchSpr) {
        searchSpr->setScale(0.432f);
        m_searchBtn = CCMenuItemSpriteExtra::create(
            searchSpr, this,
            menu_selector(EmotePickerPopup::onSearchToggle));
        m_searchBtn->setID("emote-search-btn"_spr);
        actionsMenu->addChild(m_searchBtn);
    }

    auto refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
    if (!refreshSpr) refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png");
    if (refreshSpr) {
        refreshSpr->setScale(0.38f);
        m_refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSpr, this,
            menu_selector(EmotePickerPopup::onRefreshCatalog));
        m_refreshBtn->setID("emote-refresh-btn"_spr);
        actionsMenu->addChild(m_refreshBtn);
        updateRefreshButtonState();
    }

    actionsMenu->updateLayout();

    if (m_getText) {
        std::string current = m_getText();
        if (!current.empty()) {
            m_textInput->setString(current);
        }
    }

    float previewY = inputY - PAD - PREVIEW_H;

    m_renderPreviewBg = paimon::SpriteHelper::createRoundedRect(
        contentW, PREVIEW_H, 6.f, COL_PREVIEW_BG);
    m_renderPreviewBg->setPosition({PAD, previewY});
    m_mainLayer->addChild(m_renderPreviewBg, 1);

    m_renderPreview = CCNode::create();
    m_renderPreview->setContentSize({contentW, PREVIEW_H});
    m_renderPreview->setPosition({PAD, previewY});
    m_mainLayer->addChild(m_renderPreview, 2);

    updateRenderPreview();

    {
        m_searchInputBg = paimon::SpriteHelper::createRoundedRect(
            contentW, PREVIEW_H, 6.f, COL_INPUT_BG);
        m_searchInputBg->setPosition({PAD, previewY});
        m_searchInputBg->setVisible(false);
        m_mainLayer->addChild(m_searchInputBg, 3);

        m_searchInput = TextInput::create(contentW - 16, "Search emotes...", "chatFont.fnt");
        m_searchInput->setCommonFilter(CommonFilter::Any);
        m_searchInput->setMaxCharCount(40);
        m_searchInput->setAnchorPoint({0.5f, 0.5f});
        m_searchInput->setPosition({PAD + contentW / 2.f, previewY + PREVIEW_H / 2.f});
        m_searchInput->setScale(0.78f);
        m_searchInput->setVisible(false);
        m_searchInput->setCallback(
            paimon::ui::safeTextInputCallback<EmotePickerPopup>(
                this, &EmotePickerPopup::onSearchTextChanged
            )
        );
        m_mainLayer->addChild(m_searchInput, 4);
    }

    float botH = previewY - PAD - PAD;
    m_botY = PAD;
    float botW = contentW;

    auto botBg = paimon::SpriteHelper::createRoundedRect(
        botW, botH, 8.f, COL_BOTTOM_BG);
    botBg->setPosition({PAD, m_botY});
    m_mainLayer->addChild(botBg, 1);

    float sideX = PAD + 4;

    m_typeMenu = CCMenu::create();
    m_typeMenu->setID("emote-type-tabs-menu"_spr);
    m_typeMenu->setContentSize({SIDEBAR_W, TAB_H * 3 + 8.f});
    m_typeMenu->setAnchorPoint({0.f, 1.f});
    m_typeMenu->ignoreAnchorPointForPosition(false);
    m_typeMenu->setPosition({sideX, m_botY + botH - 2.f});
    m_typeMenu->setLayout(
        ColumnLayout::create()
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAxisReverse(true)
    );
    m_mainLayer->addChild(m_typeMenu, 3);

    auto makeTabBtn = [&](const char* text, SEL_MenuHandler sel) -> CCMenuItemSpriteExtra* {
        float bw = SIDEBAR_W - 8;
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.3f);
        auto container = CCNode::create();
        container->setContentSize({bw, TAB_H});
        lbl->setPosition({bw / 2, TAB_H / 2});
        container->addChild(lbl, 1);
        auto btn = CCMenuItemSpriteExtra::create(container, this, sel);
        return btn;
    };

    m_btnAll = makeTabBtn("All",
        menu_selector(EmotePickerPopup::onTabAll));
    m_typeMenu->addChild(m_btnAll);

    m_btnStatic = makeTabBtn("Static",
        menu_selector(EmotePickerPopup::onTabStatic));
    m_typeMenu->addChild(m_btnStatic);

    m_btnGif = makeTabBtn("GIF",
        menu_selector(EmotePickerPopup::onTabGif));
    m_typeMenu->addChild(m_btnGif);

    m_typeMenu->updateLayout();

    float catTop = m_botY + botH - TAB_H * 3 - 14.f;
    float catH   = catTop - m_botY - 3.f;

    m_catScroll = ScrollLayer::create({SIDEBAR_W, catH});
    m_catScroll->setPosition({sideX, m_botY + 3.f});
    m_mainLayer->addChild(m_catScroll, 3);

    m_catMenu = CCMenu::create();
    m_catMenu->setPosition({0, 0});
    m_catScroll->m_contentLayer->addChild(m_catMenu);

    float divX = PAD + SIDEBAR_W + 4;
    auto divider = paimon::SpriteHelper::createRoundedRect(
        1.5f, botH - 6.f, 1.f, COL_DIVIDER);
    divider->setPosition({divX, m_botY + 3.f});
    m_mainLayer->addChild(divider, 2);

    m_gridX = divX + 6;
    m_gridW = PAD + botW - m_gridX + PAD;
    m_gridH = botH - 3.f;

    m_scroll = ScrollLayer::create({m_gridW, m_gridH});
    m_scroll->setPosition({m_gridX, m_botY + 1.f});
    m_mainLayer->addChild(m_scroll, 3);

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({m_gridW, m_gridH});
    m_scroll->m_contentLayer->addChild(m_contentNode);

    m_countLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_countLabel->setScale(0.3f);
    m_countLabel->setAnchorPoint({1.f, 1.f});
    m_countLabel->setColor({100, 100, 100});
    m_mainLayer->addChildAtPosition(m_countLabel, Anchor::TopRight, {-PAD - 2, -2.f});
    m_countLabel->setZOrder(4);

    updateTabHighlights();
    switchTab(Tab::All);

    // Remember the background dim level set by the base popup so the entrance /
    // exit can cross-fade it (captured here because it is reliably set by init).
    m_dimOpacity = this->getOpacity();

    this->scheduleUpdate();

    return true;
}

void EmotePickerPopup::onExit() {
    paimon::ui::detachGeodeTextInput(m_textInput);
    paimon::ui::detachGeodeTextInput(m_searchInput);
    m_textInput = nullptr;
    m_searchInput = nullptr;
    paimon::popupblur::cleanup(this);
    Popup::onExit();
}

void EmotePickerPopup::switchTab(Tab tab) {
    m_activeTab = tab;
    m_activeCategory.clear();
    updateTabHighlights();

    if (tab == Tab::All) {
        m_catScroll->setVisible(false);
        buildAllEmotesGrid();
    } else {
        m_catScroll->setVisible(true);
        rebuildCategorySidebar();
    }
}

void EmotePickerPopup::updateTabHighlights() {
    auto setTabBg = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        auto container = btn->getNormalImage();
        if (!container) return;
        if (auto old = container->getChildByID("paimon-tab-bg"_spr))
            old->removeFromParent();
        float w = container->getContentSize().width;
        float h = container->getContentSize().height;
        ccColor4F col = active ? COL_TAB_ACTIVE : COL_TAB_INACTIVE;
        auto hl = paimon::SpriteHelper::createRoundedRect(w, h, 4.f, col);
        hl->setID("paimon-tab-bg"_spr);
        hl->setPosition({0, 0});
        container->addChild(hl, -1);
    };
    setTabBg(m_btnAll,    m_activeTab == Tab::All);
    setTabBg(m_btnStatic, m_activeTab == Tab::Stickers);
    setTabBg(m_btnGif,    m_activeTab == Tab::GIFs);
}

void EmotePickerPopup::onTabAll(CCObject*)    { switchTab(Tab::All); }
void EmotePickerPopup::onTabStatic(CCObject*) { switchTab(Tab::Stickers); }
void EmotePickerPopup::onTabGif(CCObject*)    { switchTab(Tab::GIFs); }

void EmotePickerPopup::rebuildCategorySidebar() {
    m_catMenu->removeAllChildren();

    auto type = (m_activeTab == Tab::GIFs) ? EmoteType::Gif : EmoteType::Static;
    auto cats = EmoteService::get().getCategories(type);

    float btnH    = 18.f;
    float totalH  = cats.size() * (btnH + 2);
    float scrollH = m_catScroll->getContentSize().height;
    float contentH = std::max(totalH, scrollH);

    m_catScroll->m_contentLayer->setContentSize({SIDEBAR_W, contentH});
    m_catMenu->setContentSize({SIDEBAR_W, contentH});

    float y = contentH;
    bool first = true;

    for (auto& cat : cats) {
        y -= btnH + 2;

        float bw = SIDEBAR_W - 4;
        auto lbl = CCLabelBMFont::create(cat.c_str(), "chatFont.fnt");
        lbl->setScale(0.30f);
        lbl->setAnchorPoint({0.f, 0.5f});

        auto container = CCNode::create();
        container->setContentSize({bw, btnH});
        lbl->setPosition({4, btnH / 2});
        container->addChild(lbl, 1);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(EmotePickerPopup::onCategoryClicked));
        btn->setPosition({SIDEBAR_W / 2, y + btnH / 2});
        btn->setTag(static_cast<int>(
            std::hash<std::string>{}(cat) & 0x7FFFFFFF));
        m_catMenu->addChild(btn);

        if (first) {
            m_activeCategory = cat;
            first = false;
        }
    }

    m_catScroll->moveToTop();

    if (!m_activeCategory.empty()) {
        selectCategory(m_activeCategory);
    } else {
        auto emotes = (type == EmoteType::Gif)
            ? EmoteService::get().getGifEmotes()
            : EmoteService::get().getStaticEmotes();
        buildEmoteGrid(emotes);
    }
}

void EmotePickerPopup::onCategoryClicked(CCObject* sender) {
    auto type = (m_activeTab == Tab::GIFs) ? EmoteType::Gif : EmoteType::Static;
    auto cats = EmoteService::get().getCategories(type);
    int tag = static_cast<CCNode*>(sender)->getTag();

    for (auto& cat : cats) {
        int catTag = static_cast<int>(
            std::hash<std::string>{}(cat) & 0x7FFFFFFF);
        if (catTag == tag) {
            selectCategory(cat);
            return;
        }
    }
}

void EmotePickerPopup::selectCategory(std::string const& cat) {
    m_activeCategory = cat;

    auto type = (m_activeTab == Tab::GIFs) ? EmoteType::Gif : EmoteType::Static;
    auto emotes = EmoteService::get().getEmotesByCategory(type, cat);
    buildEmoteGrid(emotes);

    if (!m_catMenu) return;
    int selTag = static_cast<int>(
        std::hash<std::string>{}(cat) & 0x7FFFFFFF);

    for (auto* child : CCArrayExt<CCNode*>(m_catMenu->getChildren())) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(child);
        auto container = item->getNormalImage();
        if (!container) continue;
        if (auto old = container->getChildByID("paimon-cat-hl"_spr))
            old->removeFromParent();
        if (child->getTag() == selTag) {
            float w = container->getContentSize().width;
            float h = container->getContentSize().height;
            auto hl = paimon::SpriteHelper::createRoundedRect(
                w, h, 3.f, COL_CAT_HL);
            hl->setID("paimon-cat-hl"_spr);
            hl->setPosition({0, 0});
            container->addChild(hl, -1);
        }
    }
}

static cocos2d::CCNode* makeEmoteCellContainer(CCLayerColor*& outHover) {
    auto container = CCNode::create();
    container->setContentSize({CELL_SIZE, CELL_SIZE});

    auto cellBg = CCLayerColor::create({36, 36, 36, 204},
                                        CELL_SIZE, CELL_SIZE);
    cellBg->setPosition({0, 0});
    container->addChild(cellBg, 0);

    auto hoverBg = CCLayerColor::create({115, 199, 242, 0},
                                         CELL_SIZE, CELL_SIZE);
    hoverBg->setPosition({0, 0});
    hoverBg->setTag(97);
    container->addChild(hoverBg, 1);
    outHover = hoverBg;

    return container;
}

void EmotePickerPopup::buildEmoteGrid(
        std::vector<EmoteInfo> const& emotes) {
    m_contentNode->removeAllChildren();
    m_hoverCells.clear();
    ++m_gridGeneration;

    float gridW = m_scroll->getContentSize().width;
    int cols = std::max(1,
        static_cast<int>((gridW + CELL_GAP) / (CELL_SIZE + CELL_GAP)));
    int rows = (static_cast<int>(emotes.size()) + cols - 1) / cols;
    float contentH = rows * (CELL_SIZE + CELL_GAP);
    float scrollH  = m_scroll->getContentSize().height;
    float totalH   = std::max(contentH, scrollH);

    m_contentNode->setContentSize({gridW, totalH});
    m_scroll->m_contentLayer->setContentSize({gridW, totalH});

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({gridW, totalH});
    m_contentNode->addChild(menu);

    m_hoverCells.reserve(emotes.size());
    for (size_t i = 0; i < emotes.size(); ++i) {
        // Manual grid positioning (no RowLayout). RowLayout proved unreliable
        // here — cells in some sections ended up unplaced/offscreen. This mirrors
        // the proven CustomBadgePickerPopup layout.
        int col = static_cast<int>(i % static_cast<size_t>(cols));
        int row = static_cast<int>(i / static_cast<size_t>(cols));
        float x = static_cast<float>(col) * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2.f;
        float y = totalH - (static_cast<float>(row) * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2.f);

        CCLayerColor* hoverLayer = nullptr;
        auto container = makeEmoteCellContainer(hoverLayer);

        auto ph = CCLabelBMFont::create("...", "chatFont.fnt");
        ph->setScale(0.3f);
        ph->setPosition({CELL_SIZE / 2, CELL_SIZE / 2});
        ph->setTag(99);
        container->addChild(ph, 5);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(EmotePickerPopup::onEmoteClicked));
        btn->setUserObject(CCString::create(emotes[i].name));
        btn->setPosition({x, y});
        menu->addChild(btn);

        HoverCell hc;
        hc.btn = btn;
        hc.hoverLayer = hoverLayer;
        hc.container = container;
        hc.info = emotes[i];
        hc.placeholder = ph;
        m_hoverCells.push_back(std::move(hc));
    }

    m_scroll->moveToTop();
    m_countLabel->setString(fmt::format("{}", emotes.size()).c_str());

    // Defer thumbnail loads a tick: grids build during init() before positioning, so world-space is wrong here.
    WeakRef<EmotePickerPopup> selfWeak = this;
    Loader::get()->queueInMainThread([selfWeak]() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = selfWeak.lock();
        if (!self) return;
        self->requestAllThumbnails();
    });
}

void EmotePickerPopup::buildAllEmotesGrid() {
    m_contentNode->removeAllChildren();
    m_hoverCells.clear();
    ++m_gridGeneration;

    float gridW = m_scroll->getContentSize().width;
    int cols = std::max(1,
        static_cast<int>((gridW + CELL_GAP) / (CELL_SIZE + CELL_GAP)));

    auto cats = EmoteService::get().getAllCategories();

    std::vector<std::pair<std::string, std::vector<EmoteInfo>>> grouped;
    grouped.reserve(cats.size());
    size_t totalEmotes = 0;
    float totalH = 0.f;
    for (auto const& cat : cats) {
        auto emotes = EmoteService::get().getAllEmotesByCategory(cat);
        if (emotes.empty()) continue;
        int catRows = (static_cast<int>(emotes.size()) + cols - 1) / cols;
        totalH += CAT_HDR_H + catRows * (CELL_SIZE + CELL_GAP) + CAT_GAP;
        totalEmotes += emotes.size();
        grouped.emplace_back(cat, std::move(emotes));
    }

    float scrollH = m_scroll->getContentSize().height;
    totalH = std::max(totalH, scrollH);

    m_contentNode->setContentSize({gridW, totalH});
    m_scroll->m_contentLayer->setContentSize({gridW, totalH});

    m_hoverCells.reserve(totalEmotes);

    // Single menu for all cells; manual positioning per category section.
    // (RowLayout per-category proved unreliable — some sections rendered headers
    // but their cells were never placed/visible.)
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({gridW, totalH});
    m_contentNode->addChild(menu, 1);

    float curY = totalH;

    for (auto const& [cat, emotes] : grouped) {
        curY -= CAT_HDR_H;
        auto hdr = CCLabelBMFont::create(cat.c_str(), "chatFont.fnt");
        hdr->setScale(0.35f);
        hdr->setAnchorPoint({0.f, 0.5f});
        hdr->setPosition({4.f, curY + CAT_HDR_H / 2.f});
        hdr->setColor({150, 150, 150});
        m_contentNode->addChild(hdr, 2);

        auto sep = CCLayerColor::create({46, 46, 46, 153}, gridW - 8, 1.f);
        sep->setPosition({4.f, curY});
        m_contentNode->addChild(sep, 2);

        int catRows = (static_cast<int>(emotes.size()) + cols - 1) / cols;
        float catGridH = catRows * (CELL_SIZE + CELL_GAP);
        float sectionTop = curY;

        for (size_t i = 0; i < emotes.size(); ++i) {
            int col = static_cast<int>(i % static_cast<size_t>(cols));
            int row = static_cast<int>(i / static_cast<size_t>(cols));
            float x = static_cast<float>(col) * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2.f;
            float y = sectionTop - (static_cast<float>(row) * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2.f);

            CCLayerColor* hoverLayer = nullptr;
            auto container = makeEmoteCellContainer(hoverLayer);

            auto ph = CCLabelBMFont::create("...", "chatFont.fnt");
            ph->setScale(0.3f);
            ph->setPosition({CELL_SIZE / 2, CELL_SIZE / 2});
            ph->setTag(99);
            container->addChild(ph, 5);

            auto btn = CCMenuItemSpriteExtra::create(
                container, this,
                menu_selector(EmotePickerPopup::onEmoteClicked));
            btn->setUserObject(CCString::create(emotes[i].name));
            btn->setPosition({x, y});
            menu->addChild(btn);

            HoverCell hc;
            hc.btn = btn;
            hc.hoverLayer = hoverLayer;
            hc.container = container;
            hc.info = emotes[i];
            hc.placeholder = ph;
            m_hoverCells.push_back(std::move(hc));
        }

        curY -= catGridH + CAT_GAP;
    }

    m_scroll->moveToTop();
    m_countLabel->setString(fmt::format("{}", totalEmotes).c_str());

    // Defer thumbnail loads a tick (same reason as buildEmoteGrid()).
    WeakRef<EmotePickerPopup> selfWeak = this;
    Loader::get()->queueInMainThread([selfWeak]() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = selfWeak.lock();
        if (!self) return;
        self->requestAllThumbnails();
    });
}

void EmotePickerPopup::onEmoteClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!isInsideVisibleScroll(btn)) return;
    auto nameObj = static_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;
    insertEmoteAtCursor(nameObj->getCString());
}

void EmotePickerPopup::onInputTextChanged(std::string const& text) {
    if (m_onTextChanged) m_onTextChanged(text);
    updateRenderPreview();
}

void EmotePickerPopup::updateRenderPreview() {
    if (!m_renderPreview) return;
    m_renderPreview->removeAllChildren();

    std::string text;
    if (m_textInput) text = m_textInput->getString();

    float contentW = m_popupW - PAD * 2;

    if (text.empty()) {
        auto lbl = CCLabelBMFont::create("Preview...", "chatFont.fnt");
        lbl->setScale(0.39f);
        lbl->setColor({100, 100, 100});
        lbl->setPosition({contentW / 2, PREVIEW_H / 2});
        m_renderPreview->addChild(lbl);
        return;
    }

    auto rendered = EmoteRenderer::renderComment(
        text, 0.f, contentW - 8, "chatFont.fnt", 0.455f, true);
    if (rendered) {
        rendered->setAnchorPoint({0.f, 1.f});
        rendered->setPosition({4.f, PREVIEW_H - 4.f});
        m_renderPreview->addChild(rendered);
    }
}

void EmotePickerPopup::insertEmoteAtCursor(std::string const& emoteName) {
    if (!m_textInput) return;
    std::string current = m_textInput->getString();
    std::string emoteText = fmt::format(":{}:", emoteName);
    std::string newText = current + emoteText;

    if (static_cast<int>(newText.size()) > m_charLimit) return;

    m_textInput->setString(newText);
    onInputTextChanged(newText);
}

void EmotePickerPopup::refreshGrid() {
    if (m_searchActive) {
        buildSearchResultsGrid();
        return;
    }
    if (m_activeTab == Tab::All) {
        buildAllEmotesGrid();
    } else if (!m_activeCategory.empty()) {
        selectCategory(m_activeCategory);
    } else {
        auto type = (m_activeTab == Tab::GIFs)
            ? EmoteType::Gif : EmoteType::Static;
        auto emotes = (type == EmoteType::Gif)
            ? EmoteService::get().getGifEmotes()
            : EmoteService::get().getStaticEmotes();
        buildEmoteGrid(emotes);
    }
}

void EmotePickerPopup::updateRefreshButtonState() {
    if (!m_refreshBtn) return;

    bool enabled = !m_isRefreshingCatalog;
    m_refreshBtn->setEnabled(enabled);
    m_refreshBtn->setOpacity(enabled ? 255 : 120);

    if (auto normal = typeinfo_cast<CCSprite*>(m_refreshBtn->getNormalImage())) {
        normal->setOpacity(enabled ? 255 : 120);
    }
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
                popup->refreshGrid();
                EmoteCache::get().preloadAllToDisk();
                PaimonNotify::create("Catalogo de emotes actualizado.", NotificationIcon::Success)->show();
            } else {
                PaimonNotify::create("No se pudo actualizar el catalogo de emotes.", NotificationIcon::Error)->show();
            }
        });
    });
}

void EmotePickerPopup::rebuildScrollArea() {
    // Reserved for future use
}

void EmotePickerPopup::onSearchToggle(CCObject*) {
    m_searchActive = !m_searchActive;

    if (m_searchBtn) {
        m_searchBtn->stopAllActions();
        m_searchBtn->setScale(1.0f);
        m_searchBtn->runAction(CCSequence::create(
            CCEaseSineOut::create(CCScaleTo::create(0.08f, 0.82f)),
            CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.0f)),
            nullptr
        ));
    }

    auto crossfadeIn = [](CCNode* node) {
        if (!node) return;
        node->stopAllActions();
        node->setVisible(true);
        if (auto rgba = typeinfo_cast<CCLayerColor*>(node)) {
            rgba->setOpacity(0);
            rgba->runAction(CCFadeTo::create(0.18f, 255));
        }
    };
    auto fadeOut = [](CCNode* node) {
        if (!node) return;
        node->stopAllActions();
        if (auto rgba = typeinfo_cast<CCLayerColor*>(node)) {
            rgba->runAction(CCSequence::create(
                CCFadeTo::create(0.12f, 0),
                CCCallFunc::create(node, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
        } else {
            node->setVisible(false);
        }
    };
    (void)crossfadeIn; (void)fadeOut;

    if (m_searchInputBg) m_searchInputBg->setVisible(m_searchActive);
    if (m_searchInput) m_searchInput->setVisible(m_searchActive);
    if (m_renderPreviewBg) m_renderPreviewBg->setVisible(!m_searchActive);
    if (m_renderPreview) m_renderPreview->setVisible(!m_searchActive);

    if (m_searchActive) {
        if (m_typeMenu) m_typeMenu->setVisible(false);
        if (m_catScroll) m_catScroll->setVisible(false);

        if (m_scroll) {
            float fullX = PAD + 4;
            float fullW = m_popupW - PAD * 2 - 8;
            m_scroll->setPosition({fullX, m_botY + 1.f});
            m_scroll->setContentSize({fullW, m_gridH});
            if (m_scroll->m_contentLayer) {
                m_scroll->m_contentLayer->setContentSize({fullW, m_gridH});
            }

            if (m_scroll->m_contentLayer) {
                m_scroll->m_contentLayer->stopAllActions();
                m_scroll->m_contentLayer->setScale(0.97f);
                m_scroll->m_contentLayer->runAction(
                    CCEaseBackOut::create(CCScaleTo::create(0.20f, 1.0f)));
            }
        }

        if (m_searchInput) m_searchInput->setString("");
        m_searchQuery.clear();
        buildSearchResultsGrid();
    } else {
        if (m_typeMenu) m_typeMenu->setVisible(true);
        if (m_activeTab != Tab::All && m_catScroll) m_catScroll->setVisible(true);

        if (m_scroll) {
            m_scroll->setPosition({m_gridX, m_botY + 1.f});
            m_scroll->setContentSize({m_gridW, m_gridH});
            if (m_scroll->m_contentLayer) {
                m_scroll->m_contentLayer->setContentSize({m_gridW, m_gridH});
            }

            if (m_scroll->m_contentLayer) {
                m_scroll->m_contentLayer->stopAllActions();
                m_scroll->m_contentLayer->setScale(0.97f);
                m_scroll->m_contentLayer->runAction(
                    CCEaseBackOut::create(CCScaleTo::create(0.20f, 1.0f)));
            }
        }

        m_searchQuery.clear();
        if (m_activeTab == Tab::All) {
            buildAllEmotesGrid();
        } else if (!m_activeCategory.empty()) {
            selectCategory(m_activeCategory);
        } else {
            rebuildCategorySidebar();
        }
    }
}

void EmotePickerPopup::onSearchTextChanged(std::string const& text) {
    m_searchQuery = text;
    buildSearchResultsGrid();
}

void EmotePickerPopup::buildSearchResultsGrid() {
    std::vector<EmoteInfo> results;
    if (m_searchQuery.empty()) {
        auto cats = EmoteService::get().getAllCategories();
        results.reserve(128);
        for (auto const& cat : cats) {
            auto e = EmoteService::get().getAllEmotesByCategory(cat);
            results.insert(results.end(), e.begin(), e.end());
            if (results.size() >= 256) break;
        }
        if (results.size() > 256) results.resize(256);
    } else {
        results = EmoteService::get().searchEmotes(m_searchQuery, 96);
    }
    buildEmoteGrid(results);
}

void EmotePickerPopup::update(float dt) {
    if (m_hoverCells.empty() || !m_scroll) return;

    if ((m_hoverFrameSkip++ & 1) != 0) return;
    float effDt = dt * 2.f;

    CCPoint mouseGL = geode::cocos::getMousePos();
    CCPoint scrollWorld = m_scroll->convertToWorldSpace({0, 0});
    CCSize  scrollSize  = m_scroll->getContentSize();
    CCRect  scrollRect(scrollWorld.x, scrollWorld.y, scrollSize.width, scrollSize.height);
    bool mouseInScroll = scrollRect.containsPoint(mouseGL);

    float lerpAmt = std::min(1.f, effDt * 5.5f);

    float halfCell = CELL_SIZE * 0.5f;

    for (auto const& hc : m_hoverCells) {
        if (!hc.btn || !hc.btn->getParent() || !hc.hoverLayer) continue;

        CCPoint cellWorld = hc.btn->getParent()->convertToWorldSpace(hc.btn->getPosition());
        if (cellWorld.x + halfCell < scrollRect.getMinX() ||
            cellWorld.x - halfCell > scrollRect.getMaxX() ||
            cellWorld.y + halfCell < scrollRect.getMinY() ||
            cellWorld.y - halfCell > scrollRect.getMaxY()) {
            if (hc.hoverLayer->getOpacity() != 0) hc.hoverLayer->setOpacity(0);
            continue;
        }

        bool hovered = mouseInScroll &&
                       std::abs(mouseGL.x - cellWorld.x) <= halfCell &&
                       std::abs(mouseGL.y - cellWorld.y) <= halfCell;

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

    if ((m_lazyLoadFrameSkip++ % 3) == 0) {
        requestVisibleThumbnails();
    }
}

void EmotePickerPopup::requestVisibleThumbnails() {
    if (!m_scroll || m_hoverCells.empty()) return;

    CCPoint scrollWorld = m_scroll->convertToWorldSpace({0, 0});
    CCSize  scrollSize  = m_scroll->getContentSize();

    constexpr float PREFETCH_MARGIN = CELL_SIZE * 1.5f;
    CCRect viewport(scrollWorld.x - PREFETCH_MARGIN,
                    scrollWorld.y - PREFETCH_MARGIN,
                    scrollSize.width + PREFETCH_MARGIN * 2,
                    scrollSize.height + PREFETCH_MARGIN * 2);

    float halfCell = CELL_SIZE * 0.5f;

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

// Eager load every cell regardless of viewport visibility. The viewport-based
// lazy loader (requestVisibleThumbnails) can miss cells while the popup is still
// playing its open animation (world-space positions are unreliable during the
// scale-in), which left the grid showing only placeholders. Loading directly by
// cell index does not depend on positioning and mirrors the proven approach used
// by CustomBadgePickerPopup. EmoteCache de-dupes via RAM/disk caches, so this is
// cheap for already-cached emotes.
void EmotePickerPopup::requestAllThumbnails() {
    for (size_t i = 0; i < m_hoverCells.size(); ++i) {
        loadCellThumbnail(i);
    }
}

void EmotePickerPopup::loadCellThumbnail(size_t cellIdx) {
    if (cellIdx >= m_hoverCells.size()) return;
    auto& hc = m_hoverCells[cellIdx];
    if (hc.loadRequested) return;
    if (!hc.btn || !hc.container) return;

    hc.loadRequested = true;

    WeakRef<EmotePickerPopup> selfWeak = this;
    uint32_t gen = m_gridGeneration;
    // EmoteCache dispatches on the main thread, so attach directly.
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

    auto attachSpriteToCell = [](HoverCell& cell, CCNode* sprite) {
        if (!sprite || !cell.container) return;
        float maxD = CELL_SIZE - 6.f;
        float sc = maxD / std::max(sprite->getContentSize().width,
                                    sprite->getContentSize().height);
        sprite->setScale(sc);
        sprite->setPosition({CELL_SIZE / 2, CELL_SIZE / 2});
        cell.container->addChild(sprite, 2);
        if (cell.placeholder) cell.placeholder->setVisible(false);
        cell.loaded = true;
    };

    if (isGif && !gifData.empty()) {
        WeakRef<EmotePickerPopup> selfWeak = this;
        size_t idx = cellIdx;
        std::string key = hc.info.filename;
        uint32_t gen = m_gridGeneration;
        hc.loaded = true; // optimistic; will stay true if sprite arrives
        AnimatedGIFSprite::createAsync(gifData, key,
            [selfWeak, idx, gen](AnimatedGIFSprite* gifSprite) {
                auto self = selfWeak.lock();
                if (!self) return;
                if (self->m_gridGeneration != gen) return;
                if (idx >= self->m_hoverCells.size()) return;
                auto& cell = self->m_hoverCells[idx];
                if (!cell.container || !cell.btn || !cell.btn->getParent()) {
                    return;
                }
                if (!gifSprite) {
                    cell.loaded = false; // allow retry if grid still alive
                    return;
                }
                float maxD = CELL_SIZE - 6.f;
                float sc = maxD / std::max(gifSprite->getContentSize().width,
                                            gifSprite->getContentSize().height);
                gifSprite->setScale(sc);
                gifSprite->setPosition({CELL_SIZE / 2, CELL_SIZE / 2});
                cell.container->addChild(gifSprite, 2);
                if (cell.placeholder) cell.placeholder->setVisible(false);
            });
        return;
    }

    if (tex) {
        attachSpriteToCell(hc, CCSprite::createWithTexture(tex));
    }
}

bool EmotePickerPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto loc = touch->getLocation();
    auto local = m_mainLayer->convertToNodeSpace(loc);
    auto size = m_mainLayer->getContentSize();
    m_touchHitOutside = !CCRect(0, 0, size.width, size.height).containsPoint(local);
    return true;
}

void EmotePickerPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_touchHitOutside) {
        auto loc = touch->getLocation();
        auto local = m_mainLayer->convertToNodeSpace(loc);
        auto size = m_mainLayer->getContentSize();
        if (!CCRect(0, 0, size.width, size.height).containsPoint(local)) {
            onClose(nullptr);
            return;
        }
    }
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
    auto winSize = CCDirector::get()->getWinSize();
    float halfH = m_popupH * 0.5f;
    float y = std::clamp(halfH + bottomPadding, halfH, winSize.height - halfH);
    m_mainLayer->setPosition({winSize.width * 0.5f, y});
}

void EmotePickerPopup::positionCentered() {
    auto winSize = CCDirector::get()->getWinSize();
    m_mainLayer->setPosition({winSize.width * 0.5f, winSize.height * 0.5f});
}

// --- Entrance / exit animations -------------------------------------------
// A polished, fluid feel: the dimmed backdrop cross-fades while the body does a
// smooth single-overshoot zoom (EaseBack) on the way in and a quick anticipated
// scale-down on the way out. No elastic jelly, no hard pop.

void EmotePickerPopup::show() {
    FLAlertLayer::show();

    // Blur the scene behind the popup. Marked directly (not via the dynamic
    // popup flag) so the shared hook's entry animation doesn't fight this
    // popup's own zoom/cross-fade below.
    paimon::popupblur::captureAndApply(this);

    // Cross-fade the dim backdrop in (level captured in init()).
    this->stopActionByTag(kDimActionTag);
    this->setOpacity(0);
    auto dimIn = CCEaseSineOut::create(CCFadeTo::create(ANIM_DIM_IN, m_dimOpacity));
    dimIn->setTag(kDimActionTag);
    this->runAction(dimIn);

    // Body: smooth zoom-in with a soft overshoot.
    if (m_mainLayer) {
        m_mainLayer->stopActionByTag(kBodyActionTag);
        m_mainLayer->setScale(ANIM_IN_SCALE);
        auto bodyIn = CCEaseBackOut::create(CCScaleTo::create(ANIM_IN_DUR, 1.0f));
        bodyIn->setTag(kBodyActionTag);
        m_mainLayer->runAction(bodyIn);
    }
}

void EmotePickerPopup::onClose(CCObject*) {
    if (m_closing) return;
    m_closing = true;

    paimon::popupblur::cleanupWithFade(this, ANIM_DIM_OUT);

    // Dismiss the keyboard with the popup so it doesn't linger over the scene.
    paimon::ui::detachGeodeTextInput(m_textInput);
    paimon::ui::detachGeodeTextInput(m_searchInput);

    // Block further interaction while the exit plays.
    this->setTouchEnabled(false);

    this->stopActionByTag(kDimActionTag);
    auto dimOut = CCEaseSineIn::create(CCFadeTo::create(ANIM_DIM_OUT, 0));
    dimOut->setTag(kDimActionTag);
    this->runAction(dimOut);

    if (m_mainLayer) {
        m_mainLayer->stopActionByTag(kBodyActionTag);
        auto bodyOut = CCSequence::create(
            CCEaseBackIn::create(CCScaleTo::create(ANIM_OUT_DUR, ANIM_OUT_SCALE)),
            CCCallFunc::create(this, callfunc_selector(EmotePickerPopup::finishClose)),
            nullptr);
        bodyOut->setTag(kBodyActionTag);
        m_mainLayer->runAction(bodyOut);
    } else {
        finishClose();
    }
}

void EmotePickerPopup::finishClose() {
    // Base implementation posts CloseEvent and removes the popup from its parent.
    Popup::onClose(nullptr);
}

void EmotePickerPopup::closeAnimated() {
    onClose(nullptr);
}

} // namespace paimon::emotes
