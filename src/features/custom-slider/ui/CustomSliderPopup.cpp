#include "CustomSliderPopup.hpp"
#include "../services/CustomSliderManager.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <fmt/format.h>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::slider;

static const char* kIconTypeNames[] = { "Cube", "Ship", "Ball", "UFO", "Wave", "Robot", "Spider", "Swing" };
static constexpr int kIconTypeCount = 8;
static const char* kModeNames[] = { "Icon", "Image", "GIF" };
static constexpr int kModeCount = 3;
static const char* kAnimTypeNames[] = { "None", "Bounce", "Rotate", "Both" };
static constexpr int kAnimTypeCount = 4;

namespace {
float sliderNorm(float val, float mn, float mx) { return (mx <= mn) ? 0.f : std::clamp((val - mn) / (mx - mn), 0.f, 1.f); }
float sliderDenorm(float n, float mn, float mx) { return mn + n * (mx - mn); }
}

CustomSliderPopup* CustomSliderPopup::create() {
    auto* ret = new CustomSliderPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomSliderPopup::init() {
    if (!Popup::init(380.f, 270.f)) return false;
    this->setTitle("Custom Slider");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // ═══ Tab buttons at top ═══
    auto* tabMenu = CCMenu::create();
    tabMenu->setPosition({0, 0});
    m_mainLayer->addChild(tabMenu, 20);

    float tabY = content.height - 26.f;
    float tabSpacing = 72.f;
    const char* tabNames[] = { "General", "Animation", "Targets" };

    for (int i = 0; i < 3; i++) {
        auto* spr = ButtonSprite::create(tabNames[i], "bigFont.fnt", "GJ_button_04.png", 0.7f);
        spr->setScale(0.45f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CustomSliderPopup::onTab));
        btn->setPosition({cx - 20.f + (i - 1) * tabSpacing, tabY});
        btn->setTag(i);
        tabMenu->addChild(btn);
        if (i == 0) m_tabBtn0 = btn;
        else if (i == 1) m_tabBtn1 = btn;
        else m_tabBtn2 = btn;
    }

    // ═══ Preview (top-right corner, inside popup bounds) ═══
    {
        float px = content.width - 30.f, py = content.height - 26.f;
        auto* bg = paimon::SpriteHelper::createDarkPanel(34.f, 34.f, 80, 4.f);
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({px, py});
        m_mainLayer->addChild(bg, 4);
        m_previewNode = CCNode::create();
        m_previewNode->setPosition({px, py});
        m_mainLayer->addChild(m_previewNode, 5);
    }

    // ═══ Tab content area ═══
    constexpr float kTopReserve    = 52.f; // tabs + padding
    constexpr float kBottomReserve = 24.f; // reset button + padding
    float tabAreaH = content.height - kTopReserve - kBottomReserve;
    float tabAreaY = kBottomReserve;

    auto makeTab = [&]() {
        auto* n = CCNode::create();
        n->setContentSize({content.width, tabAreaH});
        n->setAnchorPoint({0.f, 0.f});
        n->setPosition({0.f, tabAreaY});
        m_mainLayer->addChild(n, 10);
        return n;
    };
    m_tabGeneral = makeTab();
    m_tabAnim    = makeTab();
    m_tabTargets = makeTab();

    buildGeneralTab();
    buildAnimTab();
    buildTargetsTab();

    // ═══ Reset button (bottom-center, always visible, outside tabs) ═══
    {
        auto* spr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        spr->setScale(0.4f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CustomSliderPopup::onReset));
        btn->setPosition({cx, 12.f});
        tabMenu->addChild(btn);
    }

    switchTab(0);
    refreshPreview();
    return true;
}

// ──────────────────────────────────────────────────────────────────────
// Layout helpers (file-local).
//
// Each "row" is an independent CCNode of fixed height that owns its labels,
// arrows, sliders and toggles via absolute child positions. Menu items live
// inside a per-row CCMenu so they move and hide together with the row.
// All rows are stacked by a parent ColumnLayout that has
// `ignoreInvisibleChildren(true)`, so toggling a row off (e.g. switching to
// Image mode) collapses the empty space automatically.
// ──────────────────────────────────────────────────────────────────────

namespace {
constexpr float kRowH      = 22.f;   // visual row height
constexpr float kPadX      = 20.f;   // inner horizontal padding for tabs

struct Row {
    CCNode*  node;   // outer container, added to the column
    CCMenu*  menu;   // covers the row, holds CCMenuItems
    float    width;
    float    height;
    float    mid() const { return height / 2.f; }
};

Row makeRow(float width, float height = kRowH) {
    auto* node = CCNode::create();
    node->setContentSize({width, height});
    node->setAnchorPoint({0.f, 0.5f});

    auto* menu = CCMenu::create();
    menu->setContentSize({width, height});
    menu->setAnchorPoint({0.f, 0.f});
    menu->ignoreAnchorPointForPosition(false);
    menu->setPosition({0.f, 0.f});
    node->addChild(menu, 5);

    return Row{ node, menu, width, height };
}

cocos2d::CCLabelBMFont* makeLabel(const char* text, float scale = 0.4f) {
    auto* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
    lbl->setScale(scale);
    lbl->setAnchorPoint({0.f, 0.5f});
    return lbl;
}

// Column container that auto-stacks visible children top-to-bottom and skips
// the gap when a child is invisible.
CCNode* makeColumn(const CCSize& size, float gap = 4.f) {
    auto* col = CCNode::create();
    col->setContentSize(size);
    col->setAnchorPoint({0.5f, 1.f});
    col->ignoreAnchorPointForPosition(false);
    auto* layout = ColumnLayout::create()
        ->setGap(gap)
        ->setAxisReverse(true)               // first child on top
        ->setAxisAlignment(AxisAlignment::Start)
        ->setCrossAxisAlignment(AxisAlignment::Start)
        ->setCrossAxisLineAlignment(AxisAlignment::Start)
        ->setAutoScale(false)
        ->setGrowCrossAxis(false);
    layout->ignoreInvisibleChildren(true);
    col->setLayout(layout);
    return col;
}
} // namespace

void CustomSliderPopup::buildGeneralTab() {
    auto& cfg = CustomSliderManager::get().config();
    auto content = m_tabGeneral->getContentSize();

    // Column container for rows. Sits inside the tab's reserved area.
    float colW = content.width - kPadX * 2.f;
    m_generalColumn = makeColumn({colW, content.height - 4.f}, 2.f);
    m_generalColumn->setPosition({content.width / 2.f, content.height - 2.f});
    m_tabGeneral->addChild(m_generalColumn, 1);

    // Coordinates inside each row
    const float labelX  = 8.f;
    const float rightX  = colW - 30.f;          // right edge for toggles (well inside popup)
    const float valueX  = colW - 80.f;          // arrow-cycler value label X
    const float arrLftX = colW - 120.f;         // left arrow
    const float arrRgtX = colW - 50.f;          // right arrow

    // ── Enable ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Enable");
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_enableToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(CustomSliderPopup::onToggleEnabled), 0.6f);
        m_enableToggle->setPosition({rightX, r.mid()});
        m_enableToggle->toggle(cfg.enabled);
        r.menu->addChild(m_enableToggle);

        m_rowEnable = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Mode ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Mode");
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_modeLabel = CCLabelBMFont::create(kModeNames[static_cast<int>(cfg.thumbMode)], "bigFont.fnt");
        m_modeLabel->setScale(0.38f);
        m_modeLabel->setPosition({valueX, r.mid()});
        r.node->addChild(m_modeLabel);

        auto* ls = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        ls->setFlipX(true); ls->setScale(0.35f);
        auto* lb = CCMenuItemSpriteExtra::create(ls, this, menu_selector(CustomSliderPopup::onModeLeft));
        lb->setPosition({arrLftX, r.mid()});
        r.menu->addChild(lb);
        auto* rs = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        rs->setScale(0.35f);
        auto* rb = CCMenuItemSpriteExtra::create(rs, this, menu_selector(CustomSliderPopup::onModeRight));
        rb->setPosition({arrRgtX, r.mid()});
        r.menu->addChild(rb);

        m_rowMode = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Icon Type (Icon mode only) ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Icon");
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_iconTypeLabel = CCLabelBMFont::create(kIconTypeNames[static_cast<int>(cfg.iconType)], "bigFont.fnt");
        m_iconTypeLabel->setScale(0.38f);
        m_iconTypeLabel->setPosition({valueX, r.mid()});
        r.node->addChild(m_iconTypeLabel);

        auto* ls = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        ls->setFlipX(true); ls->setScale(0.35f);
        auto* lb = CCMenuItemSpriteExtra::create(ls, this, menu_selector(CustomSliderPopup::onIconTypeLeft));
        lb->setPosition({arrLftX, r.mid()});
        r.menu->addChild(lb);
        auto* rs = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        rs->setScale(0.35f);
        auto* rb = CCMenuItemSpriteExtra::create(rs, this, menu_selector(CustomSliderPopup::onIconTypeRight));
        rb->setPosition({arrRgtX, r.mid()});
        r.menu->addChild(rb);

        m_rowIconType = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Pick Image (Image / GIF mode only) ──
    {
        auto r = makeRow(colW);

        std::string dp = cfg.customImagePath.empty() ? "(no file)" :
            geode::utils::string::pathToString(std::filesystem::path(cfg.customImagePath).filename());
        if (dp.size() > 22) dp = dp.substr(0, 19) + "...";
        m_imagePathLabel = CCLabelBMFont::create(dp.c_str(), "chatFont.fnt");
        m_imagePathLabel->setScale(0.45f);
        m_imagePathLabel->setAnchorPoint({0.f, 0.5f});
        m_imagePathLabel->setPosition({labelX, r.mid()});
        r.node->addChild(m_imagePathLabel);

        auto* pickSpr = ButtonSprite::create("Select", "goldFont.fnt", "GJ_button_01.png", 0.7f);
        pickSpr->setScale(0.45f);
        auto* pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(CustomSliderPopup::onPickImage));
        pickBtn->setPosition({rightX - 10.f, r.mid()});
        r.menu->addChild(pickBtn);

        m_rowImagePick = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Player Icon (Icon mode only) ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Player Icon", 0.38f);
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_playerIconToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(CustomSliderPopup::onTogglePlayerIcon), 0.55f);
        m_playerIconToggle->setPosition({rightX, r.mid()});
        m_playerIconToggle->toggle(cfg.usePlayerIcon);
        r.menu->addChild(m_playerIconToggle);

        m_rowPlayerIcon = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Player Colors (Icon mode only) ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Player Colors", 0.38f);
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_playerColorsToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(CustomSliderPopup::onTogglePlayerColors), 0.55f);
        m_playerColorsToggle->setPosition({rightX, r.mid()});
        m_playerColorsToggle->toggle(cfg.usePlayerColors);
        r.menu->addChild(m_playerColorsToggle);

        m_rowPlayerColors = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Scale slider ──
    {
        auto r = makeRow(colW, 22.f);

        m_scaleLabel = CCLabelBMFont::create(
            fmt::format("Scale: {:.2f}", cfg.iconScale).c_str(), "bigFont.fnt");
        m_scaleLabel->setScale(0.35f);
        m_scaleLabel->setAnchorPoint({0.f, 0.5f});
        m_scaleLabel->setPosition({labelX, r.mid()});
        r.node->addChild(m_scaleLabel);

        m_scaleSlider = Slider::create(this, menu_selector(CustomSliderPopup::onScaleChanged));
        m_scaleSlider->setScale(0.45f);
        // Slider sprite is anchored at its center; place it on the right half.
        m_scaleSlider->setPosition({colW * 0.58f, r.mid()});
        m_scaleSlider->setValue(sliderNorm(cfg.iconScale, 0.10f, 1.0f));
        r.node->addChild(m_scaleSlider);

        m_rowScale = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Container + Border (Image / GIF mode only) ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Container", 0.38f);
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_containerToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(CustomSliderPopup::onToggleContainer), 0.55f);
        m_containerToggle->setPosition({labelX + 70.f, r.mid()});
        m_containerToggle->toggle(cfg.containerEnabled);
        r.menu->addChild(m_containerToggle);

        m_borderToggleLabel = CCLabelBMFont::create("Border", "bigFont.fnt");
        m_borderToggleLabel->setScale(0.38f);
        m_borderToggleLabel->setAnchorPoint({0.f, 0.5f});
        m_borderToggleLabel->setPosition({labelX + 105.f, r.mid()});
        r.node->addChild(m_borderToggleLabel);

        m_borderToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(CustomSliderPopup::onToggleBorder), 0.55f);
        m_borderToggle->setPosition({labelX + 160.f, r.mid()});
        m_borderToggle->toggle(cfg.containerBorderEnabled);
        r.menu->addChild(m_borderToggle);

        m_rowContainer = r.node;
        m_generalColumn->addChild(r.node);
    }

    // ── Shape grid (Image / GIF + container on) ──
    {
        // Shape row is taller because it stacks a title + 2-row grid.
        constexpr float kCell      = 18.f;
        constexpr int   kCols      = 10;     // forces a clean 2x10 grid for 20 shapes
        constexpr float kGridGap   = 2.f;
        const float gridW = kCols * kCell + (kCols - 1) * kGridGap;
        const float gridH = 2.f * kCell + kGridGap;
        const float rowH  = gridH + 14.f;    // + title

        auto r = makeRow(colW, rowH);

        auto* title = makeLabel("Shape", 0.36f);
        title->setPosition({labelX, rowH - 6.f});
        r.node->addChild(title);

        m_shapeGridNode = CCNode::create();
        m_shapeGridNode->setAnchorPoint({0.5f, 0.5f});
        m_shapeGridNode->setContentSize({gridW, gridH});
        m_shapeGridNode->setPosition({colW * 0.5f, gridH * 0.5f + 2.f});
        r.node->addChild(m_shapeGridNode);

        m_shapeGridMenu = CCMenu::create();
        m_shapeGridMenu->setAnchorPoint({0.5f, 0.5f});
        m_shapeGridMenu->ignoreAnchorPointForPosition(false);
        m_shapeGridMenu->setContentSize({gridW, gridH});
        m_shapeGridMenu->setPosition({gridW * 0.5f, gridH * 0.5f});
        m_shapeGridMenu->setLayout(
            RowLayout::create()
                ->setGap(kGridGap)
                ->setGrowCrossAxis(true)
                ->setCrossAxisOverflow(false)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setAutoScale(false)
        );
        m_shapeGridNode->addChild(m_shapeGridMenu);
        rebuildShapeGrid();

        m_rowShape = r.node;
        m_generalColumn->addChild(r.node);
    }

    m_generalColumn->updateLayout();
}

void CustomSliderPopup::buildAnimTab() {
    auto& cfg = CustomSliderManager::get().config();
    auto content = m_tabAnim->getContentSize();

    float colW = content.width - kPadX * 2.f;
    m_animColumn = makeColumn({colW, content.height - 4.f}, 3.f);
    m_animColumn->setPosition({content.width / 2.f, content.height - 2.f});
    m_tabAnim->addChild(m_animColumn, 1);

    const float labelX = 8.f;
    const float rightX = colW - 30.f;
    const float valueX = colW - 80.f;
    const float arrLft = colW - 120.f;
    const float arrRgt = colW - 50.f;

    // ── Animate on Drag ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Animate");
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_animToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(CustomSliderPopup::onToggleAnimate), 0.6f);
        m_animToggle->setPosition({rightX, r.mid()});
        m_animToggle->toggle(cfg.animateOnDrag);
        r.menu->addChild(m_animToggle);

        m_animColumn->addChild(r.node);
    }

    // ── Type ──
    {
        auto r = makeRow(colW);
        auto* lbl = makeLabel("Type");
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        m_animTypeLabel = CCLabelBMFont::create(kAnimTypeNames[static_cast<int>(cfg.animType)], "bigFont.fnt");
        m_animTypeLabel->setScale(0.38f);
        m_animTypeLabel->setPosition({valueX, r.mid()});
        r.node->addChild(m_animTypeLabel);

        auto* ls = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        ls->setFlipX(true); ls->setScale(0.35f);
        auto* lb = CCMenuItemSpriteExtra::create(ls, this, menu_selector(CustomSliderPopup::onAnimTypeLeft));
        lb->setPosition({arrLft, r.mid()});
        r.menu->addChild(lb);
        auto* rs = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        rs->setScale(0.35f);
        auto* rb = CCMenuItemSpriteExtra::create(rs, this, menu_selector(CustomSliderPopup::onAnimTypeRight));
        rb->setPosition({arrRgt, r.mid()});
        r.menu->addChild(rb);

        m_animColumn->addChild(r.node);
    }

    // Helper that builds a "label + slider" row with label on the left
    // and a centered slider on the right half.
    auto addSliderRow = [&](Slider*& outSlider, CCLabelBMFont*& outLabel,
                            const std::string& text, float norm,
                            cocos2d::SEL_MenuHandler sel) {
        auto r = makeRow(colW, 22.f);

        outLabel = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        outLabel->setScale(0.35f);
        outLabel->setAnchorPoint({0.f, 0.5f});
        outLabel->setPosition({labelX, r.mid()});
        r.node->addChild(outLabel);

        outSlider = Slider::create(this, sel);
        outSlider->setScale(0.45f);
        outSlider->setPosition({colW * 0.58f, r.mid()});
        outSlider->setValue(norm);
        r.node->addChild(outSlider);

        m_animColumn->addChild(r.node);
    };

    addSliderRow(m_animDurationSlider, m_animDurationLabel,
        fmt::format("Duration: {:.2f}s", cfg.animDuration),
        sliderNorm(cfg.animDuration, 0.05f, 0.5f),
        menu_selector(CustomSliderPopup::onAnimDurationChanged));

    addSliderRow(m_animBounceSlider, m_animBounceLabel,
        fmt::format("Bounce: {:.0f}%", (cfg.animBounceScale - 1.f) * 100.f),
        sliderNorm(cfg.animBounceScale, 1.0f, 2.0f),
        menu_selector(CustomSliderPopup::onAnimBounceChanged));

    addSliderRow(m_animRotateSlider, m_animRotateLabel,
        fmt::format("Rotate: {:.0f} deg", cfg.animRotateDeg),
        sliderNorm(cfg.animRotateDeg, 5.f, 45.f),
        menu_selector(CustomSliderPopup::onAnimRotateChanged));

    m_animColumn->updateLayout();
}

void CustomSliderPopup::buildTargetsTab() {
    auto& cfg = CustomSliderManager::get().config();
    auto content = m_tabTargets->getContentSize();

    float colW = content.width - kPadX * 2.f;
    auto* col = makeColumn({colW, content.height - 4.f}, 4.f);
    col->setPosition({content.width / 2.f, content.height - 2.f});
    m_tabTargets->addChild(col, 1);

    // Description (separate non-row child; never hidden)
    {
        auto* descRow = CCNode::create();
        descRow->setContentSize({colW, 16.f});
        descRow->setAnchorPoint({0.f, 0.5f});
        auto* desc = CCLabelBMFont::create("Choose which sliders are affected", "chatFont.fnt");
        desc->setScale(0.45f);
        desc->setColor({200, 200, 200});
        desc->setAnchorPoint({0.5f, 0.5f});
        desc->setPosition({colW * 0.5f, 8.f});
        descRow->addChild(desc);
        col->addChild(descRow);
    }

    const float labelX = 18.f;
    const float rightX = colW - 40.f;

    struct ToggleRow { const char* name; bool val; CCMenuItemToggler** toggle; SEL_MenuHandler sel; };
    ToggleRow rows[] = {
        {"Options / Volume", cfg.targets.optionsSliders, &m_optionsToggle, menu_selector(CustomSliderPopup::onToggleOptions)},
        {"Editor",           cfg.targets.editorSliders,  &m_editorToggle,  menu_selector(CustomSliderPopup::onToggleEditor)},
        {"Color Pickers",    cfg.targets.colorSliders,   &m_colorsToggle,  menu_selector(CustomSliderPopup::onToggleColors)},
        {"Garage",           cfg.targets.garageSliders,  &m_garageToggle,  menu_selector(CustomSliderPopup::onToggleGarage)},
    };

    for (auto& row : rows) {
        auto r = makeRow(colW, 24.f);
        auto* lbl = makeLabel(row.name, 0.42f);
        lbl->setPosition({labelX, r.mid()});
        r.node->addChild(lbl);

        *row.toggle = CCMenuItemToggler::createWithStandardSprites(this, row.sel, 0.6f);
        (*row.toggle)->setPosition({rightX, r.mid()});
        (*row.toggle)->toggle(row.val);
        r.menu->addChild(*row.toggle);

        col->addChild(r.node);
    }

    col->updateLayout();
}

void CustomSliderPopup::onExit() {
    CustomSliderManager::get().saveConfig();
    Popup::onExit();
}

void CustomSliderPopup::switchTab(int tab) {
    m_currentTab = tab;
    m_tabGeneral->setVisible(tab == 0);
    m_tabAnim->setVisible(tab == 1);
    m_tabTargets->setVisible(tab == 2);

    // Update tab button appearance
    auto setTabActive = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        btn->setOpacity(active ? 255 : 150);
        btn->setScale(active ? 1.f : 0.9f);
    };
    setTabActive(m_tabBtn0, tab == 0);
    setTabActive(m_tabBtn1, tab == 1);
    setTabActive(m_tabBtn2, tab == 2);

    // Mode-dependent row visibility on the General tab.
    // The ColumnLayout will collapse hidden rows automatically
    // (ignoreInvisibleChildren = true).
    if (tab == 0 && m_generalColumn) {
        auto& cfg = CustomSliderManager::get().config();
        const bool isIconMode  = (cfg.thumbMode == SliderThumbMode::Icon);
        const bool isImageMode = !isIconMode;

        if (m_rowIconType)     m_rowIconType->setVisible(isIconMode);
        if (m_rowPlayerIcon)   m_rowPlayerIcon->setVisible(isIconMode);
        if (m_rowPlayerColors) m_rowPlayerColors->setVisible(isIconMode);
        if (m_rowImagePick)    m_rowImagePick->setVisible(isImageMode);
        if (m_rowContainer)    m_rowContainer->setVisible(isImageMode);
        if (m_rowShape)        m_rowShape->setVisible(isImageMode && cfg.containerEnabled);

        // Border sub-controls: only relevant when container is enabled.
        if (m_borderToggleLabel) m_borderToggleLabel->setVisible(isImageMode && cfg.containerEnabled);
        if (m_borderToggle)      m_borderToggle->setVisible(isImageMode && cfg.containerEnabled);

        m_generalColumn->updateLayout();
    }
}

void CustomSliderPopup::onTab(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    switchTab(tag);
}

void CustomSliderPopup::refreshPreview() {
    if (!m_previewNode) return;
    m_previewNode->removeAllChildren();

    auto& cfg = CustomSliderManager::get().config();
    if (!cfg.enabled) {
        auto* lbl = CCLabelBMFont::create("OFF", "bigFont.fnt");
        lbl->setScale(0.3f); lbl->setColor({150, 150, 150});
        m_previewNode->addChild(lbl); return;
    }

    switch (cfg.thumbMode) {
        case SliderThumbMode::Image:
        case SliderThumbMode::Gif: {
            if (!cfg.customImagePath.empty()) {
                CCNode* raw = nullptr;
                if (cfg.thumbMode == SliderThumbMode::Gif) {
                    raw = AnimatedGIFSprite::create(cfg.customImagePath);
                }
                if (!raw) {
                    auto* tex = CCTextureCache::sharedTextureCache()->addImage(cfg.customImagePath.c_str(), false);
                    if (tex) raw = CCSprite::createWithTexture(tex);
                }
                if (!raw) {
                    auto* lbl = CCLabelBMFont::create("?", "bigFont.fnt");
                    lbl->setScale(0.5f); lbl->setColor({150, 150, 150});
                    m_previewNode->addChild(lbl); break;
                }

                if (cfg.containerEnabled) {
                    // Build the same shape container the slider thumb will use
                    constexpr float kSize = 28.f;
                    std::string shape = cfg.containerShape.empty() ? "circle" : cfg.containerShape;

                    auto* stencil = createShapeStencil(shape, kSize);
                    if (!stencil) stencil = createShapeStencil("circle", kSize);

                    auto* clipper = CCClippingNode::create();
                    clipper->setStencil(stencil);
                    clipper->setAlphaThreshold(-1.0f);
                    clipper->setContentSize({kSize, kSize});
                    clipper->setAnchorPoint({0.5f, 0.5f});
                    clipper->ignoreAnchorPointForPosition(false);
                    clipper->setPosition({0, 0});

                    float iw = std::max(raw->getContentWidth(), 1.f);
                    float ih = std::max(raw->getContentHeight(), 1.f);
                    raw->setScale(std::max(kSize / iw, kSize / ih));
                    raw->setAnchorPoint({0.5f, 0.5f});
                    raw->ignoreAnchorPointForPosition(false);
                    raw->setPosition({kSize / 2.f, kSize / 2.f});
                    clipper->addChild(raw);
                    m_previewNode->addChild(clipper);

                    if (cfg.containerBorderEnabled) {
                        float thick = std::clamp(cfg.containerBorderThickness, 0.5f, 8.f);
                        if (auto* border = createShapeBorder(shape, kSize + thick * 2.f, thick, cfg.containerBorderColor, 255)) {
                            border->setAnchorPoint({0.5f, 0.5f});
                            border->setPosition({0, 0});
                            m_previewNode->addChild(border, 5);
                        }
                    }
                } else {
                    float mx = std::max(raw->getContentSize().width, raw->getContentSize().height);
                    if (mx > 0.f) raw->setScale(28.f / mx);
                    m_previewNode->addChild(raw);
                }
                return;
            }
            auto* lbl = CCLabelBMFont::create("?", "bigFont.fnt");
            lbl->setScale(0.5f); lbl->setColor({150, 150, 150});
            m_previewNode->addChild(lbl); break;
        }
        default: {
            auto* gm = GameManager::get(); if (!gm) return;
            int iconId = cfg.usePlayerIcon ? getPlayerIconId(cfg.iconType) : cfg.customIconId;
            auto* player = SimplePlayer::create(iconId);
            if (!player) return;
            player->updatePlayerFrame(iconId, toGDIconType(cfg.iconType));
            if (cfg.usePlayerColors) {
                player->setColor(gm->colorForIdx(gm->getPlayerColor()));
                player->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
                if (gm->getPlayerGlow()) player->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
                else player->disableGlowOutline();
            } else {
                player->setColor(cfg.color1); player->setSecondColor(cfg.color2);
                if (cfg.enableGlow) player->setGlowOutline(cfg.color2); else player->disableGlowOutline();
            }
            player->setScale(std::min(cfg.iconScale * 1.5f, 1.0f));
            m_previewNode->addChild(player); break;
        }
    }
}

int CustomSliderPopup::getPlayerIconId(SliderIconType type) {
    auto* gm = GameManager::get();
    switch (type) {
        case SliderIconType::Cube:   return gm->getPlayerFrame();
        case SliderIconType::Ship:   return gm->getPlayerShip();
        case SliderIconType::Ball:   return gm->getPlayerBall();
        case SliderIconType::Ufo:    return gm->getPlayerBird();
        case SliderIconType::Wave:   return gm->getPlayerDart();
        case SliderIconType::Robot:  return gm->getPlayerRobot();
        case SliderIconType::Spider: return gm->getPlayerSpider();
        case SliderIconType::Swing:  return gm->getPlayerSwing();
    }
    return gm->getPlayerFrame();
}

IconType CustomSliderPopup::toGDIconType(SliderIconType type) {
    switch (type) {
        case SliderIconType::Cube:   return IconType::Cube;
        case SliderIconType::Ship:   return IconType::Ship;
        case SliderIconType::Ball:   return IconType::Ball;
        case SliderIconType::Ufo:    return IconType::Ufo;
        case SliderIconType::Wave:   return IconType::Wave;
        case SliderIconType::Robot:  return IconType::Robot;
        case SliderIconType::Spider: return IconType::Spider;
        case SliderIconType::Swing:  return IconType::Swing;
    }
    return IconType::Cube;
}

// ── Callbacks ──

void CustomSliderPopup::reapplyAllSliders() {
    // Find all Sliders in the current scene and re-upgrade them
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto& mgr = CustomSliderManager::get();
    if (!mgr.config().enabled) return;

    // Recursive lambda to find all Slider nodes
    std::function<void(CCNode*)> findSliders = [&](CCNode* node) {
        if (!node) return;
        if (auto* slider = typeinfo_cast<Slider*>(node)) {
            if (auto* thumb = slider->getThumb()) {
                // Re-create the thumb images
                auto thumbSize = thumb->getContentSize();

                auto* normalBase = CCSprite::create();
                normalBase->setContentSize(thumbSize);
                auto* normalNode = CCSprite::create();
                normalNode->setScale(0.9f);
                normalBase->addChild(normalNode);
                normalNode->setPosition(thumbSize / 2.f);
                mgr.addIconToNode(normalNode, false);

                auto* selectedBase = CCSprite::create();
                selectedBase->setContentSize(thumbSize);
                auto* selectedNode = CCSprite::create();
                selectedNode->setScale(0.9f);
                selectedBase->addChild(selectedNode);
                selectedNode->setPosition(thumbSize / 2.f);
                mgr.addIconToNode(selectedNode, true);

                thumb->setNormalImage(normalBase);
                thumb->setSelectedImage(selectedBase);
            }
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                findSliders(child);
            }
        }
    };

    findSliders(scene);
}

void CustomSliderPopup::onToggleEnabled(CCObject* s) {
    CustomSliderManager::get().config().enabled = !static_cast<CCMenuItemToggler*>(s)->isToggled();
    refreshPreview();
    reapplyAllSliders();
}
void CustomSliderPopup::onModeLeft(CCObject*) {
    auto& cfg = CustomSliderManager::get().config();
    int v = (static_cast<int>(cfg.thumbMode) - 1 + kModeCount) % kModeCount;
    cfg.thumbMode = static_cast<SliderThumbMode>(v);
    if (m_modeLabel) m_modeLabel->setString(kModeNames[v]);
    switchTab(0); refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onModeRight(CCObject*) {
    auto& cfg = CustomSliderManager::get().config();
    int v = (static_cast<int>(cfg.thumbMode) + 1) % kModeCount;
    cfg.thumbMode = static_cast<SliderThumbMode>(v);
    if (m_modeLabel) m_modeLabel->setString(kModeNames[v]);
    switchTab(0); refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onIconTypeLeft(CCObject*) {
    auto& cfg = CustomSliderManager::get().config();
    int v = (static_cast<int>(cfg.iconType) - 1 + kIconTypeCount) % kIconTypeCount;
    cfg.iconType = static_cast<SliderIconType>(v);
    if (m_iconTypeLabel) m_iconTypeLabel->setString(kIconTypeNames[v]);
    refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onIconTypeRight(CCObject*) {
    auto& cfg = CustomSliderManager::get().config();
    int v = (static_cast<int>(cfg.iconType) + 1) % kIconTypeCount;
    cfg.iconType = static_cast<SliderIconType>(v);
    if (m_iconTypeLabel) m_iconTypeLabel->setString(kIconTypeNames[v]);
    refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onTogglePlayerIcon(CCObject* s) {
    CustomSliderManager::get().config().usePlayerIcon = !static_cast<CCMenuItemToggler*>(s)->isToggled();
    refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onTogglePlayerColors(CCObject* s) {
    CustomSliderManager::get().config().usePlayerColors = !static_cast<CCMenuItemToggler*>(s)->isToggled();
    refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onScaleChanged(CCObject* s) {
    float v = sliderDenorm(static_cast<SliderThumb*>(s)->getValue(), 0.10f, 1.0f);
    CustomSliderManager::get().config().iconScale = v;
    if (m_scaleLabel) m_scaleLabel->setString(fmt::format("Scale: {:.2f}", v).c_str());
    refreshPreview(); reapplyAllSliders();
}
void CustomSliderPopup::onPickImage(CCObject*) {
    WeakRef<CustomSliderPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto opt = std::move(result).unwrapOr(std::nullopt);
        if (!opt.has_value() || opt->empty()) return;
        auto path = *opt;
        auto& cfg = CustomSliderManager::get().config();
        auto dest = CustomSliderManager::get().imagesDir() / path.filename();
        std::error_code ec;
        std::filesystem::copy_file(path, dest, std::filesystem::copy_options::overwrite_existing, ec);
        cfg.customImagePath = ec ? geode::utils::string::pathToString(path) : geode::utils::string::pathToString(dest);
        std::string dp = geode::utils::string::pathToString(path.filename());
        if (dp.size() > 20) dp = dp.substr(0, 17) + "...";
        if (popup->m_imagePathLabel) popup->m_imagePathLabel->setString(dp.c_str());
        popup->refreshPreview();
        popup->reapplyAllSliders();
    });
}
void CustomSliderPopup::onToggleAnimate(CCObject* s) {
    CustomSliderManager::get().config().animateOnDrag = !static_cast<CCMenuItemToggler*>(s)->isToggled();
}
void CustomSliderPopup::onToggleContainer(CCObject* s) {
    auto& cfg = CustomSliderManager::get().config();
    cfg.containerEnabled = !static_cast<CCMenuItemToggler*>(s)->isToggled();
    // Re-apply visibility for the shape grid + border row
    switchTab(0);
    refreshPreview();
    reapplyAllSliders();
}
void CustomSliderPopup::onToggleBorder(CCObject* s) {
    CustomSliderManager::get().config().containerBorderEnabled = !static_cast<CCMenuItemToggler*>(s)->isToggled();
    refreshPreview();
    reapplyAllSliders();
}
void CustomSliderPopup::onShapeSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto shapes = getGeometricShapes();
    if (idx < 0 || idx >= static_cast<int>(shapes.size())) return;
    CustomSliderManager::get().config().containerShape = shapes[idx].first;
    rebuildShapeGrid();
    refreshPreview();
    reapplyAllSliders();
}
void CustomSliderPopup::rebuildShapeGrid() {
    if (!m_shapeGridMenu) return;
    m_shapeGridMenu->removeAllChildren();

    auto const& cfg = CustomSliderManager::get().config();
    auto shapes = getGeometricShapes();
    constexpr float kCell = 18.f;

    for (size_t i = 0; i < shapes.size(); i++) {
        auto const& shapeName = shapes[i].first;
        bool selected = (cfg.containerShape == shapeName);

        // Use PaimonDrawNode rounded rect instead of CCScale9Sprite
        cocos2d::ccColor3B bgCol = selected ? ccColor3B{60, 160, 60} : ccColor3B{40, 40, 40};
        GLubyte bgAlpha = selected ? 220 : 140;
        auto* cellBg = paimon::SpriteHelper::createColorPanel(kCell, kCell, bgCol, bgAlpha, 3.f);
        cellBg->setContentSize({kCell, kCell});

        if (auto* icon = createShapeStencil(shapeName, kCell - 6.f)) {
            icon->setAnchorPoint({0.5f, 0.5f});
            icon->setPosition({kCell * 0.5f, kCell * 0.5f});
            cellBg->addChild(icon);
        }

        auto* btn = CCMenuItemSpriteExtra::create(
            cellBg, this, menu_selector(CustomSliderPopup::onShapeSelect));
        btn->setTag(static_cast<int>(i));
        m_shapeGridMenu->addChild(btn);
    }
    m_shapeGridMenu->updateLayout();
}
void CustomSliderPopup::onAnimTypeLeft(CCObject*) {
    auto& cfg = CustomSliderManager::get().config();
    int v = (static_cast<int>(cfg.animType) - 1 + kAnimTypeCount) % kAnimTypeCount;
    cfg.animType = static_cast<SliderAnimType>(v);
    if (m_animTypeLabel) m_animTypeLabel->setString(kAnimTypeNames[v]);
}
void CustomSliderPopup::onAnimTypeRight(CCObject*) {
    auto& cfg = CustomSliderManager::get().config();
    int v = (static_cast<int>(cfg.animType) + 1) % kAnimTypeCount;
    cfg.animType = static_cast<SliderAnimType>(v);
    if (m_animTypeLabel) m_animTypeLabel->setString(kAnimTypeNames[v]);
}
void CustomSliderPopup::onAnimDurationChanged(CCObject* s) {
    float v = sliderDenorm(static_cast<SliderThumb*>(s)->getValue(), 0.05f, 0.5f);
    CustomSliderManager::get().config().animDuration = v;
    if (m_animDurationLabel) m_animDurationLabel->setString(fmt::format("Duration: {:.2f}s", v).c_str());
}
void CustomSliderPopup::onAnimBounceChanged(CCObject* s) {
    float v = sliderDenorm(static_cast<SliderThumb*>(s)->getValue(), 1.0f, 2.0f);
    CustomSliderManager::get().config().animBounceScale = v;
    if (m_animBounceLabel) m_animBounceLabel->setString(fmt::format("Bounce: {:.0f}%", (v - 1.f) * 100.f).c_str());
}
void CustomSliderPopup::onAnimRotateChanged(CCObject* s) {
    float v = sliderDenorm(static_cast<SliderThumb*>(s)->getValue(), 5.f, 45.f);
    CustomSliderManager::get().config().animRotateDeg = v;
    if (m_animRotateLabel) m_animRotateLabel->setString(fmt::format("Rotate: {:.0f} deg", v).c_str());
}
void CustomSliderPopup::onToggleOptions(CCObject* s) { CustomSliderManager::get().config().targets.optionsSliders = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }
void CustomSliderPopup::onToggleEditor(CCObject* s) { CustomSliderManager::get().config().targets.editorSliders = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }
void CustomSliderPopup::onToggleColors(CCObject* s) { CustomSliderManager::get().config().targets.colorSliders = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }
void CustomSliderPopup::onToggleGarage(CCObject* s) { CustomSliderManager::get().config().targets.garageSliders = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }
void CustomSliderPopup::onReset(CCObject*) {
    CustomSliderManager::get().resetToDefaults();
    this->onClose(nullptr);
    if (auto* p = CustomSliderPopup::create()) p->show();
}
