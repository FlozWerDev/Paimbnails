#include "MainMenuLayoutEditor.hpp"

#include "MainMenuLayoutPresetPopup.hpp"
#include "../services/MainMenuLayoutPresetManager.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/PopupManager.hpp>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    MainMenuLayoutEditor* s_active = nullptr;

    constexpr float kMinScale = 0.25f;
    constexpr float kMaxScale = 4.0f;
    constexpr float kMinHit = 26.f;
    constexpr float kOutlinePad = 6.f;
    constexpr float kGripSize = 18.f;
    // just above the drawn square (18/2 = 9) for touch; 22 swallowed the button corner.
    constexpr float kGripHit = 12.f;
    constexpr float kRotateGripHit = 14.f;
    constexpr float kRotateArm = 34.f;
    constexpr float kCanvasBottom = 100.f;
    constexpr float kTopBarHeight = 34.f;
    constexpr std::size_t kHistoryLimit = 50;
    constexpr float kTransitionDuration = 0.46f;
    constexpr float kCloseDuration = 0.32f;

    CCPoint worldPos(CCNode* node) {
        if (!node || !node->getParent()) return { 0.f, 0.f };
        return node->getParent()->convertToWorldSpace(node->getPosition());
    }

    CCRect nodeWorldRect(CCNode* node, float minHit) {
        if (!node || !node->getParent()) return { 0.f, 0.f, 0.f, 0.f };
        auto bb = node->boundingBox();
        auto* p = node->getParent();
        auto bl = p->convertToWorldSpace({ bb.getMinX(), bb.getMinY() });
        auto tr = p->convertToWorldSpace({ bb.getMaxX(), bb.getMaxY() });
        CCRect r(std::min(bl.x, tr.x), std::min(bl.y, tr.y), std::abs(tr.x - bl.x), std::abs(tr.y - bl.y));
        if (r.size.width < minHit) { r.origin.x = r.getMidX() - minHit / 2.f; r.size.width = minHit; }
        if (r.size.height < minHit) { r.origin.y = r.getMidY() - minHit / 2.f; r.size.height = minHit; }
        return r;
    }

    void strokeRect(CCDrawNode* node, CCRect r, ccColor4F color, float thickness) {
        CCPoint bl{ r.getMinX(), r.getMinY() }, br{ r.getMaxX(), r.getMinY() };
        CCPoint tr{ r.getMaxX(), r.getMaxY() }, tl{ r.getMinX(), r.getMaxY() };
        node->drawSegment(bl, br, thickness, color);
        node->drawSegment(br, tr, thickness, color);
        node->drawSegment(tr, tl, thickness, color);
        node->drawSegment(tl, bl, thickness, color);
    }
}

MainMenuLayoutEditor* MainMenuLayoutEditor::create(CCNode* root) {
    auto* ret = new MainMenuLayoutEditor();
    if (ret && ret->init(root)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

MainMenuLayoutEditor* MainMenuLayoutEditor::getActive() { return s_active; }
bool MainMenuLayoutEditor::isActive() { return s_active != nullptr; }

void MainMenuLayoutEditor::open(CCNode* root) {
    if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return;
    if (!root || s_active) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto* editor = MainMenuLayoutEditor::create(root);
    if (!editor) return;
    s_active = editor;
    scene->addChild(editor, INT_MAX - 10);
}

CCNode* MainMenuLayoutEditor::getTargetRoot() const { return m_root.lock().data(); }

MainMenuLayoutEditor::~MainMenuLayoutEditor() {
    for (auto& menu : m_disabledMenus) {
        if (menu && menu->getParent()) menu->setEnabled(true);
    }
    m_disabledMenus.clear();
    if (s_active == this) s_active = nullptr;
}

void MainMenuLayoutEditor::onExit() {
    if (!m_saved) {
        for (auto const& item : m_items) {
            auto it = m_initial.find(item.target.key);
            if (it != m_initial.end()) MainMenuLayoutManager::applyLayout(item.target, it->second);
        }
        if (auto* root = this->getTargetRoot()) {
            MainMenuLayoutManager::get().syncShapes(root, m_initialShapes);
        }
    } else {
        for (auto const& item : m_items) {
            if (m_transitions.count(item.target.key)) this->applyLive(item);
        }
    }
    m_transitions.clear();
    for (auto& menu : m_disabledMenus) {
        if (menu && menu->getParent()) menu->setEnabled(true);
    }
    m_disabledMenus.clear();
    if (s_active == this) s_active = nullptr;
    this->unscheduleUpdate();
    CCLayer::onExit();
}

bool MainMenuLayoutEditor::init(CCNode* root) {
    if (!CCLayer::init()) return false;
    m_root = root;

    auto winSize = CCDirector::get()->getWinSize();
    this->setContentSize(winSize);
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setKeypadEnabled(true);
#ifdef GEODE_IS_DESKTOP
    this->setKeyboardEnabled(true);
#endif
    this->scheduleUpdate();

    m_dark = CCLayerColor::create({ 0, 0, 0, 0 });
    m_dark->setContentSize(winSize);
    this->addChild(m_dark, -1);

    m_grid = CCDrawNode::create();
    m_grid->setVisible(false);
    this->addChild(m_grid, 1);

    m_highlights = CCDrawNode::create();
    this->addChild(m_highlights, 10);
    m_outline = CCDrawNode::create();
    this->addChild(m_outline, 20);
    m_grip = CCDrawNode::create();
    this->addChild(m_grip, 25);
    m_guideX = CCDrawNode::create();
    m_guideX->setVisible(false);
    this->addChild(m_guideX, 15);
    m_guideY = CCDrawNode::create();
    m_guideY->setVisible(false);
    this->addChild(m_guideY, 15);

    m_gridOn = Mod::get()->getSavedValue<bool>("main-menu-layout-grid", false);
    m_snapGrid = Mod::get()->getSavedValue<bool>("main-menu-layout-snap-grid", false);

    this->buildUI();
    this->collectItems();
    this->disableTargetMenus();
    this->captureInterfaceNodes(m_barContainer);
    this->captureInterfaceNodes(m_topBar);
    this->captureInterfaceNodes(m_inspector);
    this->captureInterfaceNodes(m_status);
    if (m_collapseBtn) this->captureInterfaceNodes(m_collapseBtn->getParent());
    this->applyInterfaceOpacity();
    this->rebuildGrid();
    this->pushHistory();
    this->redraw();
    return true;
}

void MainMenuLayoutEditor::buildUI() {
    this->buildTopBar();
    this->buildDock();
    this->buildInspector();
}

void MainMenuLayoutEditor::buildTopBar() {
    auto winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    m_topBar = CCNode::create();
    m_topBar->setPosition({ 0.f, 0.f });
    this->addChild(m_topBar, 30);

    if (auto* bg = paimon::SpriteHelper::createColorPanel(winSize.width + 20.f, kTopBarHeight, { 12, 16, 26 }, 215, 6.f)) {
        bg->setAnchorPoint({ 0.f, 0.f });
        bg->setPosition({ -10.f, winSize.height - kTopBarHeight });
        m_topBar->addChild(bg, 0);
    }
    if (auto* line = paimon::ui::makeDivider(winSize.width, paimon::ui::palette::gold, 120)) {
        line->setPosition({ winSize.width / 2.f, winSize.height - kTopBarHeight });
        m_topBar->addChild(line, 1);
    }

    m_title = CCLabelBMFont::create(loc.getString("menu_layout.title").c_str(), "goldFont.fnt");
    m_title->setScale(0.55f);
    m_title->setAnchorPoint({ 0.f, 0.5f });
    m_title->setPosition({ 14.f, winSize.height - kTopBarHeight / 2.f });
    m_topBar->addChild(m_title, 2);

    m_status = CCLabelBMFont::create(loc.getString("menu_layout.none_selected").c_str(), "chatFont.fnt");
    m_status->setScale(0.52f);
    m_status->setColor({ 190, 210, 240 });
    m_status->setAnchorPoint({ 1.f, 0.5f });
    m_status->setPosition({ winSize.width - 14.f, winSize.height - kTopBarHeight / 2.f });
    m_topBar->addChild(m_status, 2);
}

void MainMenuLayoutEditor::buildDock() {
    auto winSize = CCDirector::get()->getWinSize();

    // collapsible so buttons underneath stay movable.
    m_barContainer = CCNode::create();
    m_barContainer->setPosition({ 0.f, 0.f });
    this->addChild(m_barContainer, 30);

    if (auto* barBg = paimon::SpriteHelper::createColorPanel(winSize.width + 20.f, kCanvasBottom, { 10, 13, 22 }, 225, 8.f)) {
        barBg->setAnchorPoint({ 0.f, 0.f });
        barBg->setPosition({ -10.f, 0.f });
        m_barContainer->addChild(barBg, 0);
    }
    if (auto* topLine = paimon::ui::makeDivider(winSize.width, paimon::ui::palette::gold, 110)) {
        topLine->setPosition({ winSize.width / 2.f, kCanvasBottom - 1.f });
        m_barContainer->addChild(topLine, 1);
    }
    paimon::ui::addCorners(m_barContainer, { winSize.width, kCanvasBottom }, geode::SideArtStyle::PopupGold, 0.4f, true);

    // shown only with a selection.
    m_opacitySlider = Slider::create(this, menu_selector(MainMenuLayoutEditor::onOpacityChanged));
    m_opacitySlider->setScale(0.6f);
    m_opacitySlider->setPosition({ winSize.width / 2.f, kCanvasBottom - 14.f });
    m_opacitySlider->setValue(1.f);
    m_opacitySlider->setVisible(false);
    m_barContainer->addChild(m_opacitySlider, 2);

    m_bar = CCMenu::create();
    m_bar->setContentSize({ winSize.width - 24.f, 70.f });
    m_bar->setPosition({ winSize.width / 2.f, 36.f });
    m_bar->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center)->setGrowCrossAxis(false)->setAutoScale(false));
    m_barContainer->addChild(m_bar, 1);

    auto& loc = Localization::get();

    auto groupLabel = [&](char const* text) {
        auto* lbl = CCLabelBMFont::create(text, "goldFont.fnt");
        lbl->setScale(0.3f);
        lbl->setOpacity(170);
        return lbl;
    };

    // a vertical group: tiny gold header over a row of icon buttons.
    auto makeGroup = [&](char const* header, std::vector<CCMenuItemSpriteExtra*> const& btns) {
        auto* group = CCNode::create();
        float w = 0.f;
        for (auto* b : btns) w += b->getContentSize().width + 4.f;
        w = std::max(w, 40.f);
        group->setContentSize({ w, 62.f });

        auto* row = CCMenu::create();
        row->setContentSize({ w, 42.f });
        row->setPosition({ w / 2.f, 24.f });
        row->setLayout(RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Center)->setAutoScale(false));
        for (auto* b : btns) row->addChild(b);
        row->updateLayout();
        group->addChild(row);

        auto* hdr = groupLabel(header);
        hdr->setPosition({ w / 2.f, 54.f });
        group->addChild(hdr);
        return group;
    };

    auto iconBtn = [&](char const* frame, SEL_MenuHandler cb, float scale) -> CCMenuItemSpriteExtra* {
        auto* spr = CCSprite::createWithSpriteFrameName(frame);
        if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        spr->setScale(scale);
        return CCMenuItemSpriteExtra::create(spr, this, cb);
    };

    auto* fileGroup = makeGroup(loc.getString("menu_layout.title").c_str(), {
        iconBtn("GJ_updateBtn_001.png", menu_selector(MainMenuLayoutEditor::onSave), 0.72f),
        iconBtn("folderIcon_001.png", menu_selector(MainMenuLayoutEditor::onLoadPreset), 0.78f),
        iconBtn("GJ_duplicateBtn_001.png", menu_selector(MainMenuLayoutEditor::onSavePreset), 0.72f),
    });

    m_lockIcon = CCSprite::createWithSpriteFrameName("GJ_lock_open_001.png");
    auto* lockBtn = m_lockIcon ? CCMenuItemSpriteExtra::create(m_lockIcon, this, menu_selector(MainMenuLayoutEditor::onToggleLock)) : nullptr;
    if (m_lockIcon) m_lockIcon->setScale(0.72f);

    std::vector<CCMenuItemSpriteExtra*> editBtns = {
        iconBtn("GJ_undoBtn_001.png", menu_selector(MainMenuLayoutEditor::onUndo), 0.72f),
        iconBtn("GJ_redoBtn_001.png", menu_selector(MainMenuLayoutEditor::onRedo), 0.72f),
        iconBtn("GJ_resetBtn_001.png", menu_selector(MainMenuLayoutEditor::onResetSelected), 0.68f),
        iconBtn("edit_delBtn_001.png", menu_selector(MainMenuLayoutEditor::onResetAll), 0.72f),
        iconBtn("GJ_closeBtn_001.png", menu_selector(MainMenuLayoutEditor::onToggleHidden), 0.62f),
    };
    if (lockBtn) editBtns.push_back(lockBtn);
    auto* editGroup = makeGroup("EDIT", editBtns);

    m_gridIcon = CCSprite::createWithSpriteFrameName("square_01_001.png");
    auto* gridBtn = m_gridIcon ? CCMenuItemSpriteExtra::create(m_gridIcon, this, menu_selector(MainMenuLayoutEditor::onToggleGrid)) : nullptr;
    if (m_gridIcon) { m_gridIcon->setScale(0.5f); m_gridIcon->setOpacity(m_gridOn ? 255 : 120); }
    m_snapIcon = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
    auto* snapBtn = m_snapIcon ? CCMenuItemSpriteExtra::create(m_snapIcon, this, menu_selector(MainMenuLayoutEditor::onToggleSnap)) : nullptr;
    if (m_snapIcon) { m_snapIcon->setScale(0.72f); m_snapIcon->setOpacity(m_snapGrid ? 255 : 120); }

    std::vector<CCMenuItemSpriteExtra*> viewBtns;
    if (gridBtn) viewBtns.push_back(gridBtn);
    if (snapBtn) viewBtns.push_back(snapBtn);
    auto* viewGroup = makeGroup("VIEW", viewBtns);

    auto* doneGroup = CCNode::create();
    doneGroup->setContentSize({ 70.f, 62.f });
    auto* doneMenu = CCMenu::create();
    doneMenu->setContentSize({ 70.f, 56.f });
    doneMenu->setPosition({ 35.f, 24.f });
    doneMenu->setLayout(ColumnLayout::create()->setGap(5.f)->setAxisReverse(true)->setAutoScale(false));
    auto* doneBtn = paimon::ui::makeButton(loc.getString("menu_layout.save").c_str(),
        [this] { this->onSave(nullptr); }, paimon::ui::Btn::Green, 64.f, 0.56f);
    auto* cancelBtn = paimon::ui::makeButton(loc.getString("menu_layout.cancel").c_str(),
        [this] { this->onCancel(nullptr); }, paimon::ui::Btn::Red, 64.f, 0.56f);
    doneMenu->addChild(doneBtn);
    doneMenu->addChild(cancelBtn);
    doneMenu->updateLayout();
    doneGroup->addChild(doneMenu);

    auto addDivider = [&] {
        auto* holder = CCNode::create();
        holder->setContentSize({ 2.f, 50.f });
        if (auto* d = paimon::SpriteHelper::createColorPanel(2.f, 46.f, { 90, 90, 110 }, 120, 1.f)) {
            d->setAnchorPoint({ 0.5f, 0.5f });
            d->setPosition({ 1.f, 25.f });
            holder->addChild(d);
        }
        m_bar->addChild(holder);
    };

    m_bar->addChild(fileGroup);
    addDivider();
    m_bar->addChild(editGroup);
    addDivider();
    m_bar->addChild(viewGroup);
    addDivider();
    m_bar->addChild(doneGroup);
    m_bar->updateLayout();

    // single toggle arrow, always visible outside the lowered container.
    auto* toggleMenu = CCMenu::create();
    toggleMenu->setPosition({ 0.f, 0.f });
    this->addChild(toggleMenu, 33);
    m_collapseArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (m_collapseArrow) {
        m_collapseArrow->setScale(0.7f);
        m_collapseArrow->setRotation(-90.f);
        m_collapseBtn = CCMenuItemSpriteExtra::create(m_collapseArrow, this, menu_selector(MainMenuLayoutEditor::onToggleBar));
        m_collapseBtn->setPosition({ winSize.width - 24.f, kCanvasBottom + 14.f });
        toggleMenu->addChild(m_collapseBtn);
    }
}

void MainMenuLayoutEditor::buildInspector() {
    auto winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    constexpr float kW = 150.f;
    constexpr float kH = 196.f;
    m_inspector = CCNode::create();
    m_inspector->setContentSize({ kW, kH });
    m_inspector->setAnchorPoint({ 1.f, 0.5f });
    m_inspector->setPosition({ winSize.width - 8.f, winSize.height / 2.f });
    m_inspector->setVisible(false);
    this->addChild(m_inspector, 32);

    if (auto* bg = paimon::SpriteHelper::createColorPanel(kW, kH, { 10, 13, 22 }, 230, 8.f)) {
        bg->setAnchorPoint({ 0.f, 0.f });
        m_inspector->addChild(bg, 0);
    }
    paimon::ui::addCorners(m_inspector, { kW, kH }, geode::SideArtStyle::PopupGold, 0.3f, false);

    auto* header = CCLabelBMFont::create(loc.getString("menu_layout.edit_selected").c_str(), "goldFont.fnt");
    header->setScale(0.5f);
    header->setPosition({ kW / 2.f, kH - 14.f });
    m_inspector->addChild(header, 2);

    auto* menu = CCMenu::create();
    menu->setPosition({ 0.f, 0.f });
    m_inspector->addChild(menu, 3);

    // one steppable row: label, [-], value, [+]. tag encodes field*10 + sign.
    auto addRow = [&](char const* name, float y, int field, CCLabelBMFont*& out, float bigStep) {
        auto* caption = CCLabelBMFont::create(name, "chatFont.fnt");
        caption->setScale(0.42f);
        caption->setAnchorPoint({ 0.f, 0.5f });
        caption->setPosition({ 12.f, y + 11.f });
        caption->setColor({ 190, 200, 220 });
        m_inspector->addChild(caption, 2);

        auto stepButton = [&](char const* frame, int tag, float x) {
            auto* spr = CCSprite::createWithSpriteFrameName(frame);
            spr->setScale(0.5f);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MainMenuLayoutEditor::onInspectorStep));
            btn->setTag(tag);
            btn->setPosition({ x, y });
            menu->addChild(btn);
        };
        (void)bigStep;
        stepButton("edit_leftBtn_001.png", field * 10 + 0, 20.f);
        stepButton("edit_rightBtn_001.png", field * 10 + 1, kW - 20.f);

        out = CCLabelBMFont::create("0", "bigFont.fnt");
        out->setScale(0.4f);
        out->setPosition({ kW / 2.f, y });
        m_inspector->addChild(out, 2);
    };

    float y = kH - 40.f;
    constexpr float kStep = 28.f;
    addRow("X", y, 0, m_inspX, 10.f);           y -= kStep;
    addRow("Y", y, 1, m_inspY, 10.f);           y -= kStep;
    addRow("Escala", y, 2, m_inspScale, 0.1f);  y -= kStep;
    addRow("Rotacion", y, 3, m_inspRot, 15.f);  y -= kStep;
    addRow("Opacidad", y, 4, m_inspOpacity, 10.f); y -= kStep;

    auto* zCaption = CCLabelBMFont::create("Capa", "chatFont.fnt");
    zCaption->setScale(0.42f);
    zCaption->setAnchorPoint({ 0.f, 0.5f });
    zCaption->setPosition({ 12.f, y + 11.f });
    zCaption->setColor({ 190, 200, 220 });
    m_inspector->addChild(zCaption, 2);
    auto backSpr = CCSprite::createWithSpriteFrameName("edit_downBtn_001.png");
    backSpr->setScale(0.5f);
    auto* backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(MainMenuLayoutEditor::onBringBack));
    backBtn->setPosition({ 20.f, y });
    menu->addChild(backBtn);
    auto frontSpr = CCSprite::createWithSpriteFrameName("edit_upBtn_001.png");
    frontSpr->setScale(0.5f);
    auto* frontBtn = CCMenuItemSpriteExtra::create(frontSpr, this, menu_selector(MainMenuLayoutEditor::onBringFront));
    frontBtn->setPosition({ kW - 20.f, y });
    menu->addChild(frontBtn);
    m_inspZ = CCLabelBMFont::create("0", "bigFont.fnt");
    m_inspZ->setScale(0.4f);
    m_inspZ->setPosition({ kW / 2.f, y });
    m_inspector->addChild(m_inspZ, 2);
}

void MainMenuLayoutEditor::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -INT_MAX + 200, true);
}

void MainMenuLayoutEditor::collectItems() {
    m_items.clear();
    m_live.clear();
    m_initial.clear();
    m_initialShapes.clear();
    m_selected = -1;

    auto* root = this->getTargetRoot();
    if (!root) return;

    auto& mgr = MainMenuLayoutManager::get();
    mgr.captureDefaultsAndApply(root);
    m_initialShapes = MainMenuLayoutManager::captureShapes(root);

    for (auto const& button : mgr.collectButtons(root)) {
        if (!button.node || !button.node->getParent()) continue;
        auto layout = MainMenuLayoutManager::readLayout(button.node);
        if (auto custom = mgr.getCustomLayout(button.key)) {
            layout.linkGroup = custom->linkGroup;
        }
        m_live[button.key] = layout;
        m_initial[button.key] = layout;
        m_items.push_back({ button });
    }
}

void MainMenuLayoutEditor::disableTargetMenus() {
    m_disabledMenus.clear();
    std::vector<CCMenu*> seen;
    for (auto const& item : m_items) {
        auto* menu = item.target.menu;
        if (!menu || !menu->isEnabled()) continue;
        if (std::find(seen.begin(), seen.end(), menu) != seen.end()) continue;
        seen.push_back(menu);
        menu->setEnabled(false);
        m_disabledMenus.emplace_back(menu);
    }
}

MainMenuLayoutEditor::Item* MainMenuLayoutEditor::selectedItem() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size())) return nullptr;
    return &m_items[m_selected];
}

void MainMenuLayoutEditor::selectIndex(int index) {
    m_selected = (index >= 0 && index < static_cast<int>(m_items.size())) ? index : -1;
    if (auto* item = this->selectedItem()) {
        if (auto* layout = this->liveLayout(*item)) {
            m_opacitySlider->setValue(std::clamp(layout->opacity, 0.f, 1.f));
        }
    }
    this->refreshInspector();
}

MenuButtonLayout* MainMenuLayoutEditor::liveLayout(Item const& item) {
    auto it = m_live.find(item.target.key);
    return it == m_live.end() ? nullptr : &it->second;
}

void MainMenuLayoutEditor::applyLive(Item const& item) {
    m_transitions.erase(item.target.key);
    auto it = m_live.find(item.target.key);
    if (it == m_live.end()) return;
    MainMenuLayoutManager::applyLayout(item.target, it->second);
}

void MainMenuLayoutEditor::animateLive(Item const& item) {
    if (!item.target.node || !item.target.node->getParent()) return;
    m_drag = DragMode::None;
    m_dragChanged = false;
    m_guideX->setVisible(false);
    m_guideY->setVisible(false);
    m_transitions[item.target.key] = { MainMenuLayoutManager::readLayout(item.target.node), 0.f, kTransitionDuration };
}

void MainMenuLayoutEditor::captureInterfaceNodes(CCNode* node) {
    if (!node) return;
    if (auto* rgba = geode::cast::typeinfo_cast<CCNodeRGBA*>(node)) {
        m_interfaceNodes.emplace_back(rgba, rgba->getOpacity());
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : geode::cocos::CCArrayExt<CCNode*>(children)) {
            this->captureInterfaceNodes(child);
        }
    }
}

void MainMenuLayoutEditor::applyInterfaceOpacity() {
    m_dark->setOpacity(static_cast<GLubyte>(110.f * m_interfaceOpacity));
    for (auto const& [node, alpha] : m_interfaceNodes) {
        if (auto* rgba = node.lock().data()) {
            rgba->setOpacity(static_cast<GLubyte>(alpha * m_interfaceOpacity));
        }
    }
}

void MainMenuLayoutEditor::beginClose(bool saved) {
    m_closing = true;
    m_saved = saved;
    m_interfaceElapsed = 0.f;
    m_closeOpacity = m_interfaceOpacity;
    m_drag = DragMode::None;
    m_dragChanged = false;
    m_guideX->setVisible(false);
    m_guideY->setVisible(false);
    m_bar->setEnabled(false);
    if (m_collapseBtn) m_collapseBtn->setEnabled(false);
    m_opacitySlider->setVisible(false);
    if (m_inspector) m_inspector->setVisible(false);
}

void MainMenuLayoutEditor::updateAnimations(float dt) {
    m_interfaceElapsed += dt;
    float progress = std::clamp(m_interfaceElapsed / (m_closing ? kCloseDuration : kTransitionDuration), 0.f, 1.f);
    float eased = progress * progress * (3.f - 2.f * progress);
    m_interfaceOpacity = m_closing ? m_closeOpacity * (1.f - eased) : eased;
    this->applyInterfaceOpacity();

    for (auto const& item : m_items) {
        auto it = m_transitions.find(item.target.key);
        if (it == m_transitions.end()) continue;
        auto target = m_live.find(item.target.key);
        if (target == m_live.end() || !item.target.node || !item.target.node->getParent()) {
            m_transitions.erase(it);
            continue;
        }
        auto& transition = it->second;
        transition.elapsed += dt;
        float phase = std::clamp(transition.elapsed / transition.duration, 0.f, 1.f);
        if (phase >= 1.f) {
            MainMenuLayoutManager::applyLayout(item.target, target->second);
            m_transitions.erase(it);
            continue;
        }
        float t = phase * phase * (3.f - 2.f * phase);
        auto const& from = transition.from;
        auto const& to = target->second;
        auto frame = to;
        auto mix = [t](float a, float b) { return a + (b - a) * t; };
        frame.position = from.position + (to.position - from.position) * t;
        frame.scale = mix(from.scale, to.scale);
        frame.scaleX = mix(from.scaleX, to.scaleX);
        frame.scaleY = mix(from.scaleY, to.scaleY);
        float startOpacity = from.hidden ? 0.f : from.opacity;
        float endOpacity = to.hidden ? 0.f : to.opacity;
        frame.opacity = startOpacity + (endOpacity - startOpacity) * t;
        frame.hidden = from.hidden && to.hidden;
        frame.fontFile.clear();
        MainMenuLayoutManager::applyLayout(item.target, frame);
    }
}

CCRect MainMenuLayoutEditor::itemRect(Item const& item) const {
    if (!item.target.node || !item.target.node->getParent()) return { 0.f, 0.f, 0.f, 0.f };
    auto rect = nodeWorldRect(item.target.node, kMinHit);
    for (auto const& fol : item.target.labelGroupFollowers) {
        if (!fol || !fol->getParent()) continue;
        auto fr = nodeWorldRect(fol, kMinHit);
        auto minX = std::min(rect.getMinX(), fr.getMinX());
        auto minY = std::min(rect.getMinY(), fr.getMinY());
        auto maxX = std::max(rect.getMaxX(), fr.getMaxX());
        auto maxY = std::max(rect.getMaxY(), fr.getMaxY());
        rect = { minX, minY, maxX - minX, maxY - minY };
    }
    return rect;
}

CCRect MainMenuLayoutEditor::outlineRect(Item const& item) const {
    auto r = this->itemRect(item);
    r.origin.x -= kOutlinePad;
    r.origin.y -= kOutlinePad;
    r.size.width += kOutlinePad * 2.f;
    r.size.height += kOutlinePad * 2.f;
    return r;
}

CCPoint MainMenuLayoutEditor::gripPos(Item const& item) const {
    auto r = this->outlineRect(item);
    return { r.getMaxX(), r.getMinY() };
}

CCPoint MainMenuLayoutEditor::rotateGripPos(Item const& item) const {
    auto r = this->outlineRect(item);
    return { r.getMidX(), r.getMaxY() + kRotateArm };
}

bool MainMenuLayoutEditor::isBackgroundItem(Item const& item) const {
    if (!item.target.node || !item.target.node->getParent()) return false;
    auto win = CCDirector::get()->getWinSize();
    auto r = this->itemRect(item);
    return r.size.width >= win.width * 0.8f && r.size.height >= win.height * 0.8f;
}

MainMenuLayoutEditor::Item* MainMenuLayoutEditor::findItemAt(CCPoint worldPos) {
    Item* best = nullptr;
    float bestArea = FLT_MAX;
    for (auto& item : m_items) {
        if (!item.target.node || !item.target.node->getParent()) continue;
        if (this->isBackgroundItem(item)) continue;
        auto rect = this->itemRect(item);
        if (!rect.containsPoint(worldPos)) continue;
        float area = rect.size.width * rect.size.height;
        if (area < bestArea) {
            best = &item;
            bestArea = area;
        }
    }
    return best;
}

CCPoint MainMenuLayoutEditor::snapWorld(Item const& item, CCPoint proposed) {
    if (m_snapGrid) {
        proposed.x = this->snapToGrid(proposed.x);
        proposed.y = this->snapToGrid(proposed.y);
    }
    auto snapDist = std::clamp(static_cast<float>(Mod::get()->getSavedValue<int64_t>("main-menu-layout-snap-distance", 10)), 1.f, 64.f);
    auto winSize = CCDirector::get()->getWinSize();
    auto rect = this->itemRect(item);
    float halfW = rect.size.width / 2.f, halfH = rect.size.height / 2.f;

    float bestX = proposed.x, bestY = proposed.y;
    float bdX = snapDist + 1.f, bdY = snapDist + 1.f;
    bool sX = false, sY = false;
    auto considerX = [&](float c) { float d = std::abs(proposed.x - c); if (d <= snapDist && d < bdX) { bdX = d; bestX = c; sX = true; } };
    auto considerY = [&](float c) { float d = std::abs(proposed.y - c); if (d <= snapDist && d < bdY) { bdY = d; bestY = c; sY = true; } };

    considerX(winSize.width / 2.f);
    considerY(winSize.height / 2.f);
    considerX(halfW);
    considerX(winSize.width - halfW);
    considerY(halfH);
    considerY(winSize.height - halfH);

    for (auto const& other : m_items) {
        if (&other == &item || !other.target.node || !other.target.node->getParent()) continue;
        auto ow = worldPos(other.target.node);
        considerX(ow.x);
        considerY(ow.y);
    }

    ccColor4F guideColor = { 0.2f, 1.f, 0.55f, 0.85f };
    if (m_guideX) {
        m_guideX->clear();
        m_guideX->setVisible(sX);
        if (sX) m_guideX->drawSegment({ bestX, 0.f }, { bestX, winSize.height }, 1.f, guideColor);
    }
    if (m_guideY) {
        m_guideY->clear();
        m_guideY->setVisible(sY);
        if (sY) m_guideY->drawSegment({ 0.f, bestY }, { winSize.width, bestY }, 1.f, guideColor);
    }
    return { sX ? bestX : proposed.x, sY ? bestY : proposed.y };
}

void MainMenuLayoutEditor::nudgeSelection(CCPoint deltaWorld) {
    auto* item = this->selectedItem();
    if (!item) return;
    auto* node = item->target.node.data();
    if (!node || !node->getParent()) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    auto w = worldPos(node) + deltaWorld;
    layout->position = node->getParent()->convertToNodeSpace(w);
    this->applyLive(*item);
}

void MainMenuLayoutEditor::scaleSelection(float factor) {
    auto* item = this->selectedItem();
    if (!item) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    float s = std::clamp(layout->scale * factor, kMinScale, kMaxScale);
    layout->scale = s;
    layout->scaleX = s;
    layout->scaleY = s;
    this->applyLive(*item);
}

void MainMenuLayoutEditor::rotateSelection(float deltaDegrees) {
    auto* item = this->selectedItem();
    if (!item) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    layout->rotation = std::fmod(layout->rotation + deltaDegrees, 360.f);
    this->applyLive(*item);
}

void MainMenuLayoutEditor::setSelectionOpacity(float opacity) {
    auto* item = this->selectedItem();
    if (!item) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    layout->opacity = std::clamp(opacity, 0.f, 1.f);
    layout->hidden = false;
    this->applyLive(*item);
    if (m_opacitySlider) m_opacitySlider->setValue(layout->opacity);
}

void MainMenuLayoutEditor::bringSelection(int direction) {
    auto* item = this->selectedItem();
    if (!item) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    layout->layer = std::clamp(layout->layer + (direction >= 0 ? 1 : -1), -100, 100);
    this->applyLive(*item);
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

bool MainMenuLayoutEditor::isSelectionLocked() {
    auto* item = this->selectedItem();
    if (!item) return false;
    auto* layout = this->liveLayout(*item);
    return layout && layout->locked;
}

void MainMenuLayoutEditor::toggleSelectionLock() {
    auto* item = this->selectedItem();
    if (!item) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    layout->locked = !layout->locked;
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::centerSelection(bool horizontal, bool vertical) {
    auto* item = this->selectedItem();
    if (!item || !item->target.node || !item->target.node->getParent()) return;
    auto* layout = this->liveLayout(*item);
    if (!layout) return;
    auto winSize = CCDirector::get()->getWinSize();
    auto w = worldPos(item->target.node);
    if (horizontal) w.x = winSize.width / 2.f;
    if (vertical) w.y = winSize.height / 2.f;
    layout->position = item->target.node->getParent()->convertToNodeSpace(w);
    this->applyLive(*item);
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

float MainMenuLayoutEditor::snapToGrid(float value) const {
    if (!m_snapGrid || m_gridStep <= 1.f) return value;
    return std::round(value / m_gridStep) * m_gridStep;
}

void MainMenuLayoutEditor::rebuildGrid() {
    if (!m_grid) return;
    m_grid->clear();
    m_grid->setVisible(m_gridOn);
    if (!m_gridOn) return;
    auto winSize = CCDirector::get()->getWinSize();
    ccColor4F line = { 1.f, 1.f, 1.f, 0.08f };
    ccColor4F axis = { 0.3f, 1.f, 0.6f, 0.25f };
    for (float x = m_gridStep; x < winSize.width; x += m_gridStep) {
        m_grid->drawSegment({ x, kCanvasBottom }, { x, winSize.height - kTopBarHeight }, 0.5f, line);
    }
    for (float y = kCanvasBottom + m_gridStep; y < winSize.height - kTopBarHeight; y += m_gridStep) {
        m_grid->drawSegment({ 0.f, y }, { winSize.width, y }, 0.5f, line);
    }
    m_grid->drawSegment({ winSize.width / 2.f, kCanvasBottom }, { winSize.width / 2.f, winSize.height - kTopBarHeight }, 1.f, axis);
    m_grid->drawSegment({ 0.f, winSize.height / 2.f }, { winSize.width, winSize.height / 2.f }, 1.f, axis);
}

void MainMenuLayoutEditor::setGridEnabled(bool on) {
    m_gridOn = on;
    Mod::get()->setSavedValue<bool>("main-menu-layout-grid", on);
    if (m_gridIcon) m_gridIcon->setOpacity(on ? 255 : 120);
    this->rebuildGrid();
}

void MainMenuLayoutEditor::setSnapEnabled(bool on) {
    m_snapGrid = on;
    Mod::get()->setSavedValue<bool>("main-menu-layout-snap-grid", on);
    if (m_snapIcon) m_snapIcon->setOpacity(on ? 255 : 120);
}

void MainMenuLayoutEditor::refreshInspector() {
    if (!m_inspector) return;
    auto* sel = this->selectedItem();
    bool show = sel != nullptr && !m_closing;
    m_inspector->setVisible(show);
    if (m_lockIcon) {
        bool locked = this->isSelectionLocked();
        m_lockIcon->setDisplayFrame(CCSpriteFrameCache::get()->spriteFrameByName(
            locked ? "GJ_lock_001.png" : "GJ_lock_open_001.png"));
    }
    if (!show || !sel->target.node || !sel->target.node->getParent()) return;
    auto* layout = this->liveLayout(*sel);
    if (!layout) return;
    auto w = worldPos(sel->target.node);
    if (m_inspX) m_inspX->setString(fmt::format("{:.0f}", w.x).c_str());
    if (m_inspY) m_inspY->setString(fmt::format("{:.0f}", w.y).c_str());
    if (m_inspScale) m_inspScale->setString(fmt::format("{:.2f}", layout->scale).c_str());
    if (m_inspRot) m_inspRot->setString(fmt::format("{:.0f}", layout->rotation).c_str());
    if (m_inspOpacity) m_inspOpacity->setString(fmt::format("{:.0f}%", std::clamp(layout->opacity, 0.f, 1.f) * 100.f).c_str());
    if (m_inspZ) m_inspZ->setString(fmt::format("{}", layout->layer).c_str());
}

void MainMenuLayoutEditor::resetItemToDefault(Item const& item) {
    auto& mgr = MainMenuLayoutManager::get();
    auto def = mgr.getSessionDefaultLayout(item.target.key);
    if (!def) def = mgr.getDefaultLayout(item.target.key);
    if (!def) {
        auto it = m_initial.find(item.target.key);
        if (it == m_initial.end()) return;
        def = it->second;
    }
    m_live[item.target.key] = *def;
    this->animateLive(item);
}

LayoutSnapshot MainMenuLayoutEditor::buildSnapshot() const {
    LayoutSnapshot snapshot;
    snapshot.buttons = m_live;
    snapshot.shapes = MainMenuLayoutManager::captureShapes(this->getTargetRoot());
    return snapshot;
}

void MainMenuLayoutEditor::pushHistory() {
    if (m_applyingHistory) return;
    auto snapshot = this->buildSnapshot();
    if (m_historyCursor + 1 < m_history.size()) {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCursor + 1), m_history.end());
    }
    m_history.push_back(std::move(snapshot));
    if (m_history.size() > kHistoryLimit) m_history.erase(m_history.begin());
    m_historyCursor = m_history.empty() ? 0 : m_history.size() - 1;
}

void MainMenuLayoutEditor::applyHistory(LayoutSnapshot const& snapshot) {
    auto* root = this->getTargetRoot();
    if (!root) return;
    m_applyingHistory = true;
    auto& mgr = MainMenuLayoutManager::get();
    for (auto const& item : m_items) {
        auto it = snapshot.buttons.find(item.target.key);
        if (it != snapshot.buttons.end()) m_live[item.target.key] = it->second;
        else if (auto def = mgr.getDefaultLayout(item.target.key)) m_live[item.target.key] = *def;
        this->animateLive(item);
    }
    mgr.syncShapes(root, snapshot.shapes);
    m_applyingHistory = false;
}

void MainMenuLayoutEditor::undo() {
    if (m_history.empty() || m_historyCursor == 0) return;
    --m_historyCursor;
    this->applyHistory(m_history[m_historyCursor]);
    this->redraw();
}

void MainMenuLayoutEditor::redo() {
    if (m_history.empty() || m_historyCursor + 1 >= m_history.size()) return;
    ++m_historyCursor;
    this->applyHistory(m_history[m_historyCursor]);
    this->redraw();
}

void MainMenuLayoutEditor::redraw() {
    if (!m_highlights || !m_outline || !m_grip) return;
    m_highlights->clear();
    m_outline->clear();
    m_grip->clear();

    auto* sel = this->selectedItem();
    for (auto const& item : m_items) {
        if (!item.target.node || !item.target.node->getParent()) continue;
        bool isSel = (&item == sel);
        if (isSel) continue;
        if (this->isBackgroundItem(item)) continue;
        auto rect = this->itemRect(item);
        strokeRect(m_highlights, rect,
            { 0.35f, 0.65f, 1.f, 0.4f * m_interfaceOpacity }, 1.f);
    }

    if (sel && sel->target.node && sel->target.node->getParent()) {
        bool locked = this->isSelectionLocked();
        ccColor4F outlineColor = locked
            ? ccColor4F{ 1.f, 0.6f, 0.25f, 0.95f * m_interfaceOpacity }
            : ccColor4F{ 0.4f, 1.f, 0.55f, 0.95f * m_interfaceOpacity };
        auto outline = this->outlineRect(*sel);
        strokeRect(m_outline, outline, outlineColor, 2.f);

        // glow pass: a wider faint stroke reads as a soft highlight.
        auto glow = outline;
        glow.origin.x -= 3.f; glow.origin.y -= 3.f;
        glow.size.width += 6.f; glow.size.height += 6.f;
        strokeRect(m_outline, glow, { outlineColor.r, outlineColor.g, outlineColor.b, 0.25f * m_interfaceOpacity }, 1.f);

        // corner handles for a drag-affordance look.
        float hs = 4.f;
        ccColor4F fill = { outlineColor.r, outlineColor.g, outlineColor.b, m_interfaceOpacity };
        ccColor4F edge = { 1.f, 1.f, 1.f, 0.9f * m_interfaceOpacity };
        CCPoint corners[4] = {
            { outline.getMinX(), outline.getMinY() }, { outline.getMaxX(), outline.getMinY() },
            { outline.getMaxX(), outline.getMaxY() }, { outline.getMinX(), outline.getMaxY() },
        };
        for (auto const& c : corners) {
            CCPoint q[4] = { { c.x - hs, c.y - hs }, { c.x + hs, c.y - hs }, { c.x + hs, c.y + hs }, { c.x - hs, c.y + hs } };
            m_grip->drawPolygon(q, 4, fill, 1.f, edge);
        }

        if (!locked) {
            CCPoint g = this->gripPos(*sel);
            float h = kGripSize / 2.f;
            CCPoint pts[4] = { { g.x - h, g.y - h }, { g.x + h, g.y - h }, { g.x + h, g.y + h }, { g.x - h, g.y + h } };
            m_grip->drawPolygon(pts, 4, { 0.27f, 1.f, 0.51f, m_interfaceOpacity }, 1.f, edge);

            CCPoint rg = this->rotateGripPos(*sel);
            m_grip->drawSegment({ outline.getMidX(), outline.getMaxY() }, rg, 1.f, { 0.6f, 0.8f, 1.f, 0.7f * m_interfaceOpacity });
            m_grip->drawDot(rg, kGripSize * 0.4f, { 0.5f, 0.8f, 1.f, m_interfaceOpacity });
        }
    }

    this->updateHint();
    if (m_opacitySlider) m_opacitySlider->setVisible(sel != nullptr && !m_closing);
    this->refreshInspector();
}

void MainMenuLayoutEditor::updateHint() {
    if (!m_status) return;
    auto* sel = this->selectedItem();
    std::string status;
    if (!sel || !sel->target.node) {
        status = Localization::get().getString("menu_layout.none_selected");
    } else {
        auto w = worldPos(sel->target.node);
        float scale = sel->target.node->getScale();
        float opacity = 100.f;
        if (auto it = m_live.find(sel->target.key); it != m_live.end()) opacity = std::clamp(it->second.opacity, 0.f, 1.f) * 100.f;
        status = fmt::format(fmt::runtime(Localization::get().getString("menu_layout.status")),
            sel->target.label, w.x, w.y, scale, std::round(opacity));
    }
    if (status != m_status->getString()) m_status->setString(status.c_str());
}

bool MainMenuLayoutEditor::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_closing || m_interfaceElapsed < kTransitionDuration) return true;
    auto wp = touch->getLocation();

    // collapse arrow stays tappable.
    if (m_collapseBtn && m_collapseBtn->getParent()) {
        auto c = m_collapseBtn->getParent()->convertToWorldSpace(m_collapseBtn->getPosition());
        if (ccpDistanceSQ(wp, c) <= 24.f * 24.f) return false;
    }

    // bottom strip belongs to menu/slider; collapsed, the screen is canvas.
    float strip = m_collapsed ? 0.f : kCanvasBottom;
    if (wp.y <= strip) return false;

    // top bar and inspector own their regions so their menus get the touch.
    auto winSize = CCDirector::get()->getWinSize();
    if (wp.y >= winSize.height - kTopBarHeight) return false;
    if (m_inspector && m_inspector->isVisible()) {
        auto bl = m_inspector->convertToWorldSpace({ 0.f, 0.f });
        auto sz = m_inspector->getContentSize();
        CCRect ir(bl.x, bl.y, sz.width, sz.height);
        if (ir.containsPoint(wp)) return false;
    }

    if (!m_transitions.empty()) return true;

    if (auto* sel = this->selectedItem()) {
        if (sel->target.node && sel->target.node->getParent() && !this->isSelectionLocked()) {
            auto r = this->itemRect(*sel);
            CCPoint rgrip = this->rotateGripPos(*sel);
            if (ccpDistanceSQ(wp, rgrip) <= kRotateGripHit * kRotateGripHit) {
                m_drag = DragMode::Rotate;
                m_dragChanged = false;
                m_scaleFixedWorld = ccp(r.getMidX(), r.getMidY());
                m_rotateStartAngle = CC_RADIANS_TO_DEGREES(std::atan2(wp.y - m_scaleFixedWorld.y, wp.x - m_scaleFixedWorld.x));
                if (auto* layout = this->liveLayout(*sel)) m_itemStartRotation = layout->rotation;
                return true;
            }
            CCPoint grip = this->gripPos(*sel);
            if (ccpDistanceSQ(wp, grip) <= kGripHit * kGripHit) {
                m_drag = DragMode::Scale;
                m_dragChanged = false;
                m_scaleFixedWorld = ccp(r.getMidX(), r.getMidY());
                m_scaleStartDist = std::max(8.f, ccpDistance(wp, m_scaleFixedWorld));
                if (auto* layout = this->liveLayout(*sel)) m_itemStartScale = layout->scale;
                return true;
            }
        }
    }

    auto* hit = this->findItemAt(wp);
    if (!hit) {
        this->selectIndex(-1);
        this->redraw();
        return true;
    }

    int idx = static_cast<int>(hit - m_items.data());
    this->selectIndex(idx);
    auto* node = hit->target.node.data();
    if (node && node->getParent() && !this->isSelectionLocked()) {
        m_drag = DragMode::Move;
        m_dragChanged = false;
        m_touchStart = wp;
        m_itemStartWorld = worldPos(node);
    }
    this->redraw();
    return true;
}

void MainMenuLayoutEditor::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_closing || !m_transitions.empty()) return;
    auto* sel = this->selectedItem();
    if (!sel || !sel->target.node || !sel->target.node->getParent()) return;
    auto wp = touch->getLocation();
    auto* layout = this->liveLayout(*sel);
    if (!layout) return;

    if (m_drag == DragMode::Move) {
        auto proposed = m_itemStartWorld + (wp - m_touchStart);
        auto snapped = this->snapWorld(*sel, proposed);
        layout->position = sel->target.node->getParent()->convertToNodeSpace(snapped);
        this->applyLive(*sel);
        m_dragChanged = true;
    } else if (m_drag == DragMode::Scale) {
        float factor = ccpDistance(wp, m_scaleFixedWorld) / m_scaleStartDist;
        float s = std::clamp(m_itemStartScale * factor, kMinScale, kMaxScale);
        layout->scale = s;
        layout->scaleX = s;
        layout->scaleY = s;
        this->applyLive(*sel);
        m_dragChanged = true;
    } else if (m_drag == DragMode::Rotate) {
        float now = CC_RADIANS_TO_DEGREES(std::atan2(wp.y - m_scaleFixedWorld.y, wp.x - m_scaleFixedWorld.x));
        float delta = m_rotateStartAngle - now;
        float rot = m_itemStartRotation + delta;
        auto* kd = CCKeyboardDispatcher::get();
        if (kd && kd->getShiftKeyPressed()) rot = std::round(rot / 15.f) * 15.f;
        layout->rotation = std::fmod(rot, 360.f);
        this->applyLive(*sel);
        m_dragChanged = true;
    }
    this->redraw();
}

void MainMenuLayoutEditor::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_guideX) m_guideX->setVisible(false);
    if (m_guideY) m_guideY->setVisible(false);
    bool changed = m_dragChanged && m_drag != DragMode::None;
    m_drag = DragMode::None;
    m_dragChanged = false;
    if (changed) this->pushHistory();
}

void MainMenuLayoutEditor::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    this->ccTouchEnded(touch, event);
}

void MainMenuLayoutEditor::keyBackClicked() {
    this->cancelAndClose();
}

void MainMenuLayoutEditor::keyDown(enumKeyCodes key, double) {
    if (m_closing) return;
    auto* kd = CCKeyboardDispatcher::get();
    bool ctrl = kd && kd->getControlKeyPressed();
    bool shift = kd && kd->getShiftKeyPressed();

    if (key == enumKeyCodes::KEY_Escape) { this->cancelAndClose(); return; }
    if (ctrl && key == enumKeyCodes::KEY_S) { this->saveAndClose(); return; }
    if (ctrl && key == enumKeyCodes::KEY_Z) { this->undo(); return; }
    if (ctrl && key == enumKeyCodes::KEY_Y) { this->redo(); return; }
    if (!ctrl && key == enumKeyCodes::KEY_G) { this->setGridEnabled(!m_gridOn); return; }

    if (!m_transitions.empty()) return;
    if (!this->selectedItem()) return;

    if (!ctrl && key == enumKeyCodes::KEY_L) { this->toggleSelectionLock(); return; }
    if (this->isSelectionLocked()) return;
    if (!ctrl && key == enumKeyCodes::KEY_C) { this->centerSelection(true, true); return; }
    if (key == enumKeyCodes::KEY_Delete || key == enumKeyCodes::KEY_Backspace) {
        this->onToggleHidden(nullptr);
        return;
    }
    if (key == enumKeyCodes::KEY_Add || key == enumKeyCodes::KEY_OEMPlus) { this->scaleSelection(1.05f); this->pushHistory(); this->refreshInspector(); this->redraw(); return; }
    if (key == enumKeyCodes::KEY_Subtract || key == enumKeyCodes::KEY_OEMMinus) { this->scaleSelection(1.f / 1.05f); this->pushHistory(); this->refreshInspector(); this->redraw(); return; }

    float step = shift ? 10.f : 1.f;
    CCPoint d{ 0.f, 0.f };
    if (key == enumKeyCodes::KEY_Left || key == enumKeyCodes::KEY_ArrowLeft) d.x = -step;
    else if (key == enumKeyCodes::KEY_Right || key == enumKeyCodes::KEY_ArrowRight) d.x = step;
    else if (key == enumKeyCodes::KEY_Up || key == enumKeyCodes::KEY_ArrowUp) d.y = step;
    else if (key == enumKeyCodes::KEY_Down || key == enumKeyCodes::KEY_ArrowDown) d.y = -step;
    else return;
    this->nudgeSelection(d);
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::update(float dt) {
    auto* root = this->getTargetRoot();
    if (!root) { this->removeFromParent(); return; }

    // scene changed with editor open: close to avoid use-after-free.
    auto* scene = CCDirector::get()->getRunningScene();
    bool attached = false;
    for (CCNode* p = this->getParent(); p; p = p->getParent()) {
        if (p == scene) { attached = true; break; }
    }
    if (!attached) { this->removeFromParent(); return; }

    this->updateAnimations(dt);
    if (m_closing && m_interfaceElapsed >= kCloseDuration && m_transitions.empty()) {
        this->removeFromParent();
        return;
    }
    this->redraw();
}

void MainMenuLayoutEditor::onSave(CCObject*) {
    if (m_closing) return;
    auto* root = this->getTargetRoot();
    if (root) {
        MainMenuLayoutManager::get().mergeCustomFromButtons(m_live);
        PaimonNotify::show(Localization::get().getString("menu_layout.saved"), NotificationIcon::Success);
    }
    this->selectIndex(-1);
    this->beginClose(true);
}

void MainMenuLayoutEditor::onCancel(CCObject*) {
    if (m_closing) return;

    // dirty only when live differs from the state captured on open.
    bool dirty = false;
    for (auto const& [key, layout] : m_live) {
        auto it = m_initial.find(key);
        if (it == m_initial.end()) { dirty = true; break; }
        if (!(std::abs(layout.position.x - it->second.position.x) < 0.05f &&
              std::abs(layout.position.y - it->second.position.y) < 0.05f &&
              std::abs(layout.scale - it->second.scale) < 0.001f &&
              std::abs(layout.rotation - it->second.rotation) < 0.001f &&
              std::abs(layout.opacity - it->second.opacity) < 0.001f &&
              layout.hidden == it->second.hidden && layout.locked == it->second.locked &&
              layout.layer == it->second.layer)) {
            dirty = true;
            break;
        }
    }

    auto doCancel = [this] {
        for (auto const& item : m_items) {
            auto it = m_initial.find(item.target.key);
            if (it == m_initial.end()) continue;
            m_live[item.target.key] = it->second;
            this->applyLive(item);
        }
        if (auto* root = this->getTargetRoot()) {
            MainMenuLayoutManager::get().syncShapes(root, m_initialShapes);
        }
        this->beginClose(false);
    };

    if (!dirty) { doCancel(); return; }

    auto& loc = Localization::get();
    WeakRef<MainMenuLayoutEditor> self = this;
    geode::createQuickPopup(
        loc.getString("menu_layout.cancel").c_str(),
        "Hay cambios sin guardar. Deseas descartarlos?",
        "No",
        "Si",
        [self, doCancel](auto*, bool yes) {
            auto* ed = self.lock().data();
            if (!ed || !ed->getParent() || ed->m_closing || !yes) return;
            doCancel();
        });
}

void MainMenuLayoutEditor::onResetSelected(CCObject*) {
    if (m_closing) return;
    auto* sel = this->selectedItem();
    if (!sel) return;
    this->resetItemToDefault(*sel);
    if (auto* layout = this->liveLayout(*sel)) m_opacitySlider->setValue(std::clamp(layout->opacity, 0.f, 1.f));
    this->pushHistory();
    this->redraw();
}

void MainMenuLayoutEditor::onResetAll(CCObject*) {
    if (m_closing) return;
    auto* root = this->getTargetRoot();
    if (!root) return;

    auto& loc = Localization::get();
    WeakRef<MainMenuLayoutEditor> self = this;
    geode::createQuickPopup(
        loc.getString("menu_layout.reset_all").c_str(),
        "Esto restaura TODOS los botones a su posicion base. Continuar?",
        "No",
        "Si",
        [self](auto*, bool yes) {
            auto* ed = self.lock().data();
            if (!ed || !ed->getParent() || ed->m_closing || !yes) return;
            for (auto const& item : ed->m_items) {
                ed->resetItemToDefault(item);
            }
            ed->selectIndex(-1);
            ed->pushHistory();
            ed->redraw();
            PaimonNotify::show(Localization::get().getString("menu_layout.reset_done"), NotificationIcon::Info);
        });
}

void MainMenuLayoutEditor::onToggleHidden(CCObject*) {
    if (m_closing) return;
    auto* sel = this->selectedItem();
    if (!sel) return;
    auto* layout = this->liveLayout(*sel);
    if (!layout) return;
    layout->hidden = !layout->hidden;
    if (!layout->hidden && layout->opacity <= 0.01f) layout->opacity = 1.f;
    this->animateLive(*sel);
    this->pushHistory();
    this->redraw();
}

void MainMenuLayoutEditor::onOpacityChanged(CCObject*) {
    if (m_closing || !m_transitions.empty()) return;
    auto* sel = this->selectedItem();
    if (!sel || !m_opacitySlider) return;
    auto* layout = this->liveLayout(*sel);
    if (!layout) return;
    layout->opacity = std::clamp(m_opacitySlider->getValue(), 0.f, 1.f);
    layout->hidden = false;
    this->applyLive(*sel);
    this->redraw();
}

void MainMenuLayoutEditor::onToggleBar(CCObject*) {
    if (m_closing) return;
    m_collapsed = !m_collapsed;
    if (m_barContainer) {
        m_barContainer->stopAllActions();
        m_barContainer->runAction(CCEaseSineOut::create(CCMoveTo::create(
            0.22f, { 0.f, m_collapsed ? -(kCanvasBottom + 16.f) : 0.f })));
    }
    if (m_collapseArrow) {
        m_collapseArrow->stopAllActions();
        m_collapseArrow->runAction(CCEaseSineOut::create(CCRotateTo::create(0.22f, m_collapsed ? 90.f : -90.f)));
    }
}

void MainMenuLayoutEditor::onUndo(CCObject*) { if (!m_closing) this->undo(); }
void MainMenuLayoutEditor::onRedo(CCObject*) { if (!m_closing) this->redo(); }

void MainMenuLayoutEditor::onToggleGrid(CCObject*) {
    if (m_closing) return;
    this->setGridEnabled(!m_gridOn);
}

void MainMenuLayoutEditor::onToggleSnap(CCObject*) {
    if (m_closing) return;
    this->setSnapEnabled(!m_snapGrid);
}

void MainMenuLayoutEditor::onToggleLock(CCObject*) {
    if (m_closing) return;
    this->toggleSelectionLock();
}

void MainMenuLayoutEditor::onBringFront(CCObject*) { if (!m_closing) this->bringSelection(1); }
void MainMenuLayoutEditor::onBringBack(CCObject*) { if (!m_closing) this->bringSelection(-1); }

void MainMenuLayoutEditor::onInspectorStep(CCObject* sender) {
    if (m_closing || this->isSelectionLocked()) return;
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    int tag = btn->getTag();
    int field = tag / 10;
    int sign = (tag % 10 == 1) ? 1 : -1;
    auto* kd = CCKeyboardDispatcher::get();
    bool big = kd && kd->getShiftKeyPressed();

    switch (field) {
        case 0: this->nudgeSelection({ sign * (big ? 10.f : 1.f), 0.f }); break;
        case 1: this->nudgeSelection({ 0.f, sign * (big ? 10.f : 1.f) }); break;
        case 2: this->scaleSelection(sign > 0 ? (big ? 1.2f : 1.05f) : (big ? 1.f / 1.2f : 1.f / 1.05f)); break;
        case 3: this->rotateSelection(sign * (big ? 45.f : 5.f)); break;
        case 4: {
            auto* s = this->selectedItem();
            auto* layout = s ? this->liveLayout(*s) : nullptr;
            if (layout) this->setSelectionOpacity(layout->opacity + sign * (big ? 0.25f : 0.05f));
            break;
        }
        default: return;
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::saveAndClose() { this->onSave(nullptr); }
void MainMenuLayoutEditor::cancelAndClose() { this->onCancel(nullptr); }

void MainMenuLayoutEditor::onSavePreset(CCObject*) { this->openPresetPicker(true); }
void MainMenuLayoutEditor::onLoadPreset(CCObject*) { this->openPresetPicker(false); }

void MainMenuLayoutEditor::openPresetPicker(bool saveMode) {
    if (m_closing) return;
    WeakRef<MainMenuLayoutEditor> self = this;
    auto* popup = MainMenuLayoutPresetPopup::create(
        saveMode ? MainMenuLayoutPresetPopup::Mode::Save : MainMenuLayoutPresetPopup::Mode::Load,
        [self, saveMode](int slot) {
            auto* editor = self.lock().data();
            if (!editor || !editor->getParent() || editor->m_closing) return;

            if (saveMode) {
                MainMenuLayoutPresetManager::get().setPreset(slot, editor->buildSnapshot());
                PaimonNotify::show(fmt::format(fmt::runtime(Localization::get().getString("menu_layout.preset_saved")), slot + 1), NotificationIcon::Success);
                return;
            }

            auto preset = MainMenuLayoutPresetManager::get().getPreset(slot);
            if (!preset) {
                PaimonNotify::show(Localization::get().getString("menu_layout.presets_empty_slot"), NotificationIcon::Warning);
                return;
            }
            editor->applyHistory(preset->snapshot);
            editor->pushHistory();
            editor->selectIndex(-1);
            editor->redraw();
            PaimonNotify::show(fmt::format(fmt::runtime(Localization::get().getString("menu_layout.preset_loaded")), slot + 1), NotificationIcon::Success);
        }
    );
    if (popup) popup->show();
}

} // namespace paimon::menu_layout
