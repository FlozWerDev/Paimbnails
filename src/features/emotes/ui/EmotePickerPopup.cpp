#include "EmotePickerPopup.hpp"
#include "../services/EmoteService.hpp"
#include "../services/EmoteCache.hpp"
#include "../EmoteRenderer.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/PaimonNotification.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::emotes {

// ── Layout constants ────────────────────────────────
static constexpr float POPUP_W   = 380.f;
static constexpr float POPUP_H   = 192.f;
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

// ── Monochromatic palette ───────────────────────────
static constexpr ccColor4F COL_BORDER      = {0.18f, 0.18f, 0.18f, 1.0f};
static constexpr ccColor4F COL_BG          = {0.07f, 0.07f, 0.07f, 0.97f};
static constexpr ccColor4F COL_INPUT_BG    = {0.05f, 0.05f, 0.05f, 1.0f};
static constexpr ccColor4F COL_PREVIEW_BG  = {0.06f, 0.06f, 0.06f, 0.9f};
static constexpr ccColor4F COL_BOTTOM_BG   = {0.09f, 0.09f, 0.09f, 1.0f};
static constexpr ccColor4F COL_TAB_ACTIVE  = {0.22f, 0.22f, 0.22f, 1.0f};
static constexpr ccColor4F COL_TAB_INACTIVE= {0.13f, 0.13f, 0.13f, 0.8f};
static constexpr ccColor4F COL_CELL_BG     = {0.14f, 0.14f, 0.14f, 0.8f};
static constexpr ccColor4F COL_CAT_HL      = {0.20f, 0.20f, 0.20f, 0.7f};
static constexpr ccColor4F COL_DIVIDER     = {0.22f, 0.22f, 0.22f, 0.5f};
static constexpr ccColor4F COL_SEPARATOR   = {0.18f, 0.18f, 0.18f, 0.6f};

// ── Factory ─────────────────────────────────────────
EmotePickerPopup* EmotePickerPopup::create(
        CopyableFunction<std::string()> getText,
        CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit) {
    auto ret = new EmotePickerPopup();
    if (ret && ret->init(std::move(getText), std::move(onTextChanged), charLimit)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ── Init ────────────────────────────────────────────
bool EmotePickerPopup::init(
        CopyableFunction<std::string()> getText,
        CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit) {
    m_getText = std::move(getText);
    m_onTextChanged = std::move(onTextChanged);
    m_charLimit = charLimit;

    if (!Popup::init(POPUP_W, POPUP_H))
        return false;

    // No close button — dismiss by clicking outside
    if (m_closeBtn) m_closeBtn->setVisible(false);

    // Replace default bg with custom dark rounded rect + border
    if (m_bgSprite) m_bgSprite->setVisible(false);

    auto border = paimon::SpriteHelper::createRoundedRect(
        POPUP_W + 2, POPUP_H + 2, CORNER_R + 1, COL_BORDER);
    border->setPosition({-1.f, -1.f});
    m_mainLayer->addChild(border, -2);

    auto bg = paimon::SpriteHelper::createRoundedRect(
        POPUP_W, POPUP_H, CORNER_R, COL_BG);
    bg->setPosition({0.f, 0.f});
    m_mainLayer->addChild(bg, -1);

    float contentW = POPUP_W - PAD * 2;

    // ══════════ Input section (top) ══════════
    float inputY = POPUP_H - PAD - INPUT_H;
    float inputActionTotal = INPUT_ACTION_W + INPUT_ACTION_GAP;
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
    m_textInput->setCallback([this](std::string const& text) {
        onInputTextChanged(text);
    });
    m_mainLayer->addChild(m_textInput, 2);

    auto inputActionBg = paimon::SpriteHelper::createRoundedRect(
        INPUT_ACTION_W, INPUT_H, 6.f, COL_INPUT_BG);
    inputActionBg->setPosition({PAD + inputBoxW + INPUT_ACTION_GAP, inputY});
    m_mainLayer->addChild(inputActionBg, 1);

    auto refreshMenu = CCMenu::create();
    refreshMenu->setPosition({0, 0});
    m_mainLayer->addChild(refreshMenu, 3);

    auto refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
    if (!refreshSpr) refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png");
    if (refreshSpr) {
        refreshSpr->setScale(0.38f);
        m_refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSpr, this,
            menu_selector(EmotePickerPopup::onRefreshCatalog));
        m_refreshBtn->setPosition({
            PAD + inputBoxW + INPUT_ACTION_GAP + INPUT_ACTION_W / 2.f,
            inputY + INPUT_H / 2.f
        });
        refreshMenu->addChild(m_refreshBtn);
        updateRefreshButtonState();
    }

    // Seed from game input
    if (m_getText) {
        std::string current = m_getText();
        if (!current.empty()) {
            m_textInput->setString(current);
        }
    }

    // ══════════ Preview render section ══════════
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

    // ══════════ Bottom section (tabs + grid) ══════════
    float botH = previewY - PAD - PAD;
    m_botY = PAD;
    float botW = contentW;

    auto botBg = paimon::SpriteHelper::createRoundedRect(
        botW, botH, 8.f, COL_BOTTOM_BG);
    botBg->setPosition({PAD, m_botY});
    m_mainLayer->addChild(botBg, 1);

    // ── Left sidebar: type tabs + category list ──
    float sideX = PAD + 4;

    m_typeMenu = CCMenu::create();
    m_typeMenu->setPosition({0, 0});
    m_mainLayer->addChild(m_typeMenu, 3);

    auto makeTabBtn = [&](const char* text, SEL_MenuHandler sel,
                          float yOff) -> CCMenuItemSpriteExtra* {
        float bw = SIDEBAR_W - 8;
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.3f);
        auto container = CCNode::create();
        container->setContentSize({bw, TAB_H});
        lbl->setPosition({bw / 2, TAB_H / 2});
        container->addChild(lbl, 1);
        auto btn = CCMenuItemSpriteExtra::create(container, this, sel);
        btn->setPosition({sideX + SIDEBAR_W / 2, m_botY + botH - yOff});
        return btn;
    };

    m_btnAll = makeTabBtn("All",
        menu_selector(EmotePickerPopup::onTabAll), TAB_H / 2 + 4);
    m_typeMenu->addChild(m_btnAll);

    m_btnStatic = makeTabBtn("Static",
        menu_selector(EmotePickerPopup::onTabStatic), TAB_H * 1.5f + 8);
    m_typeMenu->addChild(m_btnStatic);

    m_btnGif = makeTabBtn("GIF",
        menu_selector(EmotePickerPopup::onTabGif), TAB_H * 2.5f + 12);
    m_typeMenu->addChild(m_btnGif);

    // Category scroll (below the three tab buttons)
    float catTop = m_botY + botH - TAB_H * 3 - 14.f;
    float catH   = catTop - m_botY - 3.f;

    m_catScroll = ScrollLayer::create({SIDEBAR_W, catH});
    m_catScroll->setPosition({sideX, m_botY + 3.f});
    m_mainLayer->addChild(m_catScroll, 3);

    m_catMenu = CCMenu::create();
    m_catMenu->setPosition({0, 0});
    m_catScroll->m_contentLayer->addChild(m_catMenu);

    // ── Vertical divider ──
    float divX = PAD + SIDEBAR_W + 4;
    auto divider = paimon::SpriteHelper::createRoundedRect(
        1.5f, botH - 6.f, 1.f, COL_DIVIDER);
    divider->setPosition({divX, m_botY + 3.f});
    m_mainLayer->addChild(divider, 2);

    // ── Right side: emote grid ──
    m_gridX = divX + 6;
    m_gridW = PAD + botW - m_gridX + PAD;
    m_gridH = botH - 3.f;

    m_scroll = ScrollLayer::create({m_gridW, m_gridH});
    m_scroll->setPosition({m_gridX, m_botY + 1.f});
    m_mainLayer->addChild(m_scroll, 3);

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({m_gridW, m_gridH});
    m_scroll->m_contentLayer->addChild(m_contentNode);

    // Count label (top-right corner)
    m_countLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_countLabel->setScale(0.3f);
    m_countLabel->setAnchorPoint({1.f, 1.f});
    m_countLabel->setPosition({POPUP_W - PAD - 2, POPUP_H - 2});
    m_countLabel->setColor({100, 100, 100});
    m_mainLayer->addChild(m_countLabel, 4);

    // Initial data
    updateTabHighlights();
    switchTab(Tab::All);

    return true;
}

// ── Tab switching ───────────────────────────────────
void EmotePickerPopup::switchTab(Tab tab) {
    m_activeTab = tab;
    m_activeCategory.clear();
    updateTabHighlights();

    if (tab == Tab::All) {
        // Hide category sidebar for All tab
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
        if (auto old = container->getChildByTag(50))
            old->removeFromParent();
        float w = container->getContentSize().width;
        float h = container->getContentSize().height;
        ccColor4F col = active ? COL_TAB_ACTIVE : COL_TAB_INACTIVE;
        auto hl = paimon::SpriteHelper::createRoundedRect(w, h, 4.f, col);
        hl->setTag(50);
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

// ── Category sidebar ────────────────────────────────
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

    // Highlight the selected category
    if (!m_catMenu) return;
    int selTag = static_cast<int>(
        std::hash<std::string>{}(cat) & 0x7FFFFFFF);

    for (auto* child : CCArrayExt<CCNode*>(m_catMenu->getChildren())) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(child);
        auto container = item->getNormalImage();
        if (!container) continue;
        if (auto old = container->getChildByTag(51))
            old->removeFromParent();
        if (child->getTag() == selTag) {
            float w = container->getContentSize().width;
            float h = container->getContentSize().height;
            auto hl = paimon::SpriteHelper::createRoundedRect(
                w, h, 3.f, COL_CAT_HL);
            hl->setTag(51);
            hl->setPosition({0, 0});
            container->addChild(hl, -1);
        }
    }
}

// ── Emote grid (filtered by tab/category) ──────────
void EmotePickerPopup::buildEmoteGrid(
        std::vector<EmoteInfo> const& emotes) {
    m_contentNode->removeAllChildren();

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

    for (size_t i = 0; i < emotes.size(); ++i) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        float x = col * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2;
        float y = totalH - (row * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2);

        auto cellBg = paimon::SpriteHelper::createRoundedRect(
            CELL_SIZE, CELL_SIZE, 4.f, COL_CELL_BG);

        auto container = CCNode::create();
        container->setContentSize({CELL_SIZE, CELL_SIZE});
        cellBg->setPosition({0, 0});
        container->addChild(cellBg);

        auto ph = CCLabelBMFont::create("...", "chatFont.fnt");
        ph->setScale(0.3f);
        ph->setPosition({CELL_SIZE / 2, CELL_SIZE / 2});
        ph->setTag(99);
        container->addChild(ph, 1);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(EmotePickerPopup::onEmoteClicked));
        btn->setPosition({x, y});
        btn->setUserObject(CCString::create(emotes[i].name));
        menu->addChild(btn);

        // Async thumbnail load
        auto emoteCopy = emotes[i];
        Ref<CCMenuItemSpriteExtra> btnRef = btn;
        EmoteCache::get().loadEmote(emoteCopy,
            [btnRef](CCTexture2D* tex, bool isGif,
                     std::vector<uint8_t> const& gifData) {
                Loader::get()->queueInMainThread(
                    [btnRef, tex, isGif, gifData]() {
                        if (!btnRef || !btnRef->getParent()) return;
                        auto cont = btnRef->getNormalImage();
                        if (!cont) return;

                        CCNode* sprite = nullptr;
                        if (isGif && !gifData.empty()) {
                            sprite = AnimatedGIFSprite::create(
                                gifData.data(), gifData.size());
                        } else if (tex) {
                            sprite = CCSprite::createWithTexture(tex);
                        }
                        if (sprite) {
                            float maxD = CELL_SIZE - 6.f;
                            float sc = maxD / std::max(
                                sprite->getContentSize().width,
                                sprite->getContentSize().height);
                            sprite->setScale(sc);
                            sprite->setPosition(
                                {CELL_SIZE / 2, CELL_SIZE / 2});
                            cont->addChild(sprite, 2);
                            if (auto p = cont->getChildByTag(99))
                                p->setVisible(false);
                        }
                    });
            });
    }

    m_scroll->moveToTop();
    m_countLabel->setString(fmt::format("{}", emotes.size()).c_str());
}

// ── All emotes grid (grouped by category) ──────────
void EmotePickerPopup::buildAllEmotesGrid() {
    m_contentNode->removeAllChildren();

    float gridW = m_scroll->getContentSize().width;
    int cols = std::max(1,
        static_cast<int>((gridW + CELL_GAP) / (CELL_SIZE + CELL_GAP)));

    auto cats = EmoteService::get().getAllCategories();

    // First pass: compute total height
    float totalH = 0.f;
    size_t totalEmotes = 0;
    for (auto const& cat : cats) {
        auto emotes = EmoteService::get().getAllEmotesByCategory(cat);
        int catRows = (static_cast<int>(emotes.size()) + cols - 1) / cols;
        totalH += CAT_HDR_H + catRows * (CELL_SIZE + CELL_GAP) + CAT_GAP;
        totalEmotes += emotes.size();
    }

    float scrollH = m_scroll->getContentSize().height;
    totalH = std::max(totalH, scrollH);

    m_contentNode->setContentSize({gridW, totalH});
    m_scroll->m_contentLayer->setContentSize({gridW, totalH});

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({gridW, totalH});
    m_contentNode->addChild(menu, 1);

    float curY = totalH;

    for (auto const& cat : cats) {
        auto emotes = EmoteService::get().getAllEmotesByCategory(cat);
        if (emotes.empty()) continue;

        // Category header
        curY -= CAT_HDR_H;
        auto hdr = CCLabelBMFont::create(cat.c_str(), "chatFont.fnt");
        hdr->setScale(0.35f);
        hdr->setAnchorPoint({0.f, 0.5f});
        hdr->setPosition({4.f, curY + CAT_HDR_H / 2.f});
        hdr->setColor({150, 150, 150});
        m_contentNode->addChild(hdr, 2);

        // Thin separator line under header
        auto sep = paimon::SpriteHelper::createRoundedRect(
            gridW - 8, 1.f, 0.5f, COL_SEPARATOR);
        sep->setPosition({4.f, curY});
        m_contentNode->addChild(sep, 2);

        // Emote cells for this category
        int catRows = (static_cast<int>(emotes.size()) + cols - 1) / cols;

        for (size_t i = 0; i < emotes.size(); ++i) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;
            float x = col * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2;
            float y = curY - (row * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2);

            auto cellBg = paimon::SpriteHelper::createRoundedRect(
                CELL_SIZE, CELL_SIZE, 4.f, COL_CELL_BG);

            auto container = CCNode::create();
            container->setContentSize({CELL_SIZE, CELL_SIZE});
            cellBg->setPosition({0, 0});
            container->addChild(cellBg);

            auto ph = CCLabelBMFont::create("...", "chatFont.fnt");
            ph->setScale(0.3f);
            ph->setPosition({CELL_SIZE / 2, CELL_SIZE / 2});
            ph->setTag(99);
            container->addChild(ph, 1);

            auto btn = CCMenuItemSpriteExtra::create(
                container, this,
                menu_selector(EmotePickerPopup::onEmoteClicked));
            btn->setPosition({x, y});
            btn->setUserObject(CCString::create(emotes[i].name));
            menu->addChild(btn);

            // Async thumbnail load
            auto emoteCopy = emotes[i];
            Ref<CCMenuItemSpriteExtra> btnRef = btn;
            EmoteCache::get().loadEmote(emoteCopy,
                [btnRef](CCTexture2D* tex, bool isGif,
                         std::vector<uint8_t> const& gifData) {
                    Loader::get()->queueInMainThread(
                        [btnRef, tex, isGif, gifData]() {
                            if (!btnRef || !btnRef->getParent()) return;
                            auto cont = btnRef->getNormalImage();
                            if (!cont) return;

                            CCNode* sprite = nullptr;
                            if (isGif && !gifData.empty()) {
                                sprite = AnimatedGIFSprite::create(
                                    gifData.data(), gifData.size());
                            } else if (tex) {
                                sprite = CCSprite::createWithTexture(tex);
                            }
                            if (sprite) {
                                float maxD = CELL_SIZE - 6.f;
                                float sc = maxD / std::max(
                                    sprite->getContentSize().width,
                                    sprite->getContentSize().height);
                                sprite->setScale(sc);
                                sprite->setPosition(
                                    {CELL_SIZE / 2, CELL_SIZE / 2});
                                cont->addChild(sprite, 2);
                                if (auto p = cont->getChildByTag(99))
                                    p->setVisible(false);
                            }
                        });
                });
        }

        curY -= catRows * (CELL_SIZE + CELL_GAP) + CAT_GAP;
    }

    m_scroll->moveToTop();
    m_countLabel->setString(fmt::format("{}", totalEmotes).c_str());
}

void EmotePickerPopup::onEmoteClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!isInsideVisibleScroll(btn)) return;
    auto nameObj = static_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;
    insertEmoteAtCursor(nameObj->getCString());
}

// ── Input / Preview sync ───────────────────────────
void EmotePickerPopup::onInputTextChanged(std::string const& text) {
    if (m_onTextChanged) m_onTextChanged(text);
    updateRenderPreview();
}

void EmotePickerPopup::updateRenderPreview() {
    if (!m_renderPreview) return;
    m_renderPreview->removeAllChildren();

    std::string text;
    if (m_textInput) text = m_textInput->getString();

    float contentW = POPUP_W - PAD * 2;

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

// ── Refresh ────────────────────────────────────────
void EmotePickerPopup::refreshGrid() {
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

// ── Touch handling ──────────────────────────────────
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
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float halfH = POPUP_H * 0.5f;
    float y = std::clamp(halfH + bottomPadding, halfH, winSize.height - halfH);
    m_mainLayer->setPosition({winSize.width * 0.5f, y});
}

} // namespace paimon::emotes
