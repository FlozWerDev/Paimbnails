#include "ProjectEditorLayer.hpp"
#include "FusionEditorLayer.hpp"

#include "../engine/ColorPresets.hpp"
#include "../engine/FusionAsset.hpp"
#include "../engine/FusionEngine.hpp"
#include "../engine/PackExporter.hpp"
#include "../engine/SelfTest.hpp"
#include "../engine/AutoTuner.hpp"
#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../persist/FusionStore.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "ParamSliderRow.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kMargin      = 6.f;
constexpr float kHeaderH     = 38.f;
constexpr float kFooterH     = 30.f;
constexpr float kLeftColW    = 112.f;
constexpr float kRightColW   = 216.f;
constexpr float kColGap      = 6.f;
constexpr float kTabRowH     = 22.f;
constexpr float kCardTitleH  = 18.f;

constexpr float kCellW = 70.f;
constexpr float kCellH = 56.f;

constexpr cocos2d::ccColor3B kAccent     {138,  96, 255};
constexpr cocos2d::ccColor3B kCardBorder { 74,  56, 118};
constexpr cocos2d::ccColor3B kCardFill   { 23,  19,  33};
constexpr cocos2d::ccColor3B kBarFill    { 33,  26,  50};
constexpr cocos2d::ccColor3B kInsetFill  { 30,  27,  40};

// Derived helpers — keep call sites readable.
inline float panelTopY(cocos2d::CCSize const& win) {
    return win.height - kHeaderH - kColGap;
}
inline float panelBottomY() { return kFooterH + kColGap; }
inline float centerX0()     { return kMargin + kLeftColW + kColGap; }
inline float rightPanelW()  { return kRightColW; }
inline float rightPanelX(float winW) {
    return winW - kMargin - kRightColW;
}
inline float centerRightX(float winW) {
    return rightPanelX(winW) - kColGap;
}

CCNode* makeCard(CCNode* parent, float x, float y, float w, float h) {
    auto* card = CCNode::create();
    card->setContentSize({w, h});
    card->setAnchorPoint({0.f, 0.f});
    card->setPosition({x, y});
    parent->addChild(card, 2);

    if (auto* border = CCScale9Sprite::create("GJ_square01.png")) {
        border->setContentSize({w, h});
        border->setColor(kCardBorder);
        border->setOpacity(170);
        card->addChildAtPosition(border, Anchor::Center);
    }
    if (auto* fill = CCScale9Sprite::create("GJ_square01.png")) {
        fill->setContentSize({w - 3.f, h - 3.f});
        fill->setColor(kCardFill);
        card->addChildAtPosition(fill, Anchor::Center);
    }
    return card;
}

void addCardTitle(CCNode* card, char const* text) {
    auto sz = card->getContentSize();
    if (auto* strip = CCScale9Sprite::create("GJ_square01.png")) {
        strip->setContentSize({sz.width - 3.f, kCardTitleH});
        strip->setColor(kBarFill);
        card->addChildAtPosition(strip, Anchor::Top,
            {0.f, -kCardTitleH * 0.5f - 1.5f});
    }
    if (auto* lbl = CCLabelBMFont::create(text, "goldFont.fnt")) {
        lbl->setScale(0.38f);
        card->addChildAtPosition(lbl, Anchor::Top,
            {0.f, -kCardTitleH * 0.5f - 1.5f});
    }
}

void addCardDivider(CCNode* card, float localY) {
    auto w = card->getContentSize().width;
    if (auto* line = CCLayerColor::create(
            ccc4(kCardBorder.r, kCardBorder.g, kCardBorder.b, 130))) {
        line->setContentSize({w - 10.f, 1.f});
        line->setPosition({5.f, localY});
        card->addChild(line, 1);
    }
}

char const* scopeLabel(TintScope s) {
    switch (s) {
        case TintScope::ButtonsAndMenuUi: return "Buttons + Menu UI";
        case TintScope::Everything:       return "Everything";
        case TintScope::ButtonsOnly:
        default:                          return "Buttons only";
    }
}

char const* fitModeLabel(ImageFitMode mode) {
    switch (mode) {
        case ImageFitMode::Fill:    return "Fill";
        case ImageFitMode::Stretch: return "Stretch";
        case ImageFitMode::Fit:
        default:                    return "Fit";
    }
}

char const* fusionBlendLabel(FusionBlendMode mode) {
    switch (mode) {
        case FusionBlendMode::Replace:      return "Replace";
        case FusionBlendMode::Overlay:      return "Overlay";
        case FusionBlendMode::MultiplyLuma:
        default:                            return "Luma";
    }
}

std::string percentFmt(float v) {
    return fmt::format("{:.0f}%", v * 100.f);
}

std::string toLowerCopy(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

void fitSpriteIntoBox(CCSprite* spr, float boxSize) {
    if (!spr) return;
    auto sz = spr->getContentSize();
    if (sz.width <= 0 || sz.height <= 0) return;
    float maxSide = boxSize - 12.f;
    float scale = std::min(maxSide / sz.width, maxSide / sz.height);
    spr->setScale(std::min(scale, 3.f));
}

CCSprite* makeSwatchSprite(ccColor3B color, float size) {
    auto* spr = CCSprite::create("square.png");
    if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_button_05.png");
    if (spr) {
        auto sz = spr->getContentSize();
        if (sz.width > 0 && sz.height > 0) {
            spr->setScale(size / std::max(sz.width, sz.height));
        }
        spr->setColor(color);
    }
    return spr;
}

}


ProjectEditorLayer* ProjectEditorLayer::create(std::string slotId) {
    auto* ret = new ProjectEditorLayer();
    if (ret->init(std::move(slotId))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

CCScene* ProjectEditorLayer::scene(std::string slotId) {
    auto* scene = CCScene::create();
    if (auto* layer = ProjectEditorLayer::create(std::move(slotId))) {
        scene->addChild(layer);
    }
    return scene;
}

void ProjectEditorLayer::open(std::string slotId) {
    if (auto* layer = ProjectEditorLayer::create(std::move(slotId))) {
        geode::pushSceneWithLayer(layer);
    }
}

ProjectEditorLayer::~ProjectEditorLayer() {
    m_closed->store(true, std::memory_order_release);
    m_previewGeneration->fetch_add(1, std::memory_order_acq_rel);
    m_renderGeneration->fetch_add(1, std::memory_order_acq_rel);
    m_thumbGeneration->fetch_add(1, std::memory_order_acq_rel);
    m_fusionAsset.reset();
    m_fusionMask.reset();
}

bool ProjectEditorLayer::init(std::string slotId) {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setID("texture-studio-editor"_spr);
    m_slotId = std::move(slotId);

    auto loaded = SlotStore::get().loadSlot(m_slotId);
    if (!loaded) {
        Notification::create(("Cannot load slot: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return true;
    }
    m_project = loaded.unwrap();

    buildBackground();
    buildHeader();
    buildPreviewPanel();
    buildBrowserPanel();
    buildTabsPanel();
    buildFooter();

    buildEntries();
    applyFilter();
    selectTab(0);

    startSelectionPixelLoad();

    return true;
}

void ProjectEditorLayer::keyBackClicked() {
    onBack(nullptr);
}

void ProjectEditorLayer::onBack(CCObject*) {
    (void)SlotStore::get().saveSlot(m_project);
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}

void ProjectEditorLayer::onOpenFusionLayer() {
    std::string frame;
    if (m_hasSelection) frame = m_selected.frameName;
    (void)SlotStore::get().saveSlot(m_project);
    FusionEditorLayer::open(m_slotId, frame);
}


void ProjectEditorLayer::buildBackground() {
    auto winSize = CCDirector::get()->getWinSize();

    auto* bg = CCLayerColor::create(ccc4(16, 14, 26, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -5);

    auto* gradient = CCLayerGradient::create(
        ccc4(44, 28, 66, 110), ccc4(8, 6, 16, 160));
    gradient->setContentSize(winSize);
    gradient->setVector({0, -1});
    this->addChild(gradient, -4);
}

void ProjectEditorLayer::buildHeader() {
    auto winSize = CCDirector::get()->getWinSize();
    const float headerY = winSize.height - kHeaderH * 0.5f;

    if (auto* bar = CCLayerColor::create(
            ccc4(kBarFill.r, kBarFill.g, kBarFill.b, 235))) {
        bar->setContentSize({winSize.width, kHeaderH});
        bar->setPosition({0.f, winSize.height - kHeaderH});
        this->addChild(bar, 3);
    }
    if (auto* line = CCLayerColor::create(
            ccc4(kAccent.r, kAccent.g, kAccent.b, 200))) {
        line->setContentSize({winSize.width, 1.5f});
        line->setPosition({0.f, winSize.height - kHeaderH});
        this->addChild(line, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    if (auto* backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        backSpr->setScale(0.72f);
        if (auto* backBtn = CCMenuItemExt::createSpriteExtra(backSpr,
                [this](CCMenuItemSpriteExtra*) { this->onBack(nullptr); })) {
            backBtn->setPosition({18.f, headerY});
            menu->addChild(backBtn);
        }
    }

    float titleRight = 40.f;
    if (auto* title = CCLabelBMFont::create(m_project.name.c_str(), "bigFont.fnt")) {
        title->setAnchorPoint({0.f, 0.5f});
        title->limitLabelWidth(150.f, 0.5f, 0.24f);
        title->setPosition({38.f, headerY + 1.f});
        this->addChild(title, 5);
        titleRight = 38.f + title->getScaledContentSize().width;
    }
    if (auto* sub = CCLabelBMFont::create("TEXTURE STUDIO", "goldFont.fnt")) {
        sub->setAnchorPoint({0.f, 0.5f});
        sub->setScale(0.28f);
        sub->setOpacity(190);
        sub->setPosition({titleRight + 8.f, headerY - 1.f});
        this->addChild(sub, 5);
    }

    float rightEdge = winSize.width - 10.f;
    auto placeRight = [&rightEdge, headerY, menu](CCMenuItemSpriteExtra* btn) {
        float bw = btn->getScaledContentSize().width;
        btn->setPosition({rightEdge - bw * 0.5f, headerY});
        menu->addChild(btn);
        rightEdge -= bw + 6.f;
    };

    if (auto* fusSpr = ButtonSprite::create("Fusion", "bigFont.fnt",
                                            "GJ_button_01.png", 0.32f)) {
        if (auto* fusBtn = CCMenuItemExt::createSpriteExtra(fusSpr,
                [this](CCMenuItemSpriteExtra*) { this->onOpenFusionLayer(); })) {
            placeRight(fusBtn);
        }
    }
    if (auto* autoSpr = ButtonSprite::create("Auto", "bigFont.fnt", "GJ_button_02.png", 0.32f)) {
        if (auto* autoBtn = CCMenuItemExt::createSpriteExtra(autoSpr,
                [this](CCMenuItemSpriteExtra*) { this->onAutoTune(nullptr); })) {
            placeRight(autoBtn);
        }
    }
}

void ProjectEditorLayer::buildFooter() {
    auto winSize = CCDirector::get()->getWinSize();
    const float barY = kFooterH * 0.5f;

    if (auto* bar = CCLayerColor::create(
            ccc4(kBarFill.r, kBarFill.g, kBarFill.b, 235))) {
        bar->setContentSize({winSize.width, kFooterH});
        bar->setPosition({0.f, 0.f});
        this->addChild(bar, 3);
    }
    if (auto* line = CCLayerColor::create(
            ccc4(kAccent.r, kAccent.g, kAccent.b, 200))) {
        line->setContentSize({winSize.width, 1.5f});
        line->setPosition({0.f, kFooterH});
        this->addChild(line, 3);
    }

    if (auto* status = CCLabelBMFont::create("Ready.", "bigFont.fnt")) {
        status->setScale(0.28f);
        status->setAnchorPoint({0.f, 0.5f});
        status->setColor({170, 190, 175});
        status->setPosition({10.f, barY});
        this->addChild(status, 5);
        m_statusLbl = status;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    float rightEdge = winSize.width - 10.f;
    auto placeRight = [&rightEdge, barY, menu](CCMenuItemSpriteExtra* btn) {
        float bw = btn->getScaledContentSize().width;
        btn->setPosition({rightEdge - bw * 0.5f, barY});
        menu->addChild(btn);
        rightEdge -= bw + 8.f;
    };

    if (auto* genSpr = ButtonSprite::create("Generate", "goldFont.fnt", "GJ_button_01.png", 0.34f)) {
        if (auto* genBtn = CCMenuItemExt::createSpriteExtra(genSpr,
                [this](CCMenuItemSpriteExtra*) { this->onGenerate(nullptr); })) {
            placeRight(genBtn);
            m_genBtn = genBtn;
        }
    }
    if (auto* saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_05.png", 0.34f)) {
        if (auto* saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr,
                [this](CCMenuItemSpriteExtra*) { this->onSave(nullptr); })) {
            placeRight(saveBtn);
            m_saveBtn = saveBtn;
        }
    }
}

void ProjectEditorLayer::buildPreviewPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    const float pb = panelBottomY();
    const float ph = panelTopY(winSize) - pb;
    auto* card = makeCard(this, kMargin, pb, kLeftColW, ph);
    addCardTitle(card, "Preview");

    const float cx = kMargin + kLeftColW * 0.5f;
    const float contentTop = pb + ph - kCardTitleH - 3.f;
    constexpr float kBox = 84.f;
    constexpr float kGap = 8.f;

    if (auto* nameLbl = CCLabelBMFont::create("(pack preview)", "chatFont.fnt")) {
        nameLbl->setScale(0.4f);
        nameLbl->setColor({185, 190, 200});
        nameLbl->limitLabelWidth(kLeftColW - 10.f, 0.4f, 0.18f);
        nameLbl->setPosition({cx, contentTop - 7.f});
        this->addChild(nameLbl, 5);
        m_previewNameLbl = nameLbl;
    }

    auto makeBox = [this, cx](float cy, char const* caption) -> CCNode* {
        auto* host = CCNode::create();
        host->setContentSize({kBox, kBox});
        host->setAnchorPoint({0.5f, 0.5f});
        host->setPosition({cx, cy});
        this->addChild(host, 5);

        if (auto* frame = CCScale9Sprite::create("GJ_square01.png")) {
            frame->setContentSize({kBox, kBox});
            frame->setColor(kInsetFill);
            host->addChildAtPosition(frame, Anchor::Center);
        }
        if (auto* lbl = CCLabelBMFont::create(caption, "bigFont.fnt")) {
            lbl->setScale(0.26f);
            lbl->setColor({170, 170, 180});
            host->addChildAtPosition(lbl, Anchor::Top, {0.f, 7.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.32f);
            loading->setColor({120, 120, 130});
            loading->setID("loading-label");
            host->addChildAtPosition(loading, Anchor::Center, {0.f, -3.f});
        }
        return host;
    };

    // Original → Result top-to-bottom (before / after comparison).
    const float origCy = contentTop - 14.f - kBox * 0.5f;
    const float resultCy = origCy - kBox - kGap;
    m_originalHost = makeBox(origCy, "Original");
    m_resultHost   = makeBox(resultCy, "Result");

    if (auto* coverage = CCLabelBMFont::create("Loading...", "chatFont.fnt")) {
        coverage->setScale(0.36f);
        coverage->setColor({185, 190, 200});
        coverage->limitLabelWidth(kLeftColW - 10.f, 0.36f, 0.16f);
        coverage->setPosition({cx, resultCy - kBox * 0.5f - 10.f});
        this->addChild(coverage, 5);
        m_coverageLbl = coverage;
    }
}

void ProjectEditorLayer::buildBrowserPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    const float c0 = centerX0();
    const float c1 = centerRightX(winSize.width);
    const float centerW = c1 - c0;
    const float pb = panelBottomY();
    const float ph = panelTopY(winSize) - pb;
    auto* card = makeCard(this, c0, pb, centerW, ph);

    const float toolbarY = pb + ph - 14.f;
    const float pageY = pb + 12.f;
    const float gridTop = toolbarY - 15.f;
    const float gridBottom = pageY + 12.f;
    const float gridH = std::max(80.f, gridTop - gridBottom);
    addCardDivider(card, ph - 28.f);
    addCardDivider(card, 24.f);

    constexpr float kSearchW = 100.f;
    if (auto* search = TextInput::create(kSearchW, "Search...")) {
        search->setMaxCharCount(32);
        search->setScale(0.7f);
        search->setID("sprite-search"_spr);
        search->setCallback([this](std::string const& text) {
            m_search = toLowerCopy(text);
            applyFilter();
        });
        search->setPosition({c0 + kSearchW * 0.5f * 0.7f + 8.f, toolbarY});
        this->addChild(search, 6);
    }

    auto* filterRow = CCMenu::create();
    filterRow->setPosition({0.f, 0.f});
    this->addChild(filterRow, 10);

    auto makeFilterBtn = [this, filterRow](char const* label, int mode, float x, float y)
            -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.28f);
        if (!spr) return nullptr;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, mode](CCMenuItemSpriteExtra*) {
                m_filterMode = mode;
                refreshFilterButtons();
                applyFilter();
            });
        if (btn) {
            btn->setPosition({x, y});
            filterRow->addChild(btn);
        }
        return btn;
    };
    const float filterRight = c1 - 28.f;
    const float chipGap = 40.f;
    m_filterEditedBtn  = makeFilterBtn("Edit", 2, filterRight - chipGap * 0.f, toolbarY);
    m_filterAllUiBtn   = makeFilterBtn("UI",   1, filterRight - chipGap * 1.f, toolbarY);
    m_filterButtonsBtn = makeFilterBtn("Btns", 0, filterRight - chipGap * 2.f, toolbarY);

    m_gridCols = std::max(2, static_cast<int>((centerW - 10.f) / kCellW));
    m_gridRows = std::max(2, static_cast<int>(gridH / kCellH));

    auto* gridHost = CCNode::create();
    gridHost->setContentSize({m_gridCols * kCellW, m_gridRows * kCellH});
    gridHost->setAnchorPoint({0.5f, 1.f});
    gridHost->setPosition({c0 + centerW * 0.5f, gridTop});
    this->addChild(gridHost, 5);
    m_gridHost = gridHost;

    // Pagination: ◀  page  |  count  ▶  centered under the grid.
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    const float midX = c0 + centerW * 0.5f;
    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.42f);
        if (auto* prevBtn = CCMenuItemExt::createSpriteExtra(prevSpr,
                [this](CCMenuItemSpriteExtra*) {
                    if (m_page > 0) { --m_page; rebuildGrid(); }
                })) {
            prevBtn->setPosition({c0 + 16.f, pageY});
            menu->addChild(prevBtn);
        }
    }
    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setScale(0.42f);
        nextSpr->setFlipX(true);
        if (auto* nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int perPage = m_gridCols * m_gridRows;
                    int pages = std::max(1,
                        (static_cast<int>(m_filtered.size()) + perPage - 1) / perPage);
                    if (m_page + 1 < pages) { ++m_page; rebuildGrid(); }
                })) {
            nextBtn->setPosition({c1 - 16.f, pageY});
            menu->addChild(nextBtn);
        }
    }
    if (auto* pageLbl = CCLabelBMFont::create("1 / 1", "bigFont.fnt")) {
        pageLbl->setScale(0.3f);
        pageLbl->setAnchorPoint({0.5f, 0.5f});
        pageLbl->setPosition({midX - 34.f, pageY});
        this->addChild(pageLbl, 5);
        m_pageLbl = pageLbl;
    }
    if (auto* countLbl = CCLabelBMFont::create("", "bigFont.fnt")) {
        countLbl->setScale(0.24f);
        countLbl->setColor({170, 170, 180});
        countLbl->setAnchorPoint({0.5f, 0.5f});
        countLbl->setPosition({midX + 44.f, pageY});
        this->addChild(countLbl, 5);
        m_countLbl = countLbl;
    }
}

void ProjectEditorLayer::buildTabsPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    const float panelX = rightPanelX(winSize.width);
    const float panelW = rightPanelW();
    const float pb = panelBottomY();
    const float ph = panelTopY(winSize) - pb;
    auto* card = makeCard(this, panelX, pb, panelW, ph);

    const float tabRowY = pb + ph - kTabRowH * 0.5f - 3.f;
    const float contentH = std::max(120.f, ph - kTabRowH - 8.f);
    addCardDivider(card, contentH + 3.f);

    struct TabDef {
        char const* label;
        void (ProjectEditorLayer::*builder)(CCNode*, float, float);
    };
    const TabDef defs[4] = {
        {"Pack",   &ProjectEditorLayer::buildPackTab},
        {"Tune",   &ProjectEditorLayer::buildTuneTab},
        {"Extra",  &ProjectEditorLayer::buildExtraTab},
        {"Sprite", &ProjectEditorLayer::buildSpriteTab},
    };

    auto* tabRow = CCMenu::create();
    tabRow->setContentSize({panelW - 10.f, kTabRowH});
    tabRow->setAnchorPoint({0.5f, 0.5f});
    tabRow->setPosition({panelX + panelW * 0.5f, tabRowY});
    tabRow->setLayout(
        RowLayout::create()
            ->setGap(3.f)
            ->setAxisAlignment(AxisAlignment::Even)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(true));
    this->addChild(tabRow, 10);

    for (int i = 0; i < 4; ++i) {
        if (auto* spr = ButtonSprite::create(defs[i].label, "bigFont.fnt",
                                             "GJ_button_04.png", 0.28f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [this, i](CCMenuItemSpriteExtra*) { this->selectTab(i); })) {
                tabRow->addChild(btn);
                m_tabBtns[i] = btn;
            }
        }

        auto* tab = CCNode::create();
        tab->setContentSize({panelW, contentH});
        tab->setAnchorPoint({0.f, 0.f});
        tab->setPosition({panelX, pb});
        this->addChild(tab, 5);
        m_tabs[i] = tab;

        (this->*(defs[i].builder))(tab, panelW, contentH);
    }
    tabRow->updateLayout();
}

void ProjectEditorLayer::selectTab(int index) {
    m_activeTab = std::clamp(index, 0, 3);
    for (int i = 0; i < 4; ++i) {
        if (m_tabs[i]) m_tabs[i]->setVisible(i == m_activeTab);
        if (m_tabBtns[i]) {
            if (auto* spr = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage())) {
                spr->updateBGImage(i == m_activeTab
                    ? "GJ_button_01.png" : "GJ_button_04.png");
            }
        }
    }
}


void ProjectEditorLayer::buildPackTab(CCNode* tab, float w, float h) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({w, h});
    tab->addChild(menu);

    // Compact top-to-bottom stack: title → swatches → brightness → hints → actions.
    float y = h - 12.f;

    if (auto* caption = CCLabelBMFont::create("Pack colors", "goldFont.fnt")) {
        caption->setScale(0.4f);
        caption->setPosition({w * 0.5f, y});
        tab->addChild(caption);
    }
    y -= 28.f;

    struct SwatchDef {
        char const* caption;
        ccColor3B initial;
        std::function<void(ProjectEditorLayer*, ccColor3B)> apply;
    };
    const SwatchDef swatches[4] = {
        {"C1", m_project.color1,
         [](ProjectEditorLayer* s, ccColor3B c) { s->m_project.color1 = c; }},
        {"C2", m_project.color2,
         [](ProjectEditorLayer* s, ccColor3B c) { s->m_project.color2 = c; }},
        {"Glow", m_project.colorGlow,
         [](ProjectEditorLayer* s, ccColor3B c) { s->m_project.colorGlow = c; }},
        {"White", m_project.colorDetail,
         [](ProjectEditorLayer* s, ccColor3B c) { s->m_project.colorDetail = c; }},
    };
    const float swatchGap = (w - 16.f) / 4.f;
    for (int i = 0; i < 4; ++i) {
        float x = 8.f + swatchGap * (static_cast<float>(i) + 0.5f);
        auto* swatch = makeSwatchSprite(swatches[i].initial, 24.f);
        if (!swatch) continue;
        auto apply = swatches[i].apply;
        auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
            [this, apply, i](CCMenuItemSpriteExtra*) {
                ccColor3B current = m_packSwatch[i]
                    ? m_packSwatch[i]->getColor() : ccColor3B{255, 255, 255};
                auto* popup = ColorPickPopup::create(
                    ccColor4B{current.r, current.g, current.b, 255});
                if (!popup) return;
                popup->setCallback([this, apply, i](ccColor4B const& picked) {
                    ccColor3B c{picked.r, picked.g, picked.b};
                    apply(this, c);
                    if (m_packSwatch[i]) m_packSwatch[i]->setColor(c);
                    markEdited(true);
                });
                popup->show();
            });
        if (!btn) continue;
        m_packSwatch[i] = swatch;
        btn->setPosition({x, y});
        menu->addChild(btn);
        if (auto* cap = CCLabelBMFont::create(swatches[i].caption, "bigFont.fnt")) {
            cap->setScale(0.2f);
            cap->setColor({170, 170, 180});
            cap->setPosition({x, y - 18.f});
            tab->addChild(cap);
        }
    }
    y -= 40.f;

    auto* brightnessRow = ParamSliderRow::create("Brightness", 100.f, 300.f, 1.f,
        static_cast<float>(m_project.brightness), w - 16.f,
        [this](float v) {
            m_project.brightness = static_cast<int>(std::lround(v));
            markEdited(true);
        }, nullptr);
    if (brightnessRow) {
        brightnessRow->setPosition({8.f, y});
        tab->addChild(brightnessRow);
        m_brightnessRow = brightnessRow;
    }
    y -= 24.f;

    if (auto* hint1 = CCLabelBMFont::create(
            "White = inner white details (kept vanilla by default)", "chatFont.fnt")) {
        hint1->setColor({150, 155, 165});
        hint1->limitLabelWidth(w - 14.f, 0.4f, 0.14f);
        hint1->setPosition({w * 0.5f, y});
        tab->addChild(hint1);
    }
    y -= 14.f;
    if (auto* hint2 = CCLabelBMFont::create(
            "Brightness: lower = stronger color", "chatFont.fnt")) {
        hint2->setColor({150, 155, 165});
        hint2->limitLabelWidth(w - 14.f, 0.4f, 0.14f);
        hint2->setPosition({w * 0.5f, y});
        tab->addChild(hint2);
    }
    y -= 28.f;

    // Secondary actions side-by-side — no lone button stuck at the bottom.
    if (auto* presetSpr = ButtonSprite::create("Next Preset", "bigFont.fnt", "GJ_button_05.png", 0.28f)) {
        if (auto* presetBtn = CCMenuItemExt::createSpriteExtra(presetSpr,
                [this](CCMenuItemSpriteExtra*) {
                    auto const& presets = ColorPresets::list();
                    if (presets.empty()) return;
                    m_presetIndex = (m_presetIndex + 1) % static_cast<int>(presets.size());
                    auto const& p = presets[m_presetIndex];
                    m_project.color1     = p.color1;
                    m_project.color2     = p.color2;
                    m_project.colorGlow  = p.colorGlow;
                    m_project.brightness = p.brightness;
                    if (m_packSwatch[0]) m_packSwatch[0]->setColor(p.color1);
                    if (m_packSwatch[1]) m_packSwatch[1]->setColor(p.color2);
                    if (m_packSwatch[2]) m_packSwatch[2]->setColor(p.colorGlow);
                    if (m_brightnessRow) {
                        m_brightnessRow->setValue(static_cast<float>(p.brightness));
                    }
                    markEdited(true);
                    setStatus("Preset: " + p.name);
                })) {
            presetBtn->setPosition({w * 0.32f, y});
            menu->addChild(presetBtn);
        }
    }

    if (auto* creditsSpr = ButtonSprite::create("Credits", "bigFont.fnt", "GJ_button_04.png", 0.28f)) {
        if (auto* creditsBtn = CCMenuItemExt::createSpriteExtra(creditsSpr,
                [](CCMenuItemSpriteExtra*) {
                    PopupManager::get().quickPopup(
                        "Credits",
                        "Texture Studio uses the recoloring approach pioneered by\n"
                        "<cy>PackGen</c> by <cl>Asterveila</c>:\n"
                        "  packgenweb.pages.dev\n\n"
                        "Algorithm: per-pixel <cj>luminance tinting</c> guided by\n"
                        "alpha-weighted segmentation with edge-aware masks.\n\n"
                        "Open the PackGen website in your browser?",
                        "Close", "Open Site",
                        [](FLAlertLayer*, bool yes) {
                            if (yes) {
                                geode::utils::web::openLinkInBrowser(
                                    "https://packgenweb.pages.dev/");
                            }
                        }).showInstant();
                })) {
            creditsBtn->setPosition({w * 0.72f, y});
            menu->addChild(creditsBtn);
        }
    }
}

void ProjectEditorLayer::buildTuneTab(CCNode* tab, float w, float h) {
    struct RowDef {
        char const* label;
        float minV, maxV, step, initial;
        std::function<void(ProjectEditorLayer*, float)> apply;
        ParamSliderRow::Formatter fmt;
    };
    const RowDef rows[] = {
        {"Softness", 0.f, 1.f, 0.f, m_project.maskSoftness,
         [](ProjectEditorLayer* s, float v) { s->m_project.maskSoftness = v; },
         percentFmt},
        {"Precision", 2.f, 8.f, 1.f, static_cast<float>(m_project.clusterPrecision),
         [](ProjectEditorLayer* s, float v) {
             s->m_project.clusterPrecision = static_cast<int>(std::lround(v));
         }, nullptr},
        {"Edge clean", 0.f, 4.f, 1.f, static_cast<float>(m_project.edgeCleanup),
         [](ProjectEditorLayer* s, float v) {
             s->m_project.edgeCleanup = static_cast<int>(std::lround(v));
         }, nullptr},
        {"Dark protect", 0.f, 96.f, 1.f, static_cast<float>(m_project.outlineProtect),
         [](ProjectEditorLayer* s, float v) {
             s->m_project.outlineProtect = static_cast<int>(std::lround(v));
         }, nullptr},
        {"Saturation", 0.f, 2.f, 0.f, m_project.saturation,
         [](ProjectEditorLayer* s, float v) { s->m_project.saturation = v; },
         percentFmt},
        {"Contrast", -0.5f, 0.5f, 0.f, m_project.contrast,
         [](ProjectEditorLayer* s, float v) { s->m_project.contrast = v; },
         [](float v) { return fmt::format("{:+.0f}%", v * 100.f); }},
    };

    if (auto* caption = CCLabelBMFont::create("Algorithm tuning", "goldFont.fnt")) {
        caption->setScale(0.42f);
        caption->setPosition({w / 2.f, h - 14.f});
        tab->addChild(caption);
    }

    float y = h - 36.f;
    for (auto const& def : rows) {
        auto apply = def.apply;
        auto* row = ParamSliderRow::create(def.label, def.minV, def.maxV, def.step,
            def.initial, w - 16.f,
            [this, apply](float v) {
                apply(this, v);
                markEdited(true);
            }, def.fmt);
        if (row) {
            row->setPosition({8.f, y});
            tab->addChild(row);
        }
        y -= 26.f;
    }

    if (auto* hint = CCLabelBMFont::create(
            "Precision & Edge clean control how exact the paint masks are.",
            "chatFont.fnt")) {
        hint->setColor({150, 155, 165});
        hint->limitLabelWidth(w - 16.f, 0.42f, 0.15f);
        hint->setPosition({w / 2.f, y - 6.f});
        tab->addChild(hint);
    }
}

void ProjectEditorLayer::buildExtraTab(CCNode* tab, float w, float h) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({w, h});
    tab->addChild(menu);

    struct ToggleDef {
        char const* label;
        bool initial;
        std::function<void(ProjectEditorLayer*, bool)> apply;
        bool affectsPreview;
    };
    const ToggleDef defs[] = {
        {"Alt glow", m_project.alternativeGlowOverlay,
         [](ProjectEditorLayer* s, bool v) { s->m_project.alternativeGlowOverlay = v; }, true},
        {"Transp. lists", m_project.transparentLists,
         [](ProjectEditorLayer* s, bool v) { s->m_project.transparentLists = v; }, false},
        {"Gradient BG", m_project.colorGradientBg,
         [](ProjectEditorLayer* s, bool v) { s->m_project.colorGradientBg = v; }, false},
        {"Main menu BG", m_project.colorMainMenu,
         [](ProjectEditorLayer* s, bool v) { s->m_project.colorMainMenu = v; }, false},
        {"HD port", m_project.includeMediumPort,
         [](ProjectEditorLayer* s, bool v) { s->m_project.includeMediumPort = v; }, false},
        {"Precision", m_project.usePackGenAssets,
         [](ProjectEditorLayer* s, bool v) { s->m_project.usePackGenAssets = v; }, false},
        {"Gold font", m_project.tintGoldFont,
         [](ProjectEditorLayer* s, bool v) { s->m_project.tintGoldFont = v; }, false},
        {"Gold titles", m_project.colorGoldTitles,
         [](ProjectEditorLayer* s, bool v) { s->m_project.colorGoldTitles = v; }, false},
        {"Demon faces", m_project.colorDemonFaces,
         [](ProjectEditorLayer* s, bool v) { s->m_project.colorDemonFaces = v; }, false},
        {"Mythic demons", m_project.mythicCompat,
         [](ProjectEditorLayer* s, bool v) { s->m_project.mythicCompat = v; }, false},
        {"Mod textures", m_project.includeModTextures,
         [](ProjectEditorLayer* s, bool v) { s->m_project.includeModTextures = v; }, false},
        {"Anim. GIF out", m_project.exportAnimatedFusions,
         [](ProjectEditorLayer* s, bool v) { s->m_project.exportAnimatedFusions = v; }, false},
    };

    constexpr int kRows = 6;
    const float colX[2] = {14.f, w / 2.f + 8.f};
    float y = h - 18.f;
    int index = 0;
    for (auto const& def : defs) {
        int col = index / kRows;
        int row = index % kRows;
        float rowY = y - row * 22.f;
        ++index;

        auto apply = def.apply;
        bool affectsPreview = def.affectsPreview;
        auto* toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.42f,
            [this, apply, affectsPreview](CCMenuItemToggler* t) {
                if (!t) return;
                apply(this, !t->isToggled());
                markEdited(affectsPreview);
            });
        if (toggler) {
            toggler->toggle(def.initial);
            toggler->setPosition({colX[col], rowY});
            menu->addChild(toggler);
        }
        if (auto* lbl = CCLabelBMFont::create(def.label, "bigFont.fnt")) {
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->limitLabelWidth(w / 2.f - 28.f, 0.28f, 0.15f);
            lbl->setPosition({colX[col] + 12.f, rowY});
            tab->addChild(lbl);
        }
    }
    y -= kRows * 22.f;

    if (auto* lbl = CCLabelBMFont::create("Tint scope:", "bigFont.fnt")) {
        lbl->setScale(0.3f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, y - 4.f});
        tab->addChild(lbl);
    }
    if (auto* scopeSpr = ButtonSprite::create(scopeLabel(m_project.tintScope),
                                              "bigFont.fnt", "GJ_button_04.png", 0.28f)) {
        if (auto* scopeBtn = CCMenuItemExt::createSpriteExtra(scopeSpr,
                [this](CCMenuItemSpriteExtra* btn) {
                    int next = (static_cast<int>(m_project.tintScope) + 1) % 3;
                    m_project.tintScope = static_cast<TintScope>(next);
                    if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
                        spr->setString(scopeLabel(m_project.tintScope));
                    }
                    markEdited(true);
                })) {
            scopeBtn->setPosition({w - 62.f, y - 4.f});
            menu->addChild(scopeBtn);
        }
    }
    y -= 30.f;

    std::string sheetInfo = "Sheets: " + std::to_string(m_project.sheets.size());
    if (m_project.hasBuiltOnce) sheetInfo += "  -  built before";
    if (auto* sheetLbl = CCLabelBMFont::create(sheetInfo.c_str(), "chatFont.fnt")) {
        sheetLbl->setScale(0.42f);
        sheetLbl->setColor({150, 155, 165});
        sheetLbl->setPosition({w / 2.f, y});
        tab->addChild(sheetLbl);
    }

    if (auto* testSpr = ButtonSprite::create("Self-test", "bigFont.fnt", "GJ_button_04.png", 0.3f)) {
        if (auto* testBtn = CCMenuItemExt::createSpriteExtra(testSpr,
                [](CCMenuItemSpriteExtra*) {
                    bool passed = engineSelfTest();
                    Notification::create(passed
                            ? "Texture engine self-test passed."
                            : "Texture engine self-test failed; check the log.",
                        passed ? NotificationIcon::Success : NotificationIcon::Error,
                        3.f)->show();
                })) {
            testBtn->setPosition({w / 2.f, 18.f});
            menu->addChild(testBtn);
        }
    }
}

void ProjectEditorLayer::buildSpriteTab(CCNode* tab, float w, float h) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({w, h});
    tab->addChild(menu);

    if (auto* nameLbl = CCLabelBMFont::create("Select a sprite from the list", "chatFont.fnt")) {
        nameLbl->setScale(0.42f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->limitLabelWidth(w - 46.f, 0.42f, 0.2f);
        nameLbl->setPosition({8.f, h - 8.f});
        tab->addChild(nameLbl);
        m_spriteNameLbl = nameLbl;
    }
    if (auto* resetSpr = ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.24f)) {
        if (auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
                [this](CCMenuItemSpriteExtra*) { this->onResetSprite(); })) {
            resetBtn->setAnchorPoint({1.f, 0.5f});
            resetBtn->setPosition({w - 6.f, h - 9.f});
            menu->addChild(resetBtn);
        }
    }

    auto* modeRow = CCMenu::create();
    modeRow->setContentSize({w - 16.f, 20.f});
    modeRow->setAnchorPoint({0.5f, 0.5f});
    modeRow->setPosition({w / 2.f, h - 26.f});
    modeRow->setLayout(
        RowLayout::create()
            ->setGap(5.f)
            ->setAxisAlignment(AxisAlignment::Even)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(true));
    tab->addChild(modeRow);

    auto makeModeBtn = [this, modeRow](char const* label, int mode)
            -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.3f);
        if (!spr) return nullptr;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, mode](CCMenuItemSpriteExtra*) {
                if (!m_hasSelection) return;
                auto s = currentSetting();
                s.skip            = (mode == 2);
                s.useCustomColors = (mode == 1);
                storeSetting(s);
                refreshSpriteTabUi();
                 rebuildGrid();
                refreshPreviewTint();
            });
        if (btn) modeRow->addChild(btn);
        return btn;
    };
    m_modeGlobalBtn = makeModeBtn("Global", 0);
    m_modeCustomBtn = makeModeBtn("Custom", 1);
    m_modeSkipBtn   = makeModeBtn("Skip",   2);
    modeRow->updateLayout();

    struct SwatchDef {
        char const* caption;
        std::function<ccColor3B(SpriteSetting const&)> get;
        std::function<void(SpriteSetting&, ccColor3B)> set;
    };
    const SwatchDef swatches[4] = {
        {"C1",
         [](SpriteSetting const& s) { return s.color1; },
         [](SpriteSetting& s, ccColor3B c) { s.color1 = c; }},
        {"C2",
         [](SpriteSetting const& s) { return s.color2; },
         [](SpriteSetting& s, ccColor3B c) { s.color2 = c; }},
        {"Glow",
         [](SpriteSetting const& s) { return s.colorGlow; },
         [](SpriteSetting& s, ccColor3B c) { s.colorGlow = c; }},
        {"White",
         [](SpriteSetting const& s) { return s.colorDetail; },
         [](SpriteSetting& s, ccColor3B c) { s.colorDetail = c; }},
    };
    for (int i = 0; i < 4; ++i) {
        float x = 32.f + static_cast<float>(i) * 44.f;
        auto* swatch = makeSwatchSprite({255, 255, 255}, 18.f);
        if (!swatch) continue;
        auto set = swatches[i].set;
        auto get = swatches[i].get;
        auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
            [this, set, get](CCMenuItemSpriteExtra*) {
                if (!m_hasSelection) return;
                auto s = currentSetting();
                if (!s.useCustomColors) return;
                ccColor3B current = get(s);
                auto* popup = ColorPickPopup::create(
                    ccColor4B{current.r, current.g, current.b, 255});
                if (!popup) return;
                popup->setCallback([this, set](ccColor4B const& picked) {
                    if (!m_hasSelection) return;
                    auto s2 = currentSetting();
                    set(s2, ccColor3B{picked.r, picked.g, picked.b});
                    storeSetting(s2);
                    refreshSpriteTabUi();
                    refreshPreviewTint();
                });
                popup->show();
            });
        if (!btn) continue;
        m_spriteSwatch[i] = swatch;
        btn->setPosition({x, h - 46.f});
        menu->addChild(btn);
        if (auto* cap = CCLabelBMFont::create(swatches[i].caption, "bigFont.fnt")) {
            cap->setScale(0.18f);
            cap->setColor({170, 170, 180});
            cap->setPosition({x, h - 59.f});
            tab->addChild(cap);
        }
    }

    auto* imageRow = CCMenu::create();
    imageRow->setContentSize({w - 16.f, 20.f});
    imageRow->setAnchorPoint({0.5f, 0.5f});
    imageRow->setPosition({w / 2.f, h - 74.f});
    imageRow->setLayout(
        RowLayout::create()
            ->setGap(5.f)
            ->setAxisAlignment(AxisAlignment::Even)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(true));
    tab->addChild(imageRow);
    m_imageRow = imageRow;

    if (auto* pickSpr = ButtonSprite::create("Pick...", "bigFont.fnt", "GJ_button_05.png", 0.28f)) {
        if (auto* pickBtn = CCMenuItemExt::createSpriteExtra(pickSpr,
                [this](CCMenuItemSpriteExtra*) { this->onPickImage(); })) {
            imageRow->addChild(pickBtn);
        }
    }
    if (auto* clearSpr = ButtonSprite::create("Clear", "bigFont.fnt", "GJ_button_06.png", 0.28f)) {
        if (auto* clearBtn = CCMenuItemExt::createSpriteExtra(clearSpr,
                [this](CCMenuItemSpriteExtra*) { this->onClearImage(); })) {
            imageRow->addChild(clearBtn);
        }
    }
    if (auto* fitSpr = ButtonSprite::create("Fit", "bigFont.fnt", "GJ_button_04.png", 0.28f)) {
        if (auto* fitBtn = CCMenuItemExt::createSpriteExtra(fitSpr,
                [this](CCMenuItemSpriteExtra*) {
                    if (!m_hasSelection) return;
                    auto s = currentSetting();
                    int next = (static_cast<int>(s.imageTransform.fitMode) + 1) % 3;
                    s.imageTransform.fitMode = static_cast<ImageFitMode>(next);
                    storeSetting(s);
                    refreshSpriteTabUi();
                    refreshPreviewTint();
                })) {
            imageRow->addChild(fitBtn);
            m_fitBtn = fitBtn;
        }
    }
    if (auto* modeSpr = ButtonSprite::create("Replace", "bigFont.fnt", "GJ_button_04.png", 0.28f)) {
        if (auto* modeBtn = CCMenuItemExt::createSpriteExtra(modeSpr,
                [this](CCMenuItemSpriteExtra*) {
                    if (!m_hasSelection) return;
                    auto s = currentSetting();
                    s.imageOverlay = !s.imageOverlay;
                    storeSetting(s);
                    refreshSpriteTabUi();
                    if (s.hasCustomImage) {
                        rebuildGrid();
                        refreshPreviewTint();
                    }
                })) {
            imageRow->addChild(modeBtn);
            m_imgModeBtn = modeBtn;
        }
    }
    imageRow->updateLayout();

    if (auto* stateLbl = CCLabelBMFont::create("no image", "bigFont.fnt")) {
        stateLbl->setScale(0.22f);
        stateLbl->setColor({170, 170, 180});
        stateLbl->setPosition({w / 2.f, h - 88.f});
        tab->addChild(stateLbl);
        m_imageStateLbl = stateLbl;
    }

    // Flip row: Flip X / Flip Y (custom image only — fusion has its own tab).
    auto* flipRow = CCMenu::create();
    flipRow->setContentSize({w - 16.f, 20.f});
    flipRow->setAnchorPoint({0.5f, 0.5f});
    flipRow->setPosition({w / 2.f, h - 102.f});
    flipRow->setLayout(
        RowLayout::create()
            ->setGap(10.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false));
    tab->addChild(flipRow);

    auto makeFlip = [this, flipRow](char const* label,
                                    CCMenuItemToggler*& out,
                                    bool isX) {
        auto* tog = CCMenuItemExt::createTogglerWithStandardSprites(0.38f,
            [this, isX](CCMenuItemToggler* t) {
                if (!t || !m_hasSelection) return;
                auto s = currentSetting();
                bool v = !t->isToggled();
                if (isX) s.imageTransform.flipX = v;
                else     s.imageTransform.flipY = v;
                storeSetting(s);
                refreshPreviewTint();
            });
        if (!tog) return;
        flipRow->addChild(tog);
        out = tog;
        if (auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt")) {
            lbl->setScale(0.26f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({tog->getContentSize().width / 2.f + 3.f, 0.f});
            tog->addChild(lbl);
        }
    };
    makeFlip("Flip X", m_flipXTog, true);
    makeFlip("Flip Y", m_flipYTog, false);
    flipRow->updateLayout();

    if (auto* goFusionSpr = ButtonSprite::create(
            "Open Fusion Layer", "bigFont.fnt", "GJ_button_01.png", 0.3f)) {
        if (auto* goFusionBtn = CCMenuItemExt::createSpriteExtra(goFusionSpr,
                [this](CCMenuItemSpriteExtra*) { this->onOpenFusionLayer(); })) {
            goFusionBtn->setPosition({w / 2.f, h - 118.f});
            auto* fm = CCMenu::create();
            fm->setPosition({0, 0});
            tab->addChild(fm);
            fm->addChild(goFusionBtn);
        }
    }

    struct RowDef {
        char const* label;
        float minV, maxV;
        std::function<float(SpriteSetting const&)> get;
        std::function<void(SpriteSetting&, float)> set;
        ParamSliderRow::Formatter fmt;
        ParamSliderRow** store;
    };
    auto pct = [](float v) { return fmt::format("{:.0f}%", v * 100.f); };
    const RowDef rows[] = {
        {"Scale", 0.1f, 4.f,
         [](SpriteSetting const& s) { return s.imageTransform.scale; },
         [](SpriteSetting& s, float v) { s.imageTransform.scale = v; },
         pct, &m_scaleRow},
        {"Offset X", -1.f, 1.f,
         [](SpriteSetting const& s) { return s.imageTransform.offsetX; },
         [](SpriteSetting& s, float v) { s.imageTransform.offsetX = v; },
         [](float v) { return fmt::format("{:+.0f}%", v * 100.f); }, &m_offXRow},
        {"Offset Y", -1.f, 1.f,
         [](SpriteSetting const& s) { return s.imageTransform.offsetY; },
         [](SpriteSetting& s, float v) { s.imageTransform.offsetY = v; },
         [](float v) { return fmt::format("{:+.0f}%", v * 100.f); }, &m_offYRow},
        {"Rotation", 0.f, 360.f,
         [](SpriteSetting const& s) { return s.imageTransform.rotationDeg; },
         [](SpriteSetting& s, float v) { s.imageTransform.rotationDeg = v; },
         [](float v) { return fmt::format("{:.0f}", v); }, &m_rotRow},
        {"Opacity", 0.f, 1.f,
         [](SpriteSetting const& s) { return s.imageTransform.opacity / 255.f; },
         [](SpriteSetting& s, float v) {
             s.imageTransform.opacity = static_cast<int>(std::lround(v * 255.f));
         }, pct, &m_opacityRow},
    };

    float y = h - 136.f;
    for (auto const& def : rows) {
        auto set = def.set;
        auto* row = ParamSliderRow::create(def.label, def.minV, def.maxV, 0.f,
            def.get(SpriteSetting{}), w - 16.f,
            [this, set](float v) {
                if (!m_hasSelection) return;
                auto s = currentSetting();
                set(s, v);
                storeSetting(s);
                if (s.hasCustomImage) refreshPreviewTint();
            }, def.fmt);
        if (row) {
            row->setPosition({8.f, y});
            tab->addChild(row);
            *def.store = row;
        }
        y -= 17.f;
    }

    refreshSpriteTabUi();
}


SpriteSetting ProjectEditorLayer::currentSetting() const {
    SpriteSetting s;
    s.color1      = m_project.color1;
    s.color2      = m_project.color2;
    s.colorGlow   = m_project.colorGlow;
    s.colorDetail = m_project.colorDetail;
    if (!m_hasSelection) return s;
    auto it = m_project.spriteSettings.find(m_selected.frameName);
    if (it != m_project.spriteSettings.end()) return it->second;
    return s;
}

void ProjectEditorLayer::storeSetting(SpriteSetting const& s) {
    if (!m_hasSelection) return;
    if (s.hasAny()) {
        m_project.spriteSettings[m_selected.frameName] = s;
    } else {
        m_project.spriteSettings.erase(m_selected.frameName);
    }
    m_project.modifiedAt = nowUnixMs();
    setStatus("Edited (unsaved).");
}

void ProjectEditorLayer::refreshSpriteTabUi() {
    auto setting = currentSetting();

    if (m_spriteNameLbl) {
        std::string name = m_hasSelection
            ? m_selected.frameName : std::string("Select a sprite from the list");
        m_spriteNameLbl->setString(name.c_str());
        m_spriteNameLbl->limitLabelWidth(rightPanelW() - 60.f, 0.42f, 0.2f);
    }

    int mode = setting.skip ? 2 : (setting.useCustomColors ? 1 : 0);
    auto highlight = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(active ? "GJ_button_01.png" : "GJ_button_04.png");
        }
    };
    highlight(m_modeGlobalBtn, m_hasSelection && mode == 0);
    highlight(m_modeCustomBtn, m_hasSelection && mode == 1);
    highlight(m_modeSkipBtn,   m_hasSelection && mode == 2);

    GLubyte swatchOp = (m_hasSelection && mode == 1) ? 255 : 90;
    ccColor3B colors[4] = {setting.color1, setting.color2,
                           setting.colorGlow, setting.colorDetail};
    for (int i = 0; i < 4; ++i) {
        if (m_spriteSwatch[i]) {
            m_spriteSwatch[i]->setColor(colors[i]);
            m_spriteSwatch[i]->setOpacity(swatchOp);
        }
    }

    if (m_fitBtn) {
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_fitBtn->getNormalImage())) {
            spr->setString(fitModeLabel(setting.imageTransform.fitMode));
        }
    }
    if (m_imgModeBtn) {
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_imgModeBtn->getNormalImage())) {
            spr->setString(setting.imageOverlay ? "Overlay" : "Replace");
        }
    }
    if (m_imageRow) m_imageRow->updateLayout();
    auto syncToggle = [](CCMenuItemToggler* tog, bool v) {
        if (tog && tog->isToggled() != v) tog->toggle(v);
    };
    syncToggle(m_flipXTog, setting.imageTransform.flipX);
    syncToggle(m_flipYTog, setting.imageTransform.flipY);

    if (m_scaleRow)   m_scaleRow->setValue(setting.imageTransform.scale);
    if (m_offXRow)    m_offXRow->setValue(setting.imageTransform.offsetX);
    if (m_offYRow)    m_offYRow->setValue(setting.imageTransform.offsetY);
    if (m_rotRow)     m_rotRow->setValue(setting.imageTransform.rotationDeg);
    if (m_opacityRow) m_opacityRow->setValue(setting.imageTransform.opacity / 255.f);

    if (m_imageStateLbl) {
        std::string state = !setting.hasCustomImage ? "no image"
            : (setting.imageOverlay ? "image: overlaid on sprite"
                                    : "image: replaces sprite");
        if (setting.hasFusion) state += "  |  fusion on";
        m_imageStateLbl->setString(state.c_str());
    }
}

void ProjectEditorLayer::onPickImage() {
    if (!m_hasSelection) {
        Notification::create("Select a sprite first.", NotificationIcon::Info, 1.5f)->show();
        return;
    }
    WeakRef<ProjectEditorLayer> weakSelf(this);
    std::string frameName = m_selected.frameName;
    pt::pickImage([weakSelf, frameName](
            geode::Result<std::optional<std::filesystem::path>> result) {
        auto self = weakSelf.lock();
        if (!self) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        std::filesystem::path srcPath = *pathOpt;
        auto dstPath = SlotPaths::spriteImageFile(self->m_slotId, frameName);

        paimon::ThreadTracker::get().spawn([weakSelf, frameName, srcPath, dstPath]() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto imgRes = ImageBuffer::loadFromFile(srcPath);
            std::shared_ptr<ImageBuffer> img;
            std::string err;
            if (imgRes) {
                img = std::make_shared<ImageBuffer>(std::move(imgRes).unwrap());
                std::error_code ec;
                std::filesystem::create_directories(dstPath.parent_path(), ec);
                if (auto wr = img->saveToPng(dstPath); !wr) {
                    err = wr.unwrapErr();
                    img.reset();
                }
            } else {
                err = imgRes.unwrapErr();
            }

            Loader::get()->queueInMainThread([weakSelf, frameName, img, err]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                if (!img) {
                    Notification::create(("Image import failed: " + err).c_str(),
                        NotificationIcon::Error, 3.0f)->show();
                    return;
                }
                if (!self->m_hasSelection ||
                    self->m_selected.frameName != frameName) return;
                self->m_customImage = img;
                auto s = self->currentSetting();
                s.hasCustomImage = true;
                self->storeSetting(s);
                self->refreshSpriteTabUi();
                self->rebuildGrid();
                self->refreshPreviewTint();
            });
        });
    });
}

void ProjectEditorLayer::onClearImage() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    if (!s.hasCustomImage) return;
    s.hasCustomImage = false;
    s.imageOverlay = false;
    s.imageTransform = ImageTransform{};
    storeSetting(s);
    m_customImage.reset();
    std::error_code ec;
    std::filesystem::remove(
        SlotPaths::spriteImageFile(m_slotId, m_selected.frameName), ec);
    refreshSpriteTabUi();
    rebuildGrid();
    refreshPreviewTint();
}


FusionApplyOptions ProjectEditorLayer::makeFusionOptions(SpriteSetting const& s) const {
    FusionApplyOptions opts;
    // Always stamp pure texture colours by default (Replace). Pack tint
    // must never recolor the user GIF/PNG — only Luma mode multiplies lighting.
    opts.blendMode = s.fusionBlend;
    opts.opacity   = s.fusionOpacity;
    opts.transform = s.fusionTransform;
    opts.pixelOffsetX = s.fusionPixelX;
    opts.pixelOffsetY = s.fusionPixelY;
    if (opts.transform.isDefault()) {
        opts.transform.fitMode = ImageFitMode::Fill;
    }
    return opts;
}


void ProjectEditorLayer::unloadFusion() {
    m_fusionAsset.reset();
    m_fusionMask.reset();
    m_fusionFrameIndex = 0;
}

void ProjectEditorLayer::loadFusionForSelection() {
    unloadFusion();
    if (!m_hasSelection) return;
    auto s = currentSetting();
    if (!s.hasFusion) return;

    std::string frameName = m_selected.frameName;
    std::string slotId = m_slotId;
    WeakRef<ProjectEditorLayer> weakSelf(this);
    auto closed = m_closed;

    paimon::ThreadTracker::get().spawn([weakSelf, closed, slotId, frameName]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;

        std::shared_ptr<MaskBuffer> mask;
        if (auto maskRes = FusionStore::loadForSlot(slotId, frameName)) {
            auto payload = std::move(maskRes).unwrap();
            mask = std::make_shared<MaskBuffer>(std::move(payload.mask));
        }
        std::shared_ptr<FusionAsset> asset;
        auto tryLoad = [&](std::filesystem::path const& p) {
            auto r = FusionAssetLoader::loadFromFile(p);
            if (r) asset = r.unwrap();
        };
        std::error_code ec;
        auto gifPath = SlotPaths::fusionTextureFile(slotId, frameName, ".gif");
        auto pngPath = SlotPaths::fusionTextureFile(slotId, frameName, ".png");
        if (std::filesystem::exists(gifPath, ec)) tryLoad(gifPath);
        else if (std::filesystem::exists(pngPath, ec)) tryLoad(pngPath);

        Loader::get()->queueInMainThread(
            [weakSelf, closed, frameName, mask, asset]() {
            if (paimon::isRuntimeShuttingDown() ||
                closed->load(std::memory_order_acquire)) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (!self->m_hasSelection ||
                self->m_selected.frameName != frameName) return;
            self->m_fusionMask = mask;
            self->m_fusionAsset = asset;
            self->m_fusionFrameIndex = 0;
            self->refreshPreviewTint();
        });
    });
}



void ProjectEditorLayer::onResetSprite() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    bool hadImage = s.hasCustomImage;
    bool hadFusion = s.hasFusion;
    m_project.spriteSettings.erase(m_selected.frameName);
    m_project.modifiedAt = nowUnixMs();
    m_customImage.reset();
    unloadFusion();
    if (hadImage) {
        std::error_code ec;
        std::filesystem::remove(
            SlotPaths::spriteImageFile(m_slotId, m_selected.frameName), ec);
    }
    if (hadFusion) {
        (void)FusionStore::deleteForSlot(m_slotId, m_selected.frameName);
    }
    setStatus("Sprite reset (unsaved).");
    refreshSpriteTabUi();
    rebuildGrid();
    refreshPreviewTint();
}


void ProjectEditorLayer::buildEntries() {
    m_all.clear();

    for (int i = 0; i < static_cast<int>(m_project.sheets.size()); ++i) {
        auto const& sheet = m_project.sheets[i];
        auto parsed = PlistParser::parseFile(
            std::filesystem::path(sheet.sourcePlistPath));
        if (!parsed) {
            log::warn("[texture-studio] browser: cannot parse {}: {}",
                sheet.sourcePlistPath, parsed.unwrapErr());
            continue;
        }
        for (auto const& frame : parsed.unwrap().frames) {
            auto kind = UiSpriteCatalog::classify(frame.name, sheet.baseName);
            if (kind != SpriteKind::Button && kind != SpriteKind::MenuUi) continue;
            Entry e;
            e.frameName  = frame.name;
            e.sheetIndex = i;
            e.kind       = kind;
            m_all.push_back(std::move(e));
        }
    }

    std::sort(m_all.begin(), m_all.end(), [](Entry const& a, Entry const& b) {
        if (a.kind != b.kind) return a.kind == SpriteKind::Button;
        return a.frameName < b.frameName;
    });

    refreshFilterButtons();
}

void ProjectEditorLayer::applyFilter() {
    m_filtered.clear();
    for (int i = 0; i < static_cast<int>(m_all.size()); ++i) {
        auto const& e = m_all[i];

        if (m_filterMode == 0 && e.kind != SpriteKind::Button) continue;
        if (m_filterMode == 2 &&
            m_project.spriteSettings.find(e.frameName) == m_project.spriteSettings.end()) {
            continue;
        }

        if (!m_search.empty()) {
            if (toLowerCopy(e.frameName).find(m_search) == std::string::npos) {
                continue;
            }
        }
        m_filtered.push_back(i);
    }
    m_page = 0;
    rebuildGrid();
}

void ProjectEditorLayer::refreshFilterButtons() {
    auto highlight = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(active ? "GJ_button_01.png" : "GJ_button_04.png");
        }
    };
    highlight(m_filterButtonsBtn, m_filterMode == 0);
    highlight(m_filterAllUiBtn,   m_filterMode == 1);
    highlight(m_filterEditedBtn,  m_filterMode == 2);
}

void ProjectEditorLayer::rebuildGrid() {
    if (!m_gridHost) return;
    int generation = m_thumbGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
    m_gridHost->removeAllChildren();
    m_gridMenu = nullptr;

    int perPage = m_gridCols * m_gridRows;
    int total = static_cast<int>(m_filtered.size());
    int pages = std::max(1, (total + perPage - 1) / perPage);
    m_page = std::clamp(m_page, 0, pages - 1);

    if (m_pageLbl) {
        m_pageLbl->setString(
            (std::to_string(m_page + 1) + " / " + std::to_string(pages)).c_str());
    }
    if (m_countLbl) {
        int edited = 0;
        for (auto const& [k, v] : m_project.spriteSettings) {
            if (v.hasAny()) ++edited;
        }
        m_countLbl->setString(
            (std::to_string(total) + " sprites, " +
             std::to_string(edited) + " edited").c_str());
        auto winSize = CCDirector::get()->getWinSize();
        const float centerW = centerRightX(winSize.width) - centerX0();
        m_countLbl->limitLabelWidth(
            std::max(50.f, centerW * 0.45f), 0.24f, 0.12f);
    }

    if (total == 0) {
        if (auto* empty = CCLabelBMFont::create(
                m_filterMode == 2 ? "No edited sprites yet." : "No sprites match.",
                "bigFont.fnt")) {
            empty->setScale(0.4f);
            empty->setColor({170, 170, 180});
            m_gridHost->addChildAtPosition(empty, Anchor::Center);
        }
        return;
    }

    auto* menu = CCMenu::create();
    if (!menu) return;
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(m_gridHost->getContentSize());
    m_gridHost->addChild(menu);
    m_gridMenu = menu;

    int start = m_page * perPage;
    int end   = std::min(total, start + perPage);
    float gridH = m_gridHost->getContentSize().height;

    for (int slot = 0; slot < end - start; ++slot) {
        auto const& entry = m_all[m_filtered[start + slot]];
        int col = slot % m_gridCols;
        int row = slot / m_gridCols;

        bool isSelected = m_hasSelection && entry.frameName == m_selected.frameName;

        auto* cell = CCNode::create();
        cell->setContentSize({kCellW - 5.f, kCellH - 5.f});

        if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
            bg->setContentSize(cell->getContentSize());
            bg->setTag(99);
            bg->setColor(isSelected
                ? ccColor3B{64, 84, 128}
                : (entry.kind == SpriteKind::Button
                    ? ccColor3B{40, 44, 56} : ccColor3B{36, 36, 42}));
            cell->addChildAtPosition(bg, Anchor::Center);
        }

        if (auto* thumb = CCSprite::create("square.png")) {
            thumb->setColor({72, 75, 84});
            thumb->setOpacity(150);
            thumb->setTag(100);
            auto sz = thumb->getContentSize();
            if (sz.width > 0 && sz.height > 0) {
                float maxSide = 28.f;
                thumb->setScale(std::min(
                    {maxSide / sz.width, maxSide / sz.height, 2.f}));
            }
            cell->addChildAtPosition(thumb, Anchor::Center, {0.f, 5.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.25f);
            loading->setTag(102);
            cell->addChildAtPosition(loading, Anchor::Center, {0.f, 5.f});
        }

        std::string shortName = entry.frameName;
        if (auto pos = shortName.rfind("_001.png"); pos != std::string::npos) {
            shortName.resize(pos);
        } else if (auto pos2 = shortName.rfind(".png"); pos2 != std::string::npos) {
            shortName.resize(pos2);
        }
        if (auto* nameLbl = CCLabelBMFont::create(shortName.c_str(), "chatFont.fnt")) {
            nameLbl->limitLabelWidth(kCellW - 12.f, 0.4f, 0.1f);
            cell->addChildAtPosition(nameLbl, Anchor::Bottom, {0.f, 7.f});
        }
        auto settingIt = m_project.spriteSettings.find(entry.frameName);
        if (settingIt != m_project.spriteSettings.end() && settingIt->second.hasAny()) {
            auto const& s = settingIt->second;
            char const* badgeText = s.skip ? "S"
                : (s.hasFusion ? "F"
                : (s.hasCustomImage ? "I" : "C"));
            ccColor3B badgeColor = s.skip ? ccColor3B{235, 90, 90}
                                 : (s.hasFusion ? ccColor3B{255, 170, 60}
                                 : (s.hasCustomImage ? ccColor3B{190, 120, 255}
                                                     : ccColor3B{120, 230, 130}));
            if (auto* badge = CCLabelBMFont::create(badgeText, "bigFont.fnt")) {
                badge->setScale(0.28f);
                badge->setColor(badgeColor);
                cell->addChildAtPosition(badge, Anchor::TopRight, {-6.f, -7.f});
            }
        }

        Entry entryCopy = entry;
        auto* item = CCMenuItemExt::createSpriteExtra(cell,
            [this, entryCopy](CCMenuItemSpriteExtra*) {
                this->selectEntry(entryCopy);
            });
        if (!item) continue;

        item->setTag(slot + 1);
        item->setPosition({(col + 0.5f) * kCellW,
                           gridH - (row + 0.5f) * kCellH});
        menu->addChild(item);
    }

    std::vector<Entry> pageEntries;
    pageEntries.reserve(end - start);
    for (int i = start; i < end; ++i) pageEntries.push_back(m_all[m_filtered[i]]);
    requestThumbnails(std::move(pageEntries), generation);
}

void ProjectEditorLayer::requestThumbnails(std::vector<Entry> entries, int generation) {
    if (entries.empty()) return;

    WeakRef<ProjectEditorLayer> weakSelf(this);
    auto thumbGeneration = m_thumbGeneration;
    auto closed = m_closed;
    auto project = m_project;
    auto slotId = m_slotId;

    paimon::ThreadTracker::get().spawn(
        [weakSelf, thumbGeneration, closed, project, slotId,
         entries = std::move(entries), generation]() mutable {
        for (int slot = 0; slot < static_cast<int>(entries.size()); ++slot) {
            if (paimon::isRuntimeShuttingDown() ||
                closed->load(std::memory_order_acquire) ||
                thumbGeneration->load(std::memory_order_acquire) != generation) {
                return;
            }

            auto const& entry = entries[slot];
            if (entry.sheetIndex < 0 ||
                entry.sheetIndex >= static_cast<int>(project.sheets.size())) continue;
            auto const& sheet = project.sheets[entry.sheetIndex];
            auto dataRes = FramePixelCache::get().frameData(
                std::filesystem::path(sheet.sourcePlistPath),
                std::filesystem::path(sheet.sourcePngPath), entry.frameName);
            if (!dataRes) continue;
            auto data = std::move(dataRes).unwrap();

            SpritePreviewResult preview;
            SpritePreviewOptions options;
            options.brightness = project.brightness;
            options.alternativeGlowOverlay = project.alternativeGlowOverlay;
            options.colors.color1 = project.color1;
            options.colors.color2 = project.color2;
            options.colors.glow   = project.colorGlow;
            options.colors.detail = project.colorDetail;
            options.maskSoftness     = project.maskSoftness;
            options.clusterPrecision = project.clusterPrecision;
            options.edgeCleanup      = project.edgeCleanup;
            options.outlineProtect   = project.outlineProtect;
            options.saturation       = project.saturation;
            options.contrast         = project.contrast;

            auto settingIt = project.spriteSettings.find(entry.frameName);
            SpriteSetting setting;
            if (settingIt != project.spriteSettings.end()) setting = settingIt->second;
            bool shouldTint = UiSpriteCatalog::shouldTint(entry.kind, project.tintScope);

            ImageBuffer customCanvas;
            if (!setting.skip && setting.hasCustomImage) {
                auto custom = ImageBuffer::loadFromFile(
                    SlotPaths::spriteImageFile(slotId, entry.frameName));
                if (custom) {
                    customCanvas = SpritePreviewRenderer::renderCustomImage(
                        custom.unwrap(), data.pixels.width(), data.pixels.height(),
                        setting.imageTransform);
                }
            }

            if (!customCanvas.empty() && !setting.imageOverlay) {
                preview.image = std::move(customCanvas);
            } else if (setting.skip ||
                       (!setting.useCustomColors && !shouldTint)) {
                preview.image = data.pixels;
                if (!customCanvas.empty()) {
                    SpritePreviewRenderer::compositeOver(preview.image, customCanvas);
                }
            } else {
                if (setting.useCustomColors) {
                    options.colors.color1 = setting.color1;
                    options.colors.color2 = setting.color2;
                    options.colors.glow   = setting.colorGlow;
                    options.colors.detail = setting.colorDetail;
                }
                preview = SpritePreviewRenderer::renderTintedWithStats(data.pixels, options);
                if (!customCanvas.empty()) {
                    SpritePreviewRenderer::compositeOver(preview.image, customCanvas);
                }
            }

            if (!setting.skip && setting.hasFusion) {
                auto maskRes = FusionStore::loadForSlot(slotId, entry.frameName);
                if (maskRes) {
                    auto payload = std::move(maskRes).unwrap();
                    if (payload.mask.width == preview.image.width()
                        && payload.mask.height == preview.image.height()) {
                        auto gifPath = SlotPaths::fusionTextureFile(
                            slotId, entry.frameName, ".gif");
                        auto pngPath = SlotPaths::fusionTextureFile(
                            slotId, entry.frameName, ".png");
                        std::error_code ec;
                        std::filesystem::path texPath =
                            std::filesystem::exists(gifPath, ec) ? gifPath : pngPath;
                        auto texRes = FusionAssetLoader::loadStaticFrame(texPath);
                        if (texRes) {
                            FusionApplyOptions fopts;
                            fopts.blendMode = setting.fusionBlend;
                            fopts.opacity   = setting.fusionOpacity;
                            fopts.transform = setting.fusionTransform;
                            fopts.pixelOffsetX = setting.fusionPixelX;
                            fopts.pixelOffsetY = setting.fusionPixelY;
                            if (fopts.transform.isDefault()) {
                                fopts.transform.fitMode = ImageFitMode::Fill;
                            }
                            FusionEngine::apply(preview.image, payload.mask,
                                texRes.unwrap(), fopts);
                        }
                    }
                }
            }
            preview.image = SpritesheetReader::composeLogicalFrame(preview.image, data.info);

            auto result = std::make_shared<SpritePreviewResult>(std::move(preview));
            Loader::get()->queueInMainThread(
                [weakSelf, thumbGeneration, closed, generation, slot, result]() {
                if (paimon::isRuntimeShuttingDown() ||
                    closed->load(std::memory_order_acquire) ||
                    thumbGeneration->load(std::memory_order_acquire) != generation) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                self->applyThumbnail(slot, generation, result);
            });
        }
    });
}

void ProjectEditorLayer::applyThumbnail(
    int slot, int generation, std::shared_ptr<SpritePreviewResult> result) {
    if (!result || result->image.empty() || !m_gridMenu ||
        m_thumbGeneration->load(std::memory_order_acquire) != generation) return;

    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(m_gridMenu->getChildByTag(slot + 1));
    if (!item) return;
    auto* cell = item->getNormalImage();
    if (!cell) return;
    if (auto* old = cell->getChildByTag(100)) old->removeFromParent();
    if (auto* loading = cell->getChildByTag(102)) loading->removeFromParent();

    if (auto* sprite = SpritePreviewRenderer::createSprite(result->image)) {
        auto size = sprite->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            sprite->setScale(std::min({28.f / size.width, 28.f / size.height, 2.f}));
        }
        sprite->setTag(100);
        cell->addChildAtPosition(sprite, Anchor::Center, {0.f, 5.f});
    }
}

void ProjectEditorLayer::selectEntry(Entry const& entry) {
    unloadFusion();
    m_hasSelection = true;
    m_selected = entry;
    highlightSelectedCell();
    refreshSpriteTabUi();
    selectTab(3);
    if (m_previewNameLbl) {
        m_previewNameLbl->setString(entry.frameName.c_str());
        m_previewNameLbl->limitLabelWidth(kLeftColW - 10.f, 0.42f, 0.2f);
    }
    if (currentSetting().hasFusion) {
        loadFusionForSelection();
    }
    startSelectionPixelLoad();
}

void ProjectEditorLayer::highlightSelectedCell() {
    if (!m_gridMenu) return;
    int perPage = m_gridCols * m_gridRows;
    int start = m_page * perPage;
    auto* children = m_gridMenu->getChildren();
    if (!children) return;
    for (unsigned int i = 0; i < children->count(); ++i) {
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(
            static_cast<CCNode*>(children->objectAtIndex(i)));
        if (!item) continue;
        int slot = item->getTag() - 1;
        int filteredIdx = start + slot;
        if (slot < 0 || filteredIdx >= static_cast<int>(m_filtered.size())) continue;
        auto const& entry = m_all[m_filtered[filteredIdx]];
        auto* cell = item->getNormalImage();
        if (!cell) continue;
        if (auto* bg = typeinfo_cast<CCScale9Sprite*>(cell->getChildByTag(99))) {
            bool isSelected = m_hasSelection &&
                entry.frameName == m_selected.frameName;
            bg->setColor(isSelected
                ? ccColor3B{64, 84, 128}
                : (entry.kind == SpriteKind::Button
                    ? ccColor3B{40, 44, 56} : ccColor3B{36, 36, 42}));
        }
    }
}


void ProjectEditorLayer::setOriginalSprite(CCSprite* spr) {
    if (!m_originalHost || !spr) return;
    if (m_originalSpr) m_originalSpr->removeFromParent();
    if (auto* loading = m_originalHost->getChildByID("loading-label")) {
        loading->removeFromParent();
    }
    fitSpriteIntoBox(spr, m_originalHost->getContentSize().width);
    m_originalHost->addChildAtPosition(spr, Anchor::Center, {0.f, -3.f});
    m_originalSpr = spr;
}

void ProjectEditorLayer::setResultSprite(CCSprite* spr) {
    if (!m_resultHost || !spr) return;
    if (m_resultSpr) m_resultSpr->removeFromParent();
    if (auto* loading = m_resultHost->getChildByID("loading-label")) {
        loading->removeFromParent();
    }
    fitSpriteIntoBox(spr, m_resultHost->getContentSize().width);
    m_resultHost->addChildAtPosition(spr, Anchor::Center, {0.f, -3.f});
    m_resultSpr = spr;
}

void ProjectEditorLayer::startSelectionPixelLoad() {
    Entry target = m_selected;
    if (!m_hasSelection) {
        if (!ensureRepresentativeFrame(m_project)) {
            if (m_coverageLbl) m_coverageLbl->setString("No UI frame found");
            return;
        }
        target.frameName  = m_project.representativeFrame;
        target.sheetIndex = m_project.representativeSheetIndex;
        target.kind       = SpriteKind::Button;
        if (m_previewNameLbl) {
            m_previewNameLbl->setString("(pack preview)");
        }
    }
    if (target.sheetIndex < 0 ||
        target.sheetIndex >= static_cast<int>(m_project.sheets.size())) {
        return;
    }

    int generation = m_previewGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
    auto previewGeneration = m_previewGeneration;
    auto closed = m_closed;
    auto sheet = m_project.sheets[target.sheetIndex];
    bool wantCustomImage = false;
    if (auto it = m_project.spriteSettings.find(target.frameName);
        it != m_project.spriteSettings.end()) {
        wantCustomImage = it->second.hasCustomImage;
    }
    auto customImagePath = SlotPaths::spriteImageFile(m_slotId, target.frameName);
    std::string frameName = target.frameName;

    WeakRef<ProjectEditorLayer> weakSelf(this);
    paimon::ThreadTracker::get().spawn(
        [weakSelf, previewGeneration, closed, generation, sheet, frameName,
         wantCustomImage, customImagePath]() {
            if (paimon::isRuntimeShuttingDown() ||
                closed->load(std::memory_order_acquire)) return;

            auto dataRes = FramePixelCache::get().frameData(
                std::filesystem::path(sheet.sourcePlistPath),
                std::filesystem::path(sheet.sourcePngPath), frameName);
            if (!dataRes) return;
            auto data = std::make_shared<FramePixelCache::FrameData>(
                std::move(dataRes).unwrap());
            auto logical = std::make_shared<ImageBuffer>(
                SpritesheetReader::composeLogicalFrame(data->pixels, data->info));

            std::shared_ptr<ImageBuffer> customImg;
            if (wantCustomImage) {
                auto imgRes = ImageBuffer::loadFromFile(customImagePath);
                if (imgRes) {
                    customImg = std::make_shared<ImageBuffer>(std::move(imgRes).unwrap());
                }
            }

            Loader::get()->queueInMainThread(
                [weakSelf, previewGeneration, closed, generation, data, logical,
                 customImg]() {
                if (paimon::isRuntimeShuttingDown() ||
                    closed->load(std::memory_order_acquire) ||
                    previewGeneration->load(std::memory_order_acquire) != generation) {
                    return;
                }
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                self->m_previewPixels = std::make_shared<ImageBuffer>(data->pixels);
                self->m_previewFrameInfo = data->info;
                self->m_customImage = customImg;
                if (auto* spr = SpritePreviewRenderer::createSprite(*logical)) {
                    self->setOriginalSprite(spr);
                }
                self->refreshPreviewTint();
            });
        });
}

void ProjectEditorLayer::refreshPreviewTint() {
    this->unschedule(schedule_selector(ProjectEditorLayer::renderPreviewAfterDelay));
    if (m_previewPixels && !m_previewPixels->empty()) {
        this->scheduleOnce(
            schedule_selector(ProjectEditorLayer::renderPreviewAfterDelay), 0.1f);
    }
}

void ProjectEditorLayer::renderPreviewAfterDelay(float) {
    if (!m_previewPixels || m_previewPixels->empty()) return;

    int generation = m_renderGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
    auto renderGeneration = m_renderGeneration;
    auto closed = m_closed;
    auto pixels = m_previewPixels;
    auto customImg = m_customImage;
    auto frameInfo = m_previewFrameInfo;
    auto fusionMask = m_fusionMask;
    auto fusionAsset = m_fusionAsset;
    std::size_t fusionFrame = m_fusionFrameIndex;

    SpritePreviewOptions opts = makePreviewOptions();
    SpriteSetting setting = currentSetting();
    FusionApplyOptions fusionOpts = makeFusionOptions(setting);
    bool hasSelection = m_hasSelection;
    bool globalWouldTint = true;
    if (hasSelection) {
        globalWouldTint = UiSpriteCatalog::shouldTint(m_selected.kind, m_project.tintScope);
    }

    WeakRef<ProjectEditorLayer> weakSelf(this);
    paimon::ThreadTracker::get().spawn(
        [weakSelf, renderGeneration, closed, generation, pixels, customImg,
         frameInfo, opts, setting, hasSelection, globalWouldTint,
         fusionMask, fusionAsset, fusionFrame, fusionOpts]() {
        if (paimon::isRuntimeShuttingDown() ||
            closed->load(std::memory_order_acquire)) return;

        SpritePreviewResult preview;
        bool tinted = false;
        bool wantImage = hasSelection && setting.hasCustomImage &&
                         customImg && !customImg->empty();

        if (hasSelection && setting.skip) {
            preview.image = *pixels;
        } else if (wantImage && !setting.imageOverlay) {
            preview.image = SpritePreviewRenderer::renderCustomImage(
                *customImg, pixels->width(), pixels->height(),
                setting.imageTransform);
        } else {
            if (hasSelection && setting.useCustomColors) {
                SpritePreviewOptions custom = opts;
                custom.colors.color1 = setting.color1;
                custom.colors.color2 = setting.color2;
                custom.colors.glow   = setting.colorGlow;
                custom.colors.detail = setting.colorDetail;
                preview = SpritePreviewRenderer::renderTintedWithStats(*pixels, custom);
                tinted = true;
            } else if (!hasSelection || globalWouldTint) {
                preview = SpritePreviewRenderer::renderTintedWithStats(*pixels, opts);
                tinted = true;
            } else {
                preview.image = *pixels;
            }
            if (wantImage && setting.imageOverlay) {
                auto top = SpritePreviewRenderer::renderCustomImage(
                    *customImg, pixels->width(), pixels->height(),
                    setting.imageTransform);
                SpritePreviewRenderer::compositeOver(preview.image, top);
            }
        }

        bool fused = false;
        bool wantFusion = hasSelection && setting.hasFusion
            && fusionMask && !fusionMask->empty()
            && fusionAsset && !fusionAsset->empty();
        // On Fusion tab, also show live stamp after texture pick even before
        // a mask exists? No — need a mask. After paint, stamp pure colours.
        if (wantFusion) {
            FusionEngine::apply(preview.image, *fusionMask,
                fusionAsset->frameAt(fusionFrame), fusionOpts);
            fused = true;
        }

        preview.image = SpritesheetReader::composeLogicalFrame(preview.image, frameInfo);

        auto result = std::make_shared<SpritePreviewResult>(std::move(preview));
        Loader::get()->queueInMainThread(
            [weakSelf, renderGeneration, closed, generation, result, tinted,
             hasSelection, setting, fused]() {
            if (paimon::isRuntimeShuttingDown() ||
                closed->load(std::memory_order_acquire) ||
                renderGeneration->load(std::memory_order_acquire) != generation) {
                return;
            }
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (auto* spr = SpritePreviewRenderer::createSprite(result->image)) {
                self->setResultSprite(spr);
            }
            if (self->m_coverageLbl) {
                if (fused) {
                    self->m_coverageLbl->setString(setting.fusionAnimated
                        ? "fusion (gif)"
                        : "fusion fill");
                } else if (tinted) {
                    auto const& s = result->stats;
                    self->m_coverageLbl->setString(fmt::format(
                        "C1 {:.0f}%  C2 {:.0f}%  Glow {:.0f}%{}",
                        s.color1Coverage * 100.f, s.color2Coverage * 100.f,
                        s.glowCoverage * 100.f, s.needsReview ? "  !" : "").c_str());
                } else if (hasSelection && setting.hasCustomImage) {
                    self->m_coverageLbl->setString("custom image");
                } else {
                    self->m_coverageLbl->setString("vanilla (not tinted)");
                }
            }
        });
    });
}


SpritePreviewOptions ProjectEditorLayer::makePreviewOptions() const {
    SpritePreviewOptions options;
    options.colors.color1 = m_project.color1;
    options.colors.color2 = m_project.color2;
    options.colors.glow   = m_project.colorGlow;
    options.colors.detail = m_project.colorDetail;
    options.brightness    = m_project.brightness;
    options.alternativeGlowOverlay = m_project.alternativeGlowOverlay;
    options.maskSoftness     = m_project.maskSoftness;
    options.clusterPrecision = m_project.clusterPrecision;
    options.edgeCleanup      = m_project.edgeCleanup;
    options.outlineProtect   = m_project.outlineProtect;
    options.saturation       = m_project.saturation;
    options.contrast         = m_project.contrast;
    return options;
}

void ProjectEditorLayer::markEdited(bool affectsPreview) {
    m_project.modifiedAt = nowUnixMs();
    setStatus("Edited (unsaved).");
    if (affectsPreview) refreshPreviewTint();
}

void ProjectEditorLayer::setStatus(std::string const& text) {
    if (m_statusLbl) {
        m_statusLbl->setString(text.c_str());
        m_statusLbl->limitLabelWidth(230.f, 0.3f, 0.12f);
    }
}

void ProjectEditorLayer::onAutoTune(CCObject*) {
    if (!m_previewPixels || m_previewPixels->empty()) {
        Notification::create("Preview still loading; try again in a moment.",
            NotificationIcon::Warning, 2.0f)->show();
        return;
    }
    setStatus("Auto-tuning...");

    SpritePreviewOptions base = makePreviewOptions();
    auto pixels = m_previewPixels;
    auto closed = m_closed;
    WeakRef<ProjectEditorLayer> weakSelf(this);

    paimon::ThreadTracker::get().spawn([weakSelf, closed, pixels, base]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
        auto suggestion = std::make_shared<AutoTuner::Suggestion>(
            AutoTuner::tuneForSprite(*pixels, base));
        Loader::get()->queueInMainThread([weakSelf, closed, suggestion]() {
            if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (!suggestion->changed) {
                self->setStatus(fmt::format("Auto: brightness {} already optimal.",
                    suggestion->suggestedBrightness));
                return;
            }
            self->m_project.brightness = suggestion->suggestedBrightness;
            self->m_project.modifiedAt = nowUnixMs();
            if (self->m_brightnessRow) {
                self->m_brightnessRow->setValue(
                    static_cast<float>(suggestion->suggestedBrightness));
            }
            self->refreshPreviewTint();
            self->setStatus(fmt::format("Auto: brightness -> {} (unsaved).",
                suggestion->suggestedBrightness));
        });
    });
}

void ProjectEditorLayer::onSave(CCObject*) {
    auto r = SlotStore::get().saveSlot(m_project);
    if (!r) {
        Notification::create(("Save failed: " + r.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }
    setStatus("Saved.");
    Notification::create("Slot saved.", NotificationIcon::Success, 1.5f)->show();
}

void ProjectEditorLayer::setBusy(bool busy) {
    auto disableBtn = [busy](CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        btn->setEnabled(!busy);
        if (auto* spr = typeinfo_cast<CCSprite*>(btn->getNormalImage())) {
            spr->setOpacity(busy ? 120 : 255);
        }
    };
    disableBtn(m_genBtn);
    disableBtn(m_saveBtn);
}

void ProjectEditorLayer::onGenerate(CCObject*) {
    if (m_generating->load(std::memory_order_acquire)) {
        return;
    }

    onSave(nullptr);

    auto cfg = m_project.toExportConfig();
    if (cfg.sheets.empty()) {
        Notification::create("No sheets in this slot.",
            NotificationIcon::Warning, 2.0f)->show();
        return;
    }

    auto outPath = SlotPaths::outputZipFile(m_project.id);
    setStatus("Generating...");

    m_generating->store(true, std::memory_order_release);
    setBusy(true);

    // Capture everything the thread needs by value; nothing here touches
    // `this`. We return to the UI via WeakRef + queueInMainThread; if the
    // layer is popped mid-export we just lose the callback (the zip still writes).
    WeakRef<ProjectEditorLayer> weakSelf(this);
    auto generating = m_generating;  // shared_ptr copied for the thread
    std::string projectId = m_project.id;
    PackExportConfig cfgCopy = cfg;
    std::filesystem::path outPathCopy = outPath;

    paimon::ThreadTracker::get().spawn([weakSelf, generating, projectId, cfgCopy, outPathCopy]() {
        if (paimon::isRuntimeShuttingDown()) {
            generating->store(false, std::memory_order_release);
            return;
        }

         // Keep packing and encoding off the UI thread; downloads can be large.
        auto progressCb = [weakSelf](int idx, int total, std::string const& name) {
            if (paimon::isRuntimeShuttingDown()) return;
            std::string label = name.empty()
                ? fmt::format("Processing {}/{}...", idx, total)
                : fmt::format("[{}/{}] {}", idx + 1, total, name);
            Loader::get()->queueInMainThread([weakSelf, label]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (auto self = weakSelf.lock(); self && self->getParent()) {
                    self->setStatus(label);
                }
            });
        };

        geode::Result<PackExportResult> result = Err("not started");
        try {
            result = PackExporter::exportPack(cfgCopy, outPathCopy, progressCb);
        } catch (std::exception const& e) {
            result = Err(std::string("exception: ") + e.what());
        } catch (...) {
            result = Err("unknown exception during export");
        }

        if (paimon::isRuntimeShuttingDown()) {
            generating->store(false, std::memory_order_release);
            return;
        }

        auto resultPtr = std::make_shared<geode::Result<PackExportResult>>(std::move(result));

        Loader::get()->queueInMainThread([weakSelf, generating, projectId, resultPtr]() mutable {
            generating->store(false, std::memory_order_release);

            if (paimon::isRuntimeShuttingDown()) return;

            auto self = weakSelf.lock();
            if (!self || !self->getParent()) {
                if (resultPtr && *resultPtr) {
                    auto loaded = SlotStore::get().loadSlot(projectId);
                    if (loaded) {
                        auto p = loaded.unwrap();
                        p.hasBuiltOnce  = true;
                        p.lastBuiltAt   = nowUnixMs();
                        p.lastZipRelPath = "output/pack.zip";
                        (void)SlotStore::get().saveSlot(p);
                    }
                }
                return;
            }

            self->setBusy(false);

            if (!resultPtr || !*resultPtr) {
                self->setStatus("Generate failed.");
                std::string err = resultPtr ? resultPtr->unwrapErr() : std::string("internal error");
                Notification::create(("Failed: " + err).c_str(),
                    NotificationIcon::Error, 4.0f)->show();
                return;
            }
            auto exportRes = resultPtr->unwrap();

            self->m_project.hasBuiltOnce  = true;
            self->m_project.lastBuiltAt   = nowUnixMs();
            self->m_project.lastZipRelPath = "output/pack.zip";
            (void)SlotStore::get().saveSlot(self->m_project);

            self->setStatus(
                "Generated " + std::to_string(exportRes.outputZipSizeBytes / 1024) + " KB"
                + (exportRes.animatedFusionCount > 0
                    ? fmt::format(" (+{} anim GIF)", exportRes.animatedFusionCount)
                    : ""));
            if (!exportRes.precisionNote.empty()) {
                Notification::create(
                    "Pack generated with auto-detection (PackGen assets offline).",
                    NotificationIcon::Warning, 4.0f)->show();
            } else if (exportRes.animatedFusionCount > 0) {
                Notification::create(
                    fmt::format(
                        "Pack ready with {} animated GIF(s). Needs ImagePlus in-game.",
                        exportRes.animatedFusionCount).c_str(),
                    NotificationIcon::Success, 4.0f)->show();
            } else {
                Notification::create("Pack generated! Apply it from the pack list.",
                    NotificationIcon::Success, 3.0f)->show();
            }
        });
    });
}

 }
