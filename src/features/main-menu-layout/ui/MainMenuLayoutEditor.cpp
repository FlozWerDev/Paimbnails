#include "MainMenuLayoutEditor.hpp"

#include "MainMenuLayoutPresetPopup.hpp"
#include "../services/MainMenuLayoutPresetManager.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Layout.hpp>

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
    constexpr float kGripHit = 22.f;
    constexpr float kCanvasBottom = 84.f;
    constexpr std::size_t kHistoryLimit = 50;

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

    auto* dark = CCLayerColor::create({ 0, 0, 0, 110 });
    dark->setContentSize(winSize);
    this->addChild(dark, -1);

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

    this->buildUI();
    this->collectItems();
    this->disableTargetMenus();
    this->pushHistory();
    this->redraw();
    return true;
}

void MainMenuLayoutEditor::buildUI() {
    auto winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    m_status = CCLabelBMFont::create(loc.getString("menu_layout.none_selected").c_str(), "chatFont.fnt");
    m_status->setScale(0.5f);
    m_status->setColor({ 220, 230, 245 });
    m_status->setPosition({ winSize.width / 2.f, winSize.height - 16.f });
    this->addChild(m_status, 31);

    // Toda la barra inferior vive en un contenedor que se puede "bajar"
    // (colapsar) con una unica flecha, para liberar la zona inferior y poder
    // mover los botones del menu que queden debajo.
    m_barContainer = CCNode::create();
    m_barContainer->setPosition({ 0.f, 0.f });
    this->addChild(m_barContainer, 30);

    if (auto* barBg = paimon::SpriteHelper::createDarkPanel(winSize.width, kCanvasBottom, 190, 0.f)) {
        barBg->setAnchorPoint({ 0.f, 0.f });
        barBg->setPosition({ 0.f, 0.f });
        m_barContainer->addChild(barBg, 0);
    }

    // Opacity slider: oculto hasta que haya algo seleccionado.
    m_opacitySlider = Slider::create(this, menu_selector(MainMenuLayoutEditor::onOpacityChanged));
    m_opacitySlider->setScale(0.7f);
    m_opacitySlider->setPosition({ winSize.width / 2.f, kCanvasBottom - 18.f });
    m_opacitySlider->setValue(1.f);
    m_opacitySlider->setVisible(false);
    m_barContainer->addChild(m_opacitySlider, 2);

    m_bar = CCMenu::create();
    m_bar->setContentSize({ winSize.width - 24.f, 40.f });
    m_bar->setPosition({ winSize.width / 2.f, 22.f });
    m_bar->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_barContainer->addChild(m_bar, 1);

    auto addBtn = [&](char const* key, SEL_MenuHandler cb, char const* bg, int width) {
        auto* spr = ButtonSprite::create(loc.getString(key).c_str(), width, true, "goldFont.fnt", bg, 26.f, 0.5f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        m_bar->addChild(btn);
        return btn;
    };

    addBtn("menu_layout.cancel", menu_selector(MainMenuLayoutEditor::onCancel), "GJ_button_06.png", 56);
    addBtn("menu_layout.reset_selected", menu_selector(MainMenuLayoutEditor::onResetSelected), "GJ_button_05.png", 70);
    addBtn("menu_layout.reset_all", menu_selector(MainMenuLayoutEditor::onResetAll), "GJ_button_05.png", 70);
    addBtn("menu_layout.hide_selected", menu_selector(MainMenuLayoutEditor::onToggleHidden), "GJ_button_04.png", 60);
    addBtn("menu_layout.load_preset", menu_selector(MainMenuLayoutEditor::onLoadPreset), "GJ_button_03.png", 68);
    addBtn("menu_layout.save_preset", menu_selector(MainMenuLayoutEditor::onSavePreset), "GJ_button_02.png", 68);
    addBtn("menu_layout.save", menu_selector(MainMenuLayoutEditor::onSave), "GJ_button_01.png", 56);

    m_bar->updateLayout();

    // Flecha unica para colapsar/expandir la barra (siempre visible, fuera
    // del contenedor que se baja). Centro-inferior, justo sobre la barra.
    auto* toggleMenu = CCMenu::create();
    toggleMenu->setPosition({ 0.f, 0.f });
    this->addChild(toggleMenu, 33);
    m_collapseArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (m_collapseArrow) {
        m_collapseArrow->setScale(0.7f);
        m_collapseArrow->setRotation(-90.f); // apunta hacia abajo (bajar)
        m_collapseBtn = CCMenuItemSpriteExtra::create(m_collapseArrow, this, menu_selector(MainMenuLayoutEditor::onToggleBar));
        m_collapseBtn->setPosition({ winSize.width / 2.f, kCanvasBottom + 12.f });
        toggleMenu->addChild(m_collapseBtn);
    }
}

void MainMenuLayoutEditor::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -INT_MAX + 200, true);
}

void MainMenuLayoutEditor::collectItems() {
    m_items.clear();
    m_live.clear();
    m_initial.clear();
    m_selected = -1;

    auto* root = this->getTargetRoot();
    if (!root) return;

    auto& mgr = MainMenuLayoutManager::get();
    mgr.captureDefaultsAndApply(root);

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
}

MenuButtonLayout* MainMenuLayoutEditor::liveLayout(Item const& item) {
    auto it = m_live.find(item.target.key);
    return it == m_live.end() ? nullptr : &it->second;
}

void MainMenuLayoutEditor::applyLive(Item const& item) {
    auto it = m_live.find(item.target.key);
    if (it == m_live.end()) return;
    MainMenuLayoutManager::applyLayout(item.target, it->second);
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
    MainMenuLayoutManager::applyLayout(item.target, *def);
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
    std::vector<EditableMenuButton> targets;
    targets.reserve(m_items.size());
    for (auto const& item : m_items) targets.push_back(item.target);
    MainMenuLayoutManager::get().applySnapshot(targets, snapshot, root);
    m_live = snapshot.buttons;
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
        strokeRect(m_highlights, this->itemRect(item), { 0.35f, 0.65f, 1.f, 0.4f }, 1.f);
    }

    if (sel && sel->target.node && sel->target.node->getParent()) {
        auto r = this->itemRect(*sel);
        r.origin.x -= kOutlinePad;
        r.origin.y -= kOutlinePad;
        r.size.width += kOutlinePad * 2.f;
        r.size.height += kOutlinePad * 2.f;
        strokeRect(m_outline, r, { 0.4f, 1.f, 0.55f, 0.95f }, 2.f);

        // single scale grip at bottom-right
        CCPoint g{ r.getMaxX(), r.getMinY() };
        float h = kGripSize / 2.f;
        CCPoint pts[4] = { { g.x - h, g.y - h }, { g.x + h, g.y - h }, { g.x + h, g.y + h }, { g.x - h, g.y + h } };
        m_grip->drawPolygon(pts, 4, { 0.27f, 1.f, 0.51f, 1.f }, 1.f, { 1.f, 1.f, 1.f, 0.9f });
    }

    if (m_status) {
        if (!sel || !sel->target.node) {
            m_status->setString(Localization::get().getString("menu_layout.none_selected").c_str());
        } else {
            auto w = worldPos(sel->target.node);
            float scale = sel->target.node->getScale();
            float opacity = 100.f;
            if (auto it = m_live.find(sel->target.key); it != m_live.end()) opacity = std::clamp(it->second.opacity, 0.f, 1.f) * 100.f;
            m_status->setString(fmt::format(fmt::runtime(Localization::get().getString("menu_layout.status")),
                sel->target.label, w.x, w.y, scale, std::round(opacity)).c_str());
        }
    }

    if (m_opacitySlider) m_opacitySlider->setVisible(sel != nullptr);
}

bool MainMenuLayoutEditor::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto wp = touch->getLocation();

    // La flecha de colapso siempre es accesible.
    if (m_collapseBtn && m_collapseBtn->getParent()) {
        auto c = m_collapseBtn->getParent()->convertToWorldSpace(m_collapseBtn->getPosition());
        if (ccpDistanceSQ(wp, c) <= 24.f * 24.f) return false;
    }

    // Franja inferior de controles → la maneja el menu/slider. Si la barra
    // esta colapsada, toda la pantalla es lienzo para mover botones de abajo.
    float strip = m_collapsed ? 0.f : kCanvasBottom;
    if (wp.y <= strip) return false;

    if (auto* sel = this->selectedItem()) {
        if (sel->target.node && sel->target.node->getParent()) {
            auto r = this->itemRect(*sel);
            CCPoint grip{ r.getMaxX(), r.getMinY() };
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
    if (node && node->getParent()) {
        m_drag = DragMode::Move;
        m_dragChanged = false;
        m_touchStart = wp;
        m_itemStartWorld = worldPos(node);
    }
    this->redraw();
    return true;
}

void MainMenuLayoutEditor::ccTouchMoved(CCTouch* touch, CCEvent*) {
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
    auto* kd = CCKeyboardDispatcher::get();
    bool ctrl = kd && kd->getControlKeyPressed();
    bool shift = kd && kd->getShiftKeyPressed();

    if (key == enumKeyCodes::KEY_Escape) { this->cancelAndClose(); return; }
    if (ctrl && key == enumKeyCodes::KEY_S) { this->saveAndClose(); return; }
    if (ctrl && key == enumKeyCodes::KEY_Z) { this->undo(); return; }
    if (ctrl && key == enumKeyCodes::KEY_Y) { this->redo(); return; }

    if (!this->selectedItem()) return;

    if (key == enumKeyCodes::KEY_Delete || key == enumKeyCodes::KEY_Backspace) {
        this->onToggleHidden(nullptr);
        return;
    }
    if (key == enumKeyCodes::KEY_Add || key == enumKeyCodes::KEY_OEMPlus) { this->scaleSelection(1.05f); this->pushHistory(); this->redraw(); return; }
    if (key == enumKeyCodes::KEY_Subtract || key == enumKeyCodes::KEY_OEMMinus) { this->scaleSelection(1.f / 1.05f); this->pushHistory(); this->redraw(); return; }

    float step = shift ? 10.f : 1.f;
    CCPoint d{ 0.f, 0.f };
    if (key == enumKeyCodes::KEY_Left || key == enumKeyCodes::KEY_ArrowLeft) d.x = -step;
    else if (key == enumKeyCodes::KEY_Right || key == enumKeyCodes::KEY_ArrowRight) d.x = step;
    else if (key == enumKeyCodes::KEY_Up || key == enumKeyCodes::KEY_ArrowUp) d.y = step;
    else if (key == enumKeyCodes::KEY_Down || key == enumKeyCodes::KEY_ArrowDown) d.y = -step;
    else return;
    this->nudgeSelection(d);
    this->pushHistory();
    this->redraw();
}

void MainMenuLayoutEditor::update(float) {
    auto* root = this->getTargetRoot();
    if (!root) { this->removeFromParent(); return; }

    // Si la escena cambio (ej. entrar a un nivel con el editor abierto),
    // cerramos para evitar use-after-free sobre nodos liberados.
    auto* scene = CCDirector::get()->getRunningScene();
    bool attached = false;
    for (CCNode* p = this->getParent(); p; p = p->getParent()) {
        if (p == scene) { attached = true; break; }
    }
    if (!attached) { this->removeFromParent(); return; }

    this->redraw();
}

void MainMenuLayoutEditor::onSave(CCObject*) {
    auto* root = this->getTargetRoot();
    if (root) {
        MainMenuLayoutManager::get().mergeCustomFromButtons(m_live);
        PaimonNotify::show(Localization::get().getString("menu_layout.saved"), NotificationIcon::Success);
    }
    this->removeFromParent();
}

void MainMenuLayoutEditor::onCancel(CCObject*) {
    // Revertir a lo que habia al abrir el editor.
    for (auto const& item : m_items) {
        auto it = m_initial.find(item.target.key);
        if (it == m_initial.end()) continue;
        m_live[item.target.key] = it->second;
        MainMenuLayoutManager::applyLayout(item.target, it->second);
    }
    this->removeFromParent();
}

void MainMenuLayoutEditor::onResetSelected(CCObject*) {
    auto* sel = this->selectedItem();
    if (!sel) return;
    this->resetItemToDefault(*sel);
    if (auto* layout = this->liveLayout(*sel)) m_opacitySlider->setValue(std::clamp(layout->opacity, 0.f, 1.f));
    this->pushHistory();
    this->redraw();
}

void MainMenuLayoutEditor::onResetAll(CCObject*) {
    auto* root = this->getTargetRoot();
    if (!root) return;
    for (auto const& item : m_items) {
        this->resetItemToDefault(item);
    }
    this->selectIndex(-1);
    this->pushHistory();
    this->redraw();
    PaimonNotify::show(Localization::get().getString("menu_layout.reset_done"), NotificationIcon::Info);
}

void MainMenuLayoutEditor::onToggleHidden(CCObject*) {
    auto* sel = this->selectedItem();
    if (!sel) return;
    auto* layout = this->liveLayout(*sel);
    if (!layout) return;
    layout->hidden = !layout->hidden;
    if (!layout->hidden && layout->opacity <= 0.01f) layout->opacity = 1.f;
    this->applyLive(*sel);
    this->pushHistory();
    this->redraw();
}

void MainMenuLayoutEditor::onOpacityChanged(CCObject*) {
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
    m_collapsed = !m_collapsed;
    if (m_barContainer) m_barContainer->setPositionY(m_collapsed ? -(kCanvasBottom + 16.f) : 0.f);
    if (m_collapseArrow) m_collapseArrow->setRotation(m_collapsed ? 90.f : -90.f);
}

void MainMenuLayoutEditor::saveAndClose() { this->onSave(nullptr); }
void MainMenuLayoutEditor::cancelAndClose() { this->onCancel(nullptr); }

void MainMenuLayoutEditor::onSavePreset(CCObject*) { this->openPresetPicker(true); }
void MainMenuLayoutEditor::onLoadPreset(CCObject*) { this->openPresetPicker(false); }

void MainMenuLayoutEditor::openPresetPicker(bool saveMode) {
    WeakRef<MainMenuLayoutEditor> self = this;
    auto* popup = MainMenuLayoutPresetPopup::create(
        saveMode ? MainMenuLayoutPresetPopup::Mode::Save : MainMenuLayoutPresetPopup::Mode::Load,
        [self, saveMode](int slot) {
            auto* editor = self.lock().data();
            if (!editor || !editor->getParent()) return;

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
