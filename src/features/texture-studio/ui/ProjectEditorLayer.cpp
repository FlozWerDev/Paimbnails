#include "ProjectEditorLayer.hpp"

#include "../engine/ColorPresets.hpp"
#include "../engine/PackExporter.hpp"
#include "../engine/SelfTest.hpp"
#include "../engine/AutoTuner.hpp"
#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "ParamSliderRow.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kLeftCenterX = 62.f;
constexpr float kCenterX0    = 122.f;
constexpr float kRightPanelW = 202.f;

constexpr float kCellW = 72.f;
constexpr float kCellH = 58.f;

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

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Creation / lifecycle

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
    if (auto* s = ProjectEditorLayer::scene(std::move(slotId))) {
        CCDirector::get()->pushScene(CCTransitionFade::create(0.35f, s));
    }
}

ProjectEditorLayer::~ProjectEditorLayer() {
    m_closed->store(true, std::memory_order_release);
    m_previewGeneration->fetch_add(1, std::memory_order_acq_rel);
    m_renderGeneration->fetch_add(1, std::memory_order_acq_rel);
    m_thumbGeneration->fetch_add(1, std::memory_order_acq_rel);
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

    // No selection yet: preview the pack's representative UI sprite.
    startSelectionPixelLoad();

    return true;
}

void ProjectEditorLayer::keyBackClicked() {
    onBack(nullptr);
}

void ProjectEditorLayer::onBack(CCObject*) {
    // Autosave: leaving the editor should never silently lose work.
    (void)SlotStore::get().saveSlot(m_project);
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}

// ---------------------------------------------------------------------------
// Static layout

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

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    if (auto* backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        backSpr->setScale(0.85f);
        if (auto* backBtn = CCMenuItemExt::createSpriteExtra(backSpr,
                [this](CCMenuItemSpriteExtra*) { this->onBack(nullptr); })) {
            backBtn->setPosition({24.f, winSize.height - 22.f});
            menu->addChild(backBtn);
        }
    }

    if (auto* title = CCLabelBMFont::create(m_project.name.c_str(), "bigFont.fnt")) {
        title->limitLabelWidth(220.f, 0.6f, 0.3f);
        title->setPosition({winSize.width / 2.f, winSize.height - 18.f});
        this->addChild(title, 5);
    }

    if (auto* autoSpr = ButtonSprite::create("Auto", "bigFont.fnt", "GJ_button_02.png", 0.32f)) {
        if (auto* autoBtn = CCMenuItemExt::createSpriteExtra(autoSpr,
                [this](CCMenuItemSpriteExtra*) { this->onAutoTune(nullptr); })) {
            autoBtn->setPosition({winSize.width - 40.f, winSize.height - 20.f});
            menu->addChild(autoBtn);
        }
    }
}

void ProjectEditorLayer::buildFooter() {
    auto winSize = CCDirector::get()->getWinSize();

    if (auto* status = CCLabelBMFont::create("Ready.", "bigFont.fnt")) {
        status->setScale(0.3f);
        status->setAnchorPoint({0.f, 0.5f});
        status->setColor({170, 190, 175});
        status->setPosition({12.f, 18.f});
        this->addChild(status, 5);
        m_statusLbl = status;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    if (auto* saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_05.png", 0.4f)) {
        if (auto* saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr,
                [this](CCMenuItemSpriteExtra*) { this->onSave(nullptr); })) {
            saveBtn->setPosition({winSize.width - 138.f, 18.f});
            menu->addChild(saveBtn);
            m_saveBtn = saveBtn;
        }
    }
    if (auto* genSpr = ButtonSprite::create("Generate", "goldFont.fnt", "GJ_button_01.png", 0.4f)) {
        if (auto* genBtn = CCMenuItemExt::createSpriteExtra(genSpr,
                [this](CCMenuItemSpriteExtra*) { this->onGenerate(nullptr); })) {
            genBtn->setPosition({winSize.width - 62.f, 18.f});
            menu->addChild(genBtn);
            m_genBtn = genBtn;
        }
    }
}

void ProjectEditorLayer::buildPreviewPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    constexpr float kBox = 84.f;

    if (auto* nameLbl = CCLabelBMFont::create("(pack preview)", "chatFont.fnt")) {
        nameLbl->setScale(0.42f);
        nameLbl->setColor({185, 190, 200});
        nameLbl->limitLabelWidth(108.f, 0.42f, 0.2f);
        nameLbl->setPosition({kLeftCenterX, winSize.height - 46.f});
        this->addChild(nameLbl, 5);
        m_previewNameLbl = nameLbl;
    }

    auto makeBox = [this](float cy, char const* caption) -> CCNode* {
        auto* host = CCNode::create();
        host->setContentSize({kBox, kBox});
        host->setAnchorPoint({0.5f, 0.5f});
        host->setPosition({kLeftCenterX, cy});
        this->addChild(host, 5);

        if (auto* frame = CCScale9Sprite::create("GJ_square01.png")) {
            frame->setContentSize({kBox, kBox});
            frame->setColor({26, 26, 32});
            host->addChildAtPosition(frame, Anchor::Center);
        }
        if (auto* lbl = CCLabelBMFont::create(caption, "bigFont.fnt")) {
            lbl->setScale(0.28f);
            lbl->setColor({170, 170, 180});
            host->addChildAtPosition(lbl, Anchor::Top, {0.f, 8.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.35f);
            loading->setColor({120, 120, 130});
            loading->setID("loading-label");
            host->addChildAtPosition(loading, Anchor::Center, {0.f, -4.f});
        }
        return host;
    };

    m_originalHost = makeBox(winSize.height - 102.f, "Original");
    m_resultHost   = makeBox(winSize.height - 196.f, "Result");

    if (auto* coverage = CCLabelBMFont::create("Loading...", "chatFont.fnt")) {
        coverage->setScale(0.38f);
        coverage->setColor({185, 190, 200});
        coverage->limitLabelWidth(110.f, 0.38f, 0.18f);
        coverage->setPosition({kLeftCenterX, winSize.height - 248.f});
        this->addChild(coverage, 5);
        m_coverageLbl = coverage;
    }
}

void ProjectEditorLayer::buildBrowserPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    const float x1 = winSize.width - kRightPanelW - 16.f;
    const float centerW = x1 - kCenterX0;

    if (auto* search = TextInput::create(120.f, "Search...")) {
        search->setMaxCharCount(32);
        search->setScale(0.72f);
        search->setID("sprite-search"_spr);
        search->setCallback([this](std::string const& text) {
            m_search = toLowerCopy(text);
            applyFilter();
        });
        search->setPosition({kCenterX0 + 46.f, winSize.height - 52.f});
        this->addChild(search, 6);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    const float filterRowX0 = kCenterX0 + 100.f;
    const float filterRowW = std::max(60.f, x1 - filterRowX0 - 4.f);
    auto* filterRow = CCMenu::create();
    filterRow->setContentSize({filterRowW, 20.f});
    filterRow->setAnchorPoint({0.f, 0.5f});
    filterRow->setPosition({filterRowX0, winSize.height - 52.f});
    filterRow->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(true));
    this->addChild(filterRow, 10);

    auto makeFilterBtn = [this, filterRow](char const* label, int mode)
            -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.3f);
        if (!spr) return nullptr;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, mode](CCMenuItemSpriteExtra*) {
                m_filterMode = mode;
                refreshFilterButtons();
                applyFilter();
            });
        if (btn) filterRow->addChild(btn);
        return btn;
    };
    m_filterButtonsBtn = makeFilterBtn("Buttons", 0);
    m_filterAllUiBtn   = makeFilterBtn("All UI",  1);
    m_filterEditedBtn  = makeFilterBtn("Edited",  2);
    filterRow->updateLayout();

    const float gridTop = winSize.height - 66.f;
    const float gridBottom = 56.f;
    const float gridH = gridTop - gridBottom;
    m_gridCols = std::max(2, static_cast<int>(centerW / kCellW));
    m_gridRows = std::max(2, static_cast<int>(gridH / kCellH));

    auto* gridHost = CCNode::create();
    gridHost->setContentSize({m_gridCols * kCellW, m_gridRows * kCellH});
    gridHost->setAnchorPoint({0.5f, 1.f});
    gridHost->setPosition({kCenterX0 + centerW / 2.f, gridTop});
    this->addChild(gridHost, 5);
    m_gridHost = gridHost;

    // Pagination row under the grid: arrows at the grid edges, page number on
    // the left, sprite/edited count on the right. Everything on one line but
    // with disjoint horizontal spans so nothing can overlap.
    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.5f);
        if (auto* prevBtn = CCMenuItemExt::createSpriteExtra(prevSpr,
                [this](CCMenuItemSpriteExtra*) {
                    if (m_page > 0) { --m_page; rebuildGrid(); }
                })) {
            prevBtn->setPosition({kCenterX0 + 14.f, 40.f});
            menu->addChild(prevBtn);
        }
    }
    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setScale(0.5f);
        nextSpr->setFlipX(true);
        if (auto* nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int perPage = m_gridCols * m_gridRows;
                    int pages = std::max(1,
                        (static_cast<int>(m_filtered.size()) + perPage - 1) / perPage);
                    if (m_page + 1 < pages) { ++m_page; rebuildGrid(); }
                })) {
            nextBtn->setPosition({x1 - 14.f, 40.f});
            menu->addChild(nextBtn);
        }
    }
    if (auto* pageLbl = CCLabelBMFont::create("1 / 1", "bigFont.fnt")) {
        pageLbl->setScale(0.32f);
        pageLbl->setAnchorPoint({0.f, 0.5f});
        pageLbl->setPosition({kCenterX0 + 32.f, 40.f});
        this->addChild(pageLbl, 5);
        m_pageLbl = pageLbl;
    }
    if (auto* countLbl = CCLabelBMFont::create("", "bigFont.fnt")) {
        countLbl->setScale(0.26f);
        countLbl->setColor({170, 170, 180});
        countLbl->setAnchorPoint({1.f, 0.5f});
        countLbl->setPosition({x1 - 32.f, 40.f});
        this->addChild(countLbl, 5);
        m_countLbl = countLbl;
    }
}

void ProjectEditorLayer::buildTabsPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    const float panelX = winSize.width - kRightPanelW - 8.f;
    const float panelTop = winSize.height - 58.f;
    const float panelBottom = 40.f;
    const float panelH = panelTop - panelBottom;

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
    tabRow->setContentSize({kRightPanelW, 22.f});
    tabRow->setAnchorPoint({0.5f, 0.5f});
    tabRow->setPosition({panelX + kRightPanelW / 2.f, winSize.height - 44.f});
    tabRow->setLayout(
        RowLayout::create()
            ->setGap(3.f)
            ->setAxisAlignment(AxisAlignment::Even)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(true));
    this->addChild(tabRow, 10);

    for (int i = 0; i < 4; ++i) {
        if (auto* spr = ButtonSprite::create(defs[i].label, "bigFont.fnt",
                                             "GJ_button_04.png", 0.3f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [this, i](CCMenuItemSpriteExtra*) { this->selectTab(i); })) {
                tabRow->addChild(btn);
                m_tabBtns[i] = btn;
            }
        }

        auto* tab = CCNode::create();
        tab->setContentSize({kRightPanelW, panelH});
        tab->setAnchorPoint({0.f, 0.f});
        tab->setPosition({panelX, panelBottom});
        this->addChild(tab, 5);
        m_tabs[i] = tab;

        if (auto* bg = CCScale9Sprite::create("square02b_001.png")) {
            bg->setContentSize({kRightPanelW, panelH});
            bg->setColor({0, 0, 0});
            bg->setOpacity(90);
            tab->addChildAtPosition(bg, Anchor::Center);
        }

        (this->*(defs[i].builder))(tab, kRightPanelW, panelH);
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

// ---------------------------------------------------------------------------
// Tab contents

void ProjectEditorLayer::buildPackTab(CCNode* tab, float w, float h) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({w, h});
    tab->addChild(menu);

    if (auto* caption = CCLabelBMFont::create("Pack colors", "goldFont.fnt")) {
        caption->setScale(0.42f);
        caption->setPosition({w / 2.f, h - 14.f});
        tab->addChild(caption);
    }

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
    for (int i = 0; i < 4; ++i) {
        float x = 34.f + static_cast<float>(i) * 44.f;
        auto* swatch = makeSwatchSprite(swatches[i].initial, 26.f);
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
        btn->setPosition({x, h - 44.f});
        menu->addChild(btn);
        if (auto* cap = CCLabelBMFont::create(swatches[i].caption, "bigFont.fnt")) {
            cap->setScale(0.22f);
            cap->setColor({170, 170, 180});
            cap->setPosition({x, h - 62.f});
            tab->addChild(cap);
        }
    }

    auto* brightnessRow = ParamSliderRow::create("Brightness", 100.f, 300.f, 1.f,
        static_cast<float>(m_project.brightness), w - 16.f,
        [this](float v) {
            m_project.brightness = static_cast<int>(std::lround(v));
            markEdited(true);
        }, nullptr);
    if (brightnessRow) {
        brightnessRow->setPosition({8.f, h - 90.f});
        tab->addChild(brightnessRow);
        m_brightnessRow = brightnessRow;
    }

    if (auto* hint1 = CCLabelBMFont::create(
            "White = inner white details (kept vanilla by default)", "chatFont.fnt")) {
        hint1->setColor({150, 155, 165});
        hint1->limitLabelWidth(w - 16.f, 0.42f, 0.15f);
        hint1->setPosition({w / 2.f, h - 116.f});
        tab->addChild(hint1);
    }
    if (auto* hint2 = CCLabelBMFont::create(
            "Brightness: lower = stronger color", "chatFont.fnt")) {
        hint2->setColor({150, 155, 165});
        hint2->limitLabelWidth(w - 16.f, 0.42f, 0.15f);
        hint2->setPosition({w / 2.f, h - 132.f});
        tab->addChild(hint2);
    }

    if (auto* presetSpr = ButtonSprite::create("Next Preset", "bigFont.fnt", "GJ_button_05.png", 0.32f)) {
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
            presetBtn->setPosition({w / 2.f, h - 162.f});
            menu->addChild(presetBtn);
        }
    }

    if (auto* creditsSpr = ButtonSprite::create("Credits", "bigFont.fnt", "GJ_button_05.png", 0.3f)) {
        if (auto* creditsBtn = CCMenuItemExt::createSpriteExtra(creditsSpr,
                [](CCMenuItemSpriteExtra*) {
                    geode::createQuickPopup(
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
                        });
                })) {
            creditsBtn->setPosition({w / 2.f, 20.f});
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
    // Two-column grid: left = classic PackGen options, right = the
    // asset-pack precision extras.
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
                // isToggled() is the state BEFORE this tap lands.
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
        nameLbl->setPosition({8.f, h - 10.f});
        tab->addChild(nameLbl);
        m_spriteNameLbl = nameLbl;
    }
    if (auto* resetSpr = ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.24f)) {
        if (auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
                [this](CCMenuItemSpriteExtra*) { this->onResetSprite(); })) {
            resetBtn->setAnchorPoint({1.f, 0.5f});
            resetBtn->setPosition({w - 6.f, h - 12.f});
            menu->addChild(resetBtn);
        }
    }

    auto* modeRow = CCMenu::create();
    modeRow->setContentSize({w - 16.f, 20.f});
    modeRow->setAnchorPoint({0.5f, 0.5f});
    modeRow->setPosition({w / 2.f, h - 30.f});
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
                rebuildGrid();  // badge letters depend on the mode
                refreshPreviewTint();
            });
        if (btn) modeRow->addChild(btn);
        return btn;
    };
    m_modeGlobalBtn = makeModeBtn("Global", 0);
    m_modeCustomBtn = makeModeBtn("Custom", 1);
    m_modeSkipBtn   = makeModeBtn("Skip",   2);
    modeRow->updateLayout();

    // Per-sprite color swatches (only active in Custom mode).
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
        btn->setPosition({x, h - 52.f});
        menu->addChild(btn);
        if (auto* cap = CCLabelBMFont::create(swatches[i].caption, "bigFont.fnt")) {
            cap->setScale(0.18f);
            cap->setColor({170, 170, 180});
            cap->setPosition({x, h - 66.f});
            tab->addChild(cap);
        }
    }

    // Image row: Pick / Clear / Fit / Replace-Overlay, evenly spaced.
    auto* imageRow = CCMenu::create();
    imageRow->setContentSize({w - 16.f, 20.f});
    imageRow->setAnchorPoint({0.5f, 0.5f});
    imageRow->setPosition({w / 2.f, h - 84.f});
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
        stateLbl->setPosition({w / 2.f, h - 98.f});
        tab->addChild(stateLbl);
        m_imageStateLbl = stateLbl;
    }

    // Flip row: Flip X / Flip Y togglers side by side.
    auto* flipRow = CCMenu::create();
    flipRow->setContentSize({w - 16.f, 20.f});
    flipRow->setAnchorPoint({0.5f, 0.5f});
    flipRow->setPosition({w / 2.f, h - 112.f});
    flipRow->setLayout(
        RowLayout::create()
            ->setGap(10.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false));
    tab->addChild(flipRow);

    auto makeFlip = [this, flipRow](char const* label,
                                    CCMenuItemToggler*& out,
                                    std::function<void(SpriteSetting&, bool)> apply) {
        auto* tog = CCMenuItemExt::createTogglerWithStandardSprites(0.38f,
            [this, apply](CCMenuItemToggler* t) {
                if (!t || !m_hasSelection) return;
                auto s = currentSetting();
                // isToggled() is the state BEFORE this tap lands.
                apply(s, !t->isToggled());
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
    makeFlip("Flip X", m_flipXTog,
        [](SpriteSetting& s, bool v) { s.imageTransform.flipX = v; });
    makeFlip("Flip Y", m_flipYTog,
        [](SpriteSetting& s, bool v) { s.imageTransform.flipY = v; });
    flipRow->updateLayout();

    // Image transform sliders.
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

    float y = h - 130.f;
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
        y -= 19.f;
    }

    refreshSpriteTabUi();
}

// ---------------------------------------------------------------------------
// Sprite settings plumbing

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
        m_spriteNameLbl->limitLabelWidth(kRightPanelW - 60.f, 0.42f, 0.2f);
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
    // Button labels above may have changed width; re-flow the row so the
    // Pick/Clear/Fit/Mode buttons never overlap each other.
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
        m_imageStateLbl->setString(!setting.hasCustomImage ? "no image"
            : (setting.imageOverlay ? "image: overlaid on sprite"
                                    : "image: replaces sprite"));
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
                // Selection may have moved while the dialog was open.
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

void ProjectEditorLayer::onResetSprite() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    bool hadImage = s.hasCustomImage;
    m_project.spriteSettings.erase(m_selected.frameName);
    m_project.modifiedAt = nowUnixMs();
    m_customImage.reset();
    if (hadImage) {
        std::error_code ec;
        std::filesystem::remove(
            SlotPaths::spriteImageFile(m_slotId, m_selected.frameName), ec);
    }
    setStatus("Sprite reset (unsaved).");
    refreshSpriteTabUi();
    rebuildGrid();
    refreshPreviewTint();
}

// ---------------------------------------------------------------------------
// Browser

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
        // Keep clear of the left-anchored page label on narrow windows.
        auto winSize = CCDirector::get()->getWinSize();
        const float x1 = winSize.width - kRightPanelW - 16.f;
        m_countLbl->limitLabelWidth(
            std::max(50.f, x1 - kCenterX0 - 140.f), 0.26f, 0.12f);
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
            char const* badgeText = s.skip ? "S" : (s.hasCustomImage ? "I" : "C");
            ccColor3B badgeColor = s.skip ? ccColor3B{235, 90, 90}
                                 : (s.hasCustomImage ? ccColor3B{190, 120, 255}
                                                     : ccColor3B{120, 230, 130});
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
    m_hasSelection = true;
    m_selected = entry;
    highlightSelectedCell();
    refreshSpriteTabUi();
    selectTab(3);
    if (m_previewNameLbl) {
        m_previewNameLbl->setString(entry.frameName.c_str());
        m_previewNameLbl->limitLabelWidth(108.f, 0.42f, 0.2f);
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

// ---------------------------------------------------------------------------
// Preview

void ProjectEditorLayer::setOriginalSprite(CCSprite* spr) {
    if (!m_originalHost || !spr) return;
    if (m_originalSpr) m_originalSpr->removeFromParent();
    if (auto* loading = m_originalHost->getChildByID("loading-label")) {
        loading->removeFromParent();
    }
    fitSpriteIntoBox(spr, 84.f);
    m_originalHost->addChildAtPosition(spr, Anchor::Center, {0.f, -4.f});
    m_originalSpr = spr;
}

void ProjectEditorLayer::setResultSprite(CCSprite* spr) {
    if (!m_resultHost || !spr) return;
    if (m_resultSpr) m_resultSpr->removeFromParent();
    if (auto* loading = m_resultHost->getChildByID("loading-label")) {
        loading->removeFromParent();
    }
    fitSpriteIntoBox(spr, 84.f);
    m_resultHost->addChildAtPosition(spr, Anchor::Center, {0.f, -4.f});
    m_resultSpr = spr;
}

void ProjectEditorLayer::startSelectionPixelLoad() {
    // Resolve the preview target: the selected sprite, else the pack's
    // representative UI frame.
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

    SpritePreviewOptions opts = makePreviewOptions();
    SpriteSetting setting = currentSetting();
    bool hasSelection = m_hasSelection;
    bool globalWouldTint = true;
    if (hasSelection) {
        globalWouldTint = UiSpriteCatalog::shouldTint(m_selected.kind, m_project.tintScope);
    }

    WeakRef<ProjectEditorLayer> weakSelf(this);
    paimon::ThreadTracker::get().spawn(
        [weakSelf, renderGeneration, closed, generation, pixels, customImg,
         frameInfo, opts, setting, hasSelection, globalWouldTint]() {
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
        preview.image = SpritesheetReader::composeLogicalFrame(preview.image, frameInfo);

        auto result = std::make_shared<SpritePreviewResult>(std::move(preview));
        Loader::get()->queueInMainThread(
            [weakSelf, renderGeneration, closed, generation, result, tinted,
             hasSelection, setting]() {
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
                if (tinted) {
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

// ---------------------------------------------------------------------------
// Actions

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
    // Reentrancy guard: setBusy disables the button but a fast tap can slip in.
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

        // CPU-bound work runs off the main thread: SheetTinter / RectPacker /
        // PNG encode are pure RAM (no GL/cocos2d). Without this, multi-sheet
        // packs hang the main thread and Windows kills the process.
        // Progress matters here: the first precision export downloads the
        // PackGen asset pack (~200 small files) and would look frozen.
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
            // Clear busy *before* touching the layer so the flag reflects
            // reality even if the layer is already gone.
            generating->store(false, std::memory_order_release);

            if (paimon::isRuntimeShuttingDown()) return;

            auto self = weakSelf.lock();
            if (!self || !self->getParent()) {
                // Layer popped mid-generation: zip is written, but we can't
                // refresh the UI — persist the "built" state via SlotStore.
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
                "Generated " + std::to_string(exportRes.outputZipSizeBytes / 1024) + " KB");
            if (!exportRes.precisionNote.empty()) {
                // Requested precision but the asset pack was unreachable.
                Notification::create(
                    "Pack generated with auto-detection (PackGen assets offline).",
                    NotificationIcon::Warning, 4.0f)->show();
            } else {
                Notification::create("Pack generated! Apply it from the pack list.",
                    NotificationIcon::Success, 3.0f)->show();
            }
        });
    });
}

}  // namespace paimon::texture_studio
