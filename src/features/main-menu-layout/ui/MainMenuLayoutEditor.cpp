#include "MainMenuLayoutEditor.hpp"

#include "MainMenuDrawShapeNode.hpp"
#include "MainMenuLayoutPresetPopup.hpp"
#include "../services/MainMenuLayoutPresetManager.hpp"
#include "../../fonts/ui/FontPickerPopup.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/PopupManager.hpp>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    MainMenuLayoutEditor* s_active = nullptr;

    constexpr float kMinScale = 0.15f;
    constexpr float kMaxScale = 5.0f;
    constexpr float kMinShapeSize = 8.f;
    constexpr float kMaxShapeSize = 900.f;
    constexpr float kMinHit = 22.f;
    constexpr float kOutlinePad = 5.f;
    constexpr float kHandleDrawR = 4.2f;
    constexpr float kHandleHitR = 11.f;
    constexpr float kRotateArm = 26.f;
    constexpr float kRotateHitR = 13.f;
    constexpr float kSafeMargin = 14.f;

    constexpr float kTopBarH = 26.f;
    constexpr float kDockH = 42.f;
    constexpr float kDockMarginB = 4.f;
    constexpr float kLayersW = 176.f;
    constexpr float kInspW = 204.f;
    constexpr float kInspH = 232.f;
    constexpr float kCardMargin = 8.f;
    constexpr std::size_t kHistoryLimit = 60;
    constexpr float kIntroDuration = 0.24f;
    constexpr float kCloseDuration = 0.20f;

    std::string tr(char const* key) {
        return Localization::get().getString(key);
    }

    bool esLang() {
        return studio_ui::isSpanish();
    }

    char const* loc(char const* en, char const* es) {
        return esLang() ? es : en;
    }

    PaimonDrawNode* makeAlphaDrawNode() {
        auto* n = PaimonDrawNode::create();
        if (n) n->setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
        return n;
    }

    CCPoint worldPos(CCNode* node) {
        if (!node || !node->getParent()) return { 0.f, 0.f };
        return node->getParent()->convertToWorldSpace(node->getPosition());
    }

    CCRect nodeWorldRect(CCNode* node, float minHit) {
        if (!node || !node->getParent()) return { 0.f, 0.f, 0.f, 0.f };
        auto bb = node->boundingBox();
        auto* p = node->getParent();
        auto bl = p->convertToWorldSpace({ bb.getMinX(), bb.getMinY() });
        auto trPt = p->convertToWorldSpace({ bb.getMaxX(), bb.getMaxY() });
        CCRect r(std::min(bl.x, trPt.x), std::min(bl.y, trPt.y),
            std::abs(trPt.x - bl.x), std::abs(trPt.y - bl.y));
        if (r.size.width < minHit) {
            r.origin.x = r.getMidX() - minHit * 0.5f;
            r.size.width = minHit;
        }
        if (r.size.height < minHit) {
            r.origin.y = r.getMidY() - minHit * 0.5f;
            r.size.height = minHit;
        }
        return r;
    }

    CCRect nodeWorldBoxRect(CCNode* node) {
        if (!node || !node->getParent()) return { 0.f, 0.f, 0.f, 0.f };
        auto bl = node->convertToWorldSpace({ 0.f, 0.f });
        auto sz = node->getContentSize();
        auto tr = node->convertToWorldSpace({ sz.width, sz.height });
        return {
            std::min(bl.x, tr.x),
            std::min(bl.y, tr.y),
            std::abs(tr.x - bl.x),
            std::abs(tr.y - bl.y)
        };
    }

    void strokeRect(CCDrawNode* node, CCRect r, ccColor4F color, float thickness) {
        CCPoint bl{ r.getMinX(), r.getMinY() }, br{ r.getMaxX(), r.getMinY() };
        CCPoint tr{ r.getMaxX(), r.getMaxY() }, tl{ r.getMinX(), r.getMaxY() };
        node->drawSegment(bl, br, thickness, color);
        node->drawSegment(br, tr, thickness, color);
        node->drawSegment(tr, tl, thickness, color);
        node->drawSegment(tl, bl, thickness, color);
    }

    void drawDashedRect(CCDrawNode* node, CCRect r, ccColor4F color, float thickness, float dash = 5.f, float gap = 4.f) {
        auto drawEdge = [&](CCPoint a, CCPoint b) {
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float len = std::hypot(dx, dy);
            if (len < 0.5f) return;
            float ux = dx / len, uy = dy / len;
            for (float d = 0.f; d < len; d += dash + gap) {
                float dEnd = std::min(len, d + dash);
                node->drawSegment(
                    { a.x + ux * d, a.y + uy * d },
                    { a.x + ux * dEnd, a.y + uy * dEnd },
                    thickness, color);
            }
        };
        CCPoint bl{ r.getMinX(), r.getMinY() }, br{ r.getMaxX(), r.getMinY() };
        CCPoint tr{ r.getMaxX(), r.getMaxY() }, tl{ r.getMinX(), r.getMaxY() };
        drawEdge(bl, br);
        drawEdge(br, tr);
        drawEdge(tr, tl);
        drawEdge(tl, bl);
    }

    void drawSquareHandle(CCDrawNode* node, CCPoint c, float half, ccColor4F fill, ccColor4F edge) {
        CCPoint pts[4] = {
            { c.x - half, c.y - half },
            { c.x + half, c.y - half },
            { c.x + half, c.y + half },
            { c.x - half, c.y + half },
        };
        node->drawPolygon(pts, 4, fill, 1.f, edge);
    }

    void setLabelIfChanged(CCLabelBMFont* label, std::string const& text) {
        if (label && text != label->getString()) {
            label->setString(text.c_str());
        }
    }

    bool sameColor(ccColor3B a, ccColor3B b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    bool buttonLayoutEqual(MenuButtonLayout const& a, MenuButtonLayout const& b) {
        return std::abs(a.position.x - b.position.x) <= 0.05f &&
               std::abs(a.position.y - b.position.y) <= 0.05f &&
               std::abs(a.scale - b.scale) <= 0.001f &&
               std::abs(a.scaleX - b.scaleX) <= 0.001f &&
               std::abs(a.scaleY - b.scaleY) <= 0.001f &&
               std::abs(a.rotation - b.rotation) <= 0.01f &&
               std::abs(a.opacity - b.opacity) <= 0.001f &&
               a.hidden == b.hidden &&
               a.locked == b.locked &&
               a.layer == b.layer &&
               a.linkGroup == b.linkGroup &&
               a.hasColor == b.hasColor &&
               sameColor(a.color, b.color) &&
               a.fontFile == b.fontFile;
    }

    bool shapeLayoutEqual(DrawShapeLayout const& a, DrawShapeLayout const& b) {
        return a.id == b.id &&
               a.kind == b.kind &&
               std::abs(a.position.x - b.position.x) <= 0.05f &&
               std::abs(a.position.y - b.position.y) <= 0.05f &&
               std::abs(a.scaleX - b.scaleX) <= 0.001f &&
               std::abs(a.scaleY - b.scaleY) <= 0.001f &&
               std::abs(a.rotation - b.rotation) <= 0.01f &&
               std::abs(a.opacity - b.opacity) <= 0.001f &&
               a.hidden == b.hidden &&
               a.locked == b.locked &&
               std::abs(a.width - b.width) <= 0.05f &&
               std::abs(a.height - b.height) <= 0.05f &&
               std::abs(a.cornerRadius - b.cornerRadius) <= 0.05f &&
               sameColor(a.color, b.color) &&
               std::abs(a.strokeWidth - b.strokeWidth) <= 0.05f &&
               sameColor(a.strokeColor, b.strokeColor) &&
               a.layer == b.layer &&
               a.linkGroup == b.linkGroup;
    }

    std::string shortFontName(std::string const& fnt) {
        if (fnt.empty()) return loc("Default", "Normal");
        std::string name = fnt;
        if (auto p = name.find_last_of("/\\"); p != std::string::npos) name = name.substr(p + 1);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".fnt") == 0) {
            name.erase(name.size() - 4);
        }
        return name;
    }

    CCLabelBMFont* findEditableLabel(CCNode* node) {
        if (!node) return nullptr;
        return typeinfo_cast<CCLabelBMFont*>(node);
    }

    bool rectsIntersect(CCRect const& a, CCRect const& b) {
        return !(a.getMaxX() < b.getMinX() || a.getMinX() > b.getMaxX() ||
                 a.getMaxY() < b.getMinY() || a.getMinY() > b.getMaxY());
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
        for (auto const& elem : m_elements) {
            if (elem.isShape) continue;
            auto it = m_initialButtons.find(elem.id);
            if (it != m_initialButtons.end()) {
                MainMenuLayoutManager::applyLayout(elem.target, it->second);
            }
        }
        if (auto* root = this->getTargetRoot()) {
            MainMenuLayoutManager::get().syncShapes(root, m_initialShapes);
        }
    } else {
        for (auto const& elem : m_elements) {
            if (!elem.isShape && m_transitions.count(elem.id)) {
                this->applyElementToScene(elem);
            }
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
    m_sceneName = MainMenuLayoutManager::rootClassName(root);
    m_settings.load();

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

    m_grid = makeAlphaDrawNode();
    this->addChild(m_grid, 2);

    m_highlights = makeAlphaDrawNode();
    this->addChild(m_highlights, 10);

    m_guideX = makeAlphaDrawNode();
    m_guideX->setVisible(false);
    this->addChild(m_guideX, 14);

    m_guideY = makeAlphaDrawNode();
    m_guideY->setVisible(false);
    this->addChild(m_guideY, 14);

    m_outline = makeAlphaDrawNode();
    this->addChild(m_outline, 20);

    m_grip = makeAlphaDrawNode();
    this->addChild(m_grip, 24);

    m_marqueeDraw = makeAlphaDrawNode();
    this->addChild(m_marqueeDraw, 26);

    m_hudRoot = CCNode::create();
    m_hudRoot->setContentSize(winSize);
    m_hudRoot->setPosition({ 0.f, 0.f });
    this->addChild(m_hudRoot, 40);

    this->collectElements();
    this->disableTargetMenus();
    this->buildUI();

    this->captureInterfaceNodes(m_hudRoot);
    this->applyInterfaceOpacity();

    this->rebuildGrid();
    this->rebuildLayersList();
    this->pushHistory();
    this->refreshHudState();
    this->refreshInspector();
    this->syncHudVisibility();
    this->redraw();
    return true;
}

void MainMenuLayoutEditor::collectElements() {
    m_elements.clear();
    m_liveButtons.clear();
    m_initialButtons.clear();
    m_liveShapes.clear();
    m_shapeOrder.clear();
    m_initialShapes.clear();
    m_selectedIds.clear();

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
        m_liveButtons[button.key] = layout;
        m_initialButtons[button.key] = layout;

        EditorElement elem;
        elem.isShape = false;
        elem.id = button.key;
        elem.target = button;
        elem.info = studio_ui::describeButtonElement(button);
        m_elements.push_back(std::move(elem));
    }

    m_initialShapes = mgr.getShapesForScene(m_sceneName);
    mgr.syncShapes(root, m_initialShapes);
    for (auto const& s : m_initialShapes) {
        m_liveShapes[s.id] = s;
        m_shapeOrder.push_back(s.id);
    }
    m_nextShapeCounter = static_cast<int>(m_initialShapes.size()) + 1;
    this->rebuildShapeElements();
}

void MainMenuLayoutEditor::rebuildShapeElements() {
    m_elements.erase(
        std::remove_if(m_elements.begin(), m_elements.end(), [](EditorElement const& e) {
            return e.isShape;
        }),
        m_elements.end());

    auto* root = this->getTargetRoot();
    if (!root) return;

    std::unordered_map<std::string, MainMenuDrawShapeNode*> liveNodes;
    if (auto* container = root->getChildByID("paimon-draw-shape-container"_spr)) {
        if (auto* children = container->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (MainMenuLayoutManager::isDrawShapeNode(child)) {
                    auto* sn = typeinfo_cast<MainMenuDrawShapeNode*>(child);
                    if (sn) {
                        auto l = sn->readLayout();
                        liveNodes[l.id] = sn;
                    }
                }
            }
        }
    }

    int idx = 0;
    for (auto const& id : m_shapeOrder) {
        auto it = m_liveShapes.find(id);
        if (it == m_liveShapes.end()) continue;
        auto nodeIt = liveNodes.find(id);
        if (nodeIt == liveNodes.end() || !nodeIt->second) continue;

        EditorElement elem;
        elem.isShape = true;
        elem.id = id;
        elem.target.node = nodeIt->second;
        elem.target.key = id;
        elem.info = studio_ui::describeShapeElement(it->second, idx++);
        elem.target.label = elem.info.displayName;
        m_elements.push_back(std::move(elem));
    }
}

void MainMenuLayoutEditor::disableTargetMenus() {
    m_disabledMenus.clear();
    auto* root = this->getTargetRoot();
    if (!root) return;

    std::unordered_set<CCMenu*> seen;
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) return;
        if (auto* menu = typeinfo_cast<CCMenu*>(node)) {
            if (menu->isEnabled() && seen.insert(menu).second) {
                menu->setEnabled(false);
                m_disabledMenus.emplace_back(menu);
            }
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                walk(child);
            }
        }
    };
    walk(root);

    for (auto const& elem : m_elements) {
        auto* menu = elem.target.menu;
        if (menu && menu->isEnabled() && seen.insert(menu).second) {
            menu->setEnabled(false);
            m_disabledMenus.emplace_back(menu);
        }
    }
}

void MainMenuLayoutEditor::buildUI() {
    this->buildTopBar();
    this->buildDock();
    this->buildShapeBar();
    this->buildLayersPanel();
    this->buildInspector();
    this->buildPreviewBadge();
}

void MainMenuLayoutEditor::buildTopBar() {
    auto winSize = CCDirector::get()->getWinSize();
    float const barY = winSize.height - kTopBarH * 0.5f;

    m_topBar = CCNode::create();
    m_topBar->setPosition({ 0.f, 0.f });
    m_hudRoot->addChild(m_topBar, 1);

    auto* bg = paimon::ui::makeInset({ winSize.width + 12.f, kTopBarH + 4.f }, 195);
    bg->setPosition({ -6.f, winSize.height - kTopBarH });
    m_topBar->addChild(bg, 0);

    if (auto* div = paimon::ui::makeDivider(winSize.width, paimon::ui::palette::gold, 150)) {
        div->setPosition({ winSize.width * 0.5f, winSize.height - kTopBarH });
        m_topBar->addChild(div, 1);
    }

    m_titleLbl = CCLabelBMFont::create(tr("menu_layout.title").c_str(), "goldFont.fnt");
    m_titleLbl->setScale(0.40f);
    m_titleLbl->setAnchorPoint({ 0.f, 0.5f });
    m_titleLbl->setPosition({ 8.f, barY });
    m_topBar->addChild(m_titleLbl, 2);

    std::string sceneTag = m_sceneName == "PauseLayer" ? loc("Pause", "Pausa")
                         : m_sceneName == "LevelInfoLayer" ? loc("Level", "Nivel")
                         : "Menu";
    float const badgeX = 8.f + m_titleLbl->getScaledContentSize().width + 6.f;
    if (auto* badge = paimon::ui::makeInset({ 38.f, 15.f }, 135)) {
        badge->setPosition({ badgeX, barY - 7.5f });
        m_topBar->addChild(badge, 2);
        auto* sLbl = CCLabelBMFont::create(sceneTag.c_str(), "bigFont.fnt");
        sLbl->setScale(0.24f);
        sLbl->setColor(paimon::ui::palette::info);
        sLbl->setPosition({ 19.f, 7.5f });
        badge->addChild(sLbl, 1);
    }

    m_dirtyDot = CCLabelBMFont::create("*", "goldFont.fnt");
    m_dirtyDot->setScale(0.55f);
    m_dirtyDot->setPosition({ badgeX + 43.f, barY });
    m_dirtyDot->setVisible(false);
    m_topBar->addChild(m_dirtyDot, 2);

    m_topMenu = CCMenu::create();
    m_topMenu->setPosition({ 0.f, 0.f });
    m_topBar->addChild(m_topMenu, 3);

    auto* saveBtn = studio_ui::makeGdButton(
        tr("menu_layout.save"), 52.f, 18.f, paimon::ui::Btn::Green,
        [this] { this->onSave(nullptr); }, "goldFont.fnt");
    saveBtn->setPosition({ winSize.width - 31.f, barY });
    m_topMenu->addChild(saveBtn);

    auto* cancelBtn = studio_ui::makeGdButton(
        tr("menu_layout.cancel"), 54.f, 18.f, paimon::ui::Btn::Red,
        [this] { this->onCancel(nullptr); }, "goldFont.fnt");
    cancelBtn->setPosition({ winSize.width - 88.f, barY });
    m_topMenu->addChild(cancelBtn);

    m_previewBtn = studio_ui::makeGdButton(
        loc("Preview", "Vista"), 48.f, 18.f, paimon::ui::Btn::Blue,
        [this] { this->onTogglePreview(nullptr); }, "bigFont.fnt");
    m_previewBtn->setPosition({ winSize.width - 142.f, barY });
    m_topMenu->addChild(m_previewBtn);

    float const leftEdge = badgeX + 50.f;
    float const rightEdge = winSize.width - 170.f;
    m_hintMaxWidth = std::max(100.f, rightEdge - leftEdge - 8.f);

    m_hintLbl = CCLabelBMFont::create("", "chatFont.fnt");
    m_hintLbl->setColor(paimon::ui::palette::muted);
    m_hintLbl->setPosition({ (leftEdge + rightEdge) * 0.5f, barY });
    m_topBar->addChild(m_hintLbl, 2);

    // Floating live transform readout pill (stays visible during drag!)
    m_liveBadge = studio_ui::makeGdWindow({ 156.f, 22.f }, paimon::ui::Bg::Brown, 120);
    m_liveBadge->setAnchorPoint({ 0.5f, 1.f });
    m_liveBadge->setPosition({ winSize.width * 0.5f, winSize.height - kTopBarH - 5.f });
    m_liveBadge->setVisible(false);
    this->addChild(m_liveBadge, 45);

    m_liveBadgeLbl = CCLabelBMFont::create("", "bigFont.fnt");
    m_liveBadgeLbl->setScale(0.30f);
    m_liveBadgeLbl->setColor(paimon::ui::palette::gold);
    m_liveBadgeLbl->setPosition({ 78.f, 11.f });
    m_liveBadge->addChild(m_liveBadgeLbl, 3);
}

void MainMenuLayoutEditor::buildDock() {
    auto winSize = CCDirector::get()->getWinSize();

    struct ToolDef {
        char const* primary;
        char const* fallback;
        std::string caption;
        SEL_MenuHandler handler;
        CCMenuItemSpriteExtra** outPtr;
    };

    std::vector<ToolDef> defs = {
        { "GJ_undoBtn_001.png", "GJ_undoBtn_001.png", tr("menu_layout.tool_undo"), menu_selector(MainMenuLayoutEditor::onUndo), &m_undoBtn },
        { "GJ_redoBtn_001.png", "GJ_redoBtn_001.png", tr("menu_layout.tool_redo"), menu_selector(MainMenuLayoutEditor::onRedo), &m_redoBtn },
        { "square_01_001.png", "square_01_001.png", tr("menu_layout.tool_grid"), menu_selector(MainMenuLayoutEditor::onToggleGrid), &m_gridBtn },
        { "GJ_select_001.png", "GJ_select_001.png", tr("menu_layout.tool_snap"), menu_selector(MainMenuLayoutEditor::onToggleSnap), &m_snapBtn },
        { "GJ_optionsBtn_001.png", "GJ_optionsBtn_001.png", loc("Canvas", "Lienzo"), menu_selector(MainMenuLayoutEditor::onOpenCanvasSettings), nullptr },
        { "edit_areaModeBtn03_001.png", "GJ_select_001.png", loc("Multi", "Multi"), menu_selector(MainMenuLayoutEditor::onToggleMultiSelect), &m_multiBtn },
        { "GJ_plusBtn_001.png", "edit_addBtn_001.png", loc("Draw+", "Draw+"), menu_selector(MainMenuLayoutEditor::onToggleShapeBar), &m_shapeToolBtn },
        { "GJ_sortIcon_001.png", "GJ_optionsBtn_001.png", tr("menu_layout.layers_title"), menu_selector(MainMenuLayoutEditor::onToggleLayers), &m_layersBtn },
        { "folderIcon_001.png", "folderIcon_001.png", loc("Slots", "Slots"), menu_selector(MainMenuLayoutEditor::onOpenPresets), nullptr },
        { "edit_delBtn_001.png", "GJ_deleteIcon_001.png", tr("menu_layout.reset_all"), menu_selector(MainMenuLayoutEditor::onResetAll), nullptr },
        { "GJ_infoIcon_001.png", "GJ_infoIcon_001.png", tr("menu_layout.tool_help"), nullptr, nullptr },
    };

    constexpr float kToolW = 40.f;
    constexpr float kToolH = 34.f;
    constexpr float kGap = 2.f;
    constexpr float kPadX = 8.f;
    float const naturalW = kPadX * 2.f + defs.size() * kToolW + (defs.size() - 1) * kGap;
    float const fit = std::min(1.f, (winSize.width - 14.f) / naturalW);
    float const dockW = naturalW * fit;

    m_dock = CCNode::create();
    m_dock->setContentSize({ dockW, kDockH });
    m_dock->setAnchorPoint({ 0.5f, 0.f });
    m_dock->setPosition({ winSize.width * 0.5f, kDockMarginB });
    m_hudRoot->addChild(m_dock, 1);

    m_dock->addChild(studio_ui::makeGdWindow({ dockW, kDockH }, paimon::ui::Bg::Brown, 105), 0);

    m_dockMenu = CCMenu::create();
    m_dockMenu->setPosition({ 0.f, 0.f });
    m_dock->addChild(m_dockMenu, 3);

    float cursorX = kPadX * fit + (kToolW * fit) * 0.5f;
    float const centerY = kDockH * 0.5f;

    for (auto& def : defs) {
        CCMenuItemSpriteExtra* btn = nullptr;
        if (def.handler) {
            btn = studio_ui::makeDockTool(def.primary, def.fallback, def.caption, kToolW, kToolH, this, def.handler);
        } else {
            btn = studio_ui::makeDockTool(def.primary, def.fallback, def.caption, kToolW, kToolH,
                PaimonInfoTarget::shared(), menu_selector(PaimonInfoTarget::onInfo));
            btn->setUserObject(CCString::createWithFormat("%s\n---\n%s",
                tr("menu_layout.help_title").c_str(), tr("menu_layout.help_body").c_str()));
        }
        btn->setScale(fit);
        btn->m_baseScale = fit;
        btn->setPosition({ cursorX, centerY });
        m_dockMenu->addChild(btn);
        if (def.outPtr) *def.outPtr = btn;
        cursorX += (kToolW + kGap) * fit;
    }

    auto* tabFace = CCNode::create();
    tabFace->setContentSize({ 34.f, 13.f });
    if (auto* pill = paimon::ui::makeInset({ 34.f, 13.f }, 185)) {
        tabFace->addChild(pill, 0);
    }
    m_collapseArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (m_collapseArrow) {
        m_collapseArrow->setScale(0.42f);
        m_collapseArrow->setRotation(-90.f);
        m_collapseArrow->setPosition({ 17.f, 6.5f });
        tabFace->addChild(m_collapseArrow, 1);
    }
    m_collapseBtn = CCMenuItemSpriteExtra::create(tabFace, this, menu_selector(MainMenuLayoutEditor::onToggleDock));
    m_collapseBtn->m_baseScale = 1.f;
    m_collapseBtn->setPosition({ winSize.width * 0.5f, kDockMarginB + kDockH + 6.f });

    auto* collapseMenu = CCMenu::create();
    collapseMenu->setPosition({ 0.f, 0.f });
    collapseMenu->addChild(m_collapseBtn);
    m_hudRoot->addChild(collapseMenu, 3);
}

void MainMenuLayoutEditor::buildShapeBar() {
    auto winSize = CCDirector::get()->getWinSize();
    constexpr float W = 196.f;
    constexpr float H = 34.f;

    m_shapeBar = CCNode::create();
    m_shapeBar->setContentSize({ W, H });
    m_shapeBar->setAnchorPoint({ 0.5f, 0.f });
    m_shapeBar->setPosition({ winSize.width * 0.5f, kDockMarginB + kDockH + 16.f });
    m_shapeBar->setVisible(false);
    m_hudRoot->addChild(m_shapeBar, 4);

    m_shapeBar->addChild(studio_ui::makeGdWindow({ W, H }, paimon::ui::Bg::Brown, 95), 0);

    m_shapeBarMenu = CCMenu::create();
    m_shapeBarMenu->setPosition({ 0.f, 0.f });
    m_shapeBar->addChild(m_shapeBarMenu, 3);

    auto addKindBtn = [&](char const* text, float x, DrawShapeKind kind) {
        auto* btn = studio_ui::makeGdButton(text, 56.f, 20.f, paimon::ui::Btn::Blue, [this, kind] {
            this->addDrawShape(kind);
            this->setShapeBarOpen(false);
        });
        btn->setPosition({ x, H * 0.5f });
        m_shapeBarMenu->addChild(btn);
    };
    addKindBtn(tr("menu_layout.draw_add_rect").c_str(), 36.f, DrawShapeKind::Rectangle);
    addKindBtn(tr("menu_layout.draw_add_round").c_str(), 98.f, DrawShapeKind::RoundedRect);
    addKindBtn(tr("menu_layout.draw_add_circle").c_str(), 160.f, DrawShapeKind::Circle);
}

void MainMenuLayoutEditor::buildLayersPanel() {
    auto winSize = CCDirector::get()->getWinSize();
    float const topY = winSize.height - kTopBarH - 6.f;
    float const botY = kDockMarginB + kDockH + 10.f;
    float const panelH = std::max(150.f, topY - botY);

    m_layersPanel = CCNode::create();
    m_layersPanel->setContentSize({ kLayersW, panelH });
    m_layersPanel->setAnchorPoint({ 0.f, 0.f });
    m_layersPanel->setPosition({ kCardMargin, botY });
    m_layersPanel->setVisible(false);
    m_hudRoot->addChild(m_layersPanel, 2);

    m_layersPanel->addChild(studio_ui::makeGdWindow({ kLayersW, panelH }, paimon::ui::Bg::Brown, 80), 0);

    m_layersMenu = CCMenu::create();
    m_layersMenu->setPosition({ 0.f, 0.f });
    m_layersPanel->addChild(m_layersMenu, 4);

    m_layersCountLbl = CCLabelBMFont::create(tr("menu_layout.layers_title").c_str(), "goldFont.fnt");
    m_layersCountLbl->setScale(0.42f);
    m_layersCountLbl->setAnchorPoint({ 0.f, 0.5f });
    m_layersCountLbl->setPosition({ 10.f, panelH - 14.f });
    m_layersPanel->addChild(m_layersCountLbl, 3);

    auto* closeBtn = paimon::ui::makeFrameButton("GJ_closeBtn_001.png", 0.38f, [this] {
        this->setLayersOpen(false);
    });
    closeBtn->setPosition({ kLayersW - 14.f, panelH - 14.f });
    m_layersMenu->addChild(closeBtn);

    if (auto* div = paimon::ui::makeDivider(kLayersW - 16.f, paimon::ui::palette::gold, 120)) {
        div->setPosition({ kLayersW * 0.5f, panelH - 25.f });
        m_layersPanel->addChild(div, 2);
    }

    struct FilterDef { LayerFilter f; char const* en; char const* es; };
    static constexpr FilterDef kFilters[] = {
        { LayerFilter::All, "All", "Todo" },
        { LayerFilter::Buttons, "Btn", "Btn" },
        { LayerFilter::Art, "Art", "Art" },
        { LayerFilter::Shapes, "Draw", "Draw" },
        { LayerFilter::Hidden, "Hid", "Ocult" },
    };
    float fx = 24.f;
    float const fy = panelH - 36.f;
    for (auto const& fd : kFilters) {
        auto* btn = studio_ui::makeGdButton(loc(fd.en, fd.es), 30.f, 16.f,
            fd.f == m_layerFilter ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray,
            [this, f = fd.f] { this->setLayerFilter(f); });
        btn->setPosition({ fx, fy });
        m_layersMenu->addChild(btn);
        m_layerFilterBtns.emplace_back(fd.f, btn);
        fx += 32.f;
    }

    float const scrollW = kLayersW - 16.f;
    float const scrollH = panelH - 56.f;
    auto* listTray = paimon::ui::makeInset({ scrollW + 2.f, scrollH + 2.f }, 115);
    listTray->setPosition({ 7.f, 7.f });
    m_layersPanel->addChild(listTray, 1);

    auto* stencil = paimon::SpriteHelper::createRectStencil(scrollW, scrollH);
    auto* clipper = CCClippingNode::create(stencil);
    clipper->setContentSize({ scrollW, scrollH });
    clipper->setAnchorPoint({ 0.f, 0.f });
    clipper->setPosition({ 8.f, 8.f });
    m_layersPanel->addChild(clipper, 3);

    m_layersScroll = ScrollLayer::create({ scrollW, scrollH });
    m_layersScroll->setAnchorPoint({ 0.f, 0.f });
    m_layersScroll->setPosition({ 0.f, 0.f });
    clipper->addChild(m_layersScroll, 1);
}

void MainMenuLayoutEditor::buildInspector() {
    constexpr float W = kInspW;
    constexpr float H = kInspH;
    constexpr float P = 10.f;

    m_inspector = CCNode::create();
    m_inspector->setContentSize({ W, H });
    m_inspector->setAnchorPoint({ 1.f, 0.5f });
    m_inspector->setVisible(false);
    m_hudRoot->addChild(m_inspector, 2);

    m_inspector->addChild(studio_ui::makeGdWindow({ W, H }, paimon::ui::Bg::Brown, 88), 0);

    m_inspMenu = CCMenu::create();
    m_inspMenu->setPosition({ 0.f, 0.f });
    m_inspector->addChild(m_inspMenu, 4);

    m_inspName = CCLabelBMFont::create("", "goldFont.fnt");
    m_inspName->setScale(0.42f);
    m_inspName->setAnchorPoint({ 0.f, 0.5f });
    m_inspName->setPosition({ P, H - 14.f });
    m_inspector->addChild(m_inspName, 3);

    m_inspSub = CCLabelBMFont::create("", "chatFont.fnt");
    m_inspSub->setScale(0.40f);
    m_inspSub->setColor(studio_ui::kTextMuted);
    m_inspSub->setAnchorPoint({ 0.f, 0.5f });
    m_inspSub->setPosition({ P, H - 27.f });
    m_inspector->addChild(m_inspSub, 3);

    auto* resetSelBtn = studio_ui::makeGdButton(
        tr("menu_layout.action_reset"), 46.f, 16.f, paimon::ui::Btn::Red,
        [this] {
            if (m_closing) return;
            for (auto* elem : this->selectedElements()) {
                this->resetElementToDefault(*elem);
            }
            this->pushHistory();
            this->refreshInspector();
            this->rebuildLayersList();
            this->redraw();
        }, "goldFont.fnt");
    resetSelBtn->setPosition({ W - 53.f, H - 15.f });
    m_inspMenu->addChild(resetSelBtn);

    auto* closeBtn = paimon::ui::makeFrameButton("GJ_closeBtn_001.png", 0.38f, [this] {
        this->onDeselect(nullptr);
    });
    closeBtn->setPosition({ W - 15.f, H - 15.f });
    m_inspMenu->addChild(closeBtn);

    if (auto* div = paimon::ui::makeDivider(W - 18.f, paimon::ui::palette::gold, 120)) {
        div->setPosition({ W * 0.5f, H - 35.f });
        m_inspector->addChild(div, 2);
    }

    m_inspSingleRoot = CCNode::create();
    m_inspSingleRoot->setContentSize({ W, H - 36.f });
    m_inspector->addChild(m_inspSingleRoot, 3);

    auto* singleTabMenu = CCMenu::create();
    singleTabMenu->setPosition({ 0.f, 0.f });
    m_inspSingleRoot->addChild(singleTabMenu, 4);

    char const* tabNames[3] = {
        loc("Move", "Mover"),
        loc("Style", "Estilo"),
        loc("Group", "Grupo"),
    };
    for (int i = 0; i < 3; ++i) {
        m_tabBtns[i] = studio_ui::makeGdButton(tabNames[i], 58.f, 18.f,
            i == 0 ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray,
            [this, t = static_cast<InspectorTab>(i)] { this->setInspectorTab(t); });
        m_tabBtns[i]->setPosition({ 38.f + i * 64.f, 182.f });
        singleTabMenu->addChild(m_tabBtns[i]);
    }
    if (auto* div2 = paimon::ui::makeDivider(W - 18.f, paimon::ui::palette::gold, 95)) {
        div2->setPosition({ W * 0.5f, 170.f });
        m_inspSingleRoot->addChild(div2, 2);
    }

    auto makeGdStepper = [&](CCNode* parent, CCMenu* menu, std::string const& label,
                             float x, float y, float labelW, float valW,
                             char const* minusFrame, char const* plusFrame,
                             CCLabelBMFont*& outLbl, CCLabelBMFont*& outVal,
                             std::function<void(int)> onStep, std::function<void()> onEditVal) {
        outLbl = CCLabelBMFont::create(label.c_str(), "chatFont.fnt");
        outLbl->setScale(0.44f);
        outLbl->setColor(studio_ui::kTextMuted);
        outLbl->setAnchorPoint({ 0.f, 0.5f });
        outLbl->setPosition({ x, y });
        outLbl->limitLabelWidth(labelW, 0.44f, 0.16f);
        parent->addChild(outLbl, 2);

        float const minusX = x + labelW + 9.f;
        float const valX = minusX + 10.f + valW * 0.5f;
        float const plusX = valX + valW * 0.5f + 10.f;

        auto* minusBtn = paimon::ui::makeFrameButton(minusFrame, 0.34f, [onStep] { onStep(-1); });
        minusBtn->setPosition({ minusX, y });
        menu->addChild(minusBtn);

        auto* valFace = CCNode::create();
        valFace->setContentSize({ valW, 17.f });
        if (auto* vbg = paimon::ui::makeInset({ valW, 17.f }, 135)) {
            valFace->addChild(vbg, 0);
        }
        outVal = CCLabelBMFont::create("0", "bigFont.fnt");
        outVal->setScale(0.28f);
        outVal->setPosition({ valW * 0.5f, 8.5f });
        valFace->addChild(outVal, 1);

        auto* valBtn = CCMenuItemExt::createSpriteExtra(valFace, [cb = std::move(onEditVal)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        });
        valBtn->m_baseScale = 1.f;
        valBtn->setPosition({ valX, y });
        menu->addChild(valBtn);

        auto* plusBtn = paimon::ui::makeFrameButton(plusFrame, 0.34f, [onStep] { onStep(1); });
        plusBtn->setPosition({ plusX, y });
        menu->addChild(plusBtn);
    };

    // --- Tab 0: Transform ---
    m_tabTransformRoot = CCNode::create();
    m_tabTransformRoot->setContentSize({ W, 168.f });
    m_inspSingleRoot->addChild(m_tabTransformRoot, 2);

    auto* trMenu = CCMenu::create();
    trMenu->setPosition({ 0.f, 0.f });
    m_tabTransformRoot->addChild(trMenu, 4);

    CCLabelBMFont* dummyLbl = nullptr;
    auto bigStep = [] {
        auto* kd = CCKeyboardDispatcher::get();
        return kd && kd->getShiftKeyPressed();
    };

    makeGdStepper(m_tabTransformRoot, trMenu, "X", P, 154.f, 12.f, 36.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", dummyLbl, m_valX,
        [this, bigStep](int s) {
            if (m_closing || !this->primaryElement() || this->isLocked(*this->primaryElement())) return;
            this->nudgeSelection({ s * (bigStep() ? 10.f : 1.f), 0.f });
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || this->isLocked(*el) || !el->target.node) return;
            auto w = worldPos(el->target.node);
            studio_ui::openNumericInputPopup("X", w.x, -1000.f, 2000.f, false, [this](float val) {
                auto* cur = this->primaryElement();
                if (!cur || !cur->target.node) return;
                auto cw = worldPos(cur->target.node);
                this->nudgeSelection({ val - cw.x, 0.f });
                this->pushHistory(); this->refreshInspector(); this->redraw();
            });
        });

    makeGdStepper(m_tabTransformRoot, trMenu, "Y", P + 94.f, 154.f, 12.f, 36.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", dummyLbl, m_valY,
        [this, bigStep](int s) {
            if (m_closing || !this->primaryElement() || this->isLocked(*this->primaryElement())) return;
            this->nudgeSelection({ 0.f, s * (bigStep() ? 10.f : 1.f) });
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || this->isLocked(*el) || !el->target.node) return;
            auto w = worldPos(el->target.node);
            studio_ui::openNumericInputPopup("Y", w.y, -1000.f, 2000.f, false, [this](float val) {
                auto* cur = this->primaryElement();
                if (!cur || !cur->target.node) return;
                auto cw = worldPos(cur->target.node);
                this->nudgeSelection({ 0.f, val - cw.y });
                this->pushHistory(); this->refreshInspector(); this->redraw();
            });
        });

    makeGdStepper(m_tabTransformRoot, trMenu, loc("Scale", "Escala"), P, 130.f, 38.f, 44.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", m_lblScaleA, m_valScaleA,
        [this, bigStep](int s) {
            auto* el = this->primaryElement();
            if (m_closing || !el || this->isLocked(*el)) return;
            if (el->isShape) {
                auto& sh = m_liveShapes[el->id];
                sh.width = std::clamp(sh.width + s * (bigStep() ? 16.f : 4.f), kMinShapeSize, kMaxShapeSize);
                this->applyElementToScene(*el);
            } else {
                float factor = s > 0 ? (bigStep() ? 1.15f : 1.05f) : (bigStep() ? 1.f / 1.15f : 1.f / 1.05f);
                auto& b = m_liveButtons[el->id];
                if (m_uniformScale) this->scaleSelection(factor);
                else this->setSelectionScale(b.scaleX * factor, b.scaleY);
            }
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || this->isLocked(*el)) return;
            if (el->isShape) {
                auto& sh = m_liveShapes[el->id];
                studio_ui::openNumericInputPopup(tr("menu_layout.draw_shape_width"), sh.width, kMinShapeSize, kMaxShapeSize, false,
                    [this](float v) {
                        auto* cur = this->primaryElement();
                        if (!cur || !cur->isShape) return;
                        m_liveShapes[cur->id].width = v;
                        this->applyElementToScene(*cur);
                        this->pushHistory(); this->refreshInspector(); this->redraw();
                    });
            } else {
                auto& b = m_liveButtons[el->id];
                studio_ui::openNumericInputPopup(tr("menu_layout.field_scale"), b.scaleX * 100.f, kMinScale * 100.f, kMaxScale * 100.f, false,
                    [this](float v) {
                        auto* cur = this->primaryElement();
                        if (!cur || cur->isShape) return;
                        float s = v / 100.f;
                        if (m_uniformScale) this->setSelectionScale(s, s);
                        else this->setSelectionScale(s, m_liveButtons[cur->id].scaleY);
                        this->pushHistory(); this->refreshInspector(); this->redraw();
                    });
            }
        });

    m_aspectLockBtn = studio_ui::makeGdButton(loc("Lock", "Fijo"), 38.f, 16.f, paimon::ui::Btn::Green, [this] {
        m_uniformScale = !m_uniformScale;
        if (m_uniformScale) {
            if (auto* el = this->primaryElement(); el && !el->isShape) {
                auto& b = m_liveButtons[el->id];
                this->setSelectionScale(b.scaleX, b.scaleX);
                this->pushHistory();
            }
        }
        this->refreshInspector();
        this->redraw();
    });
    m_aspectLockBtn->setPosition({ W - 29.f, 130.f });
    trMenu->addChild(m_aspectLockBtn);

    m_rowScaleB = CCNode::create();
    m_rowScaleB->setContentSize({ W, 22.f });
    m_rowScaleB->setPosition({ 0.f, 95.f });
    m_tabTransformRoot->addChild(m_rowScaleB, 3);

    auto* rowBMenu = CCMenu::create();
    rowBMenu->setPosition({ 0.f, 0.f });
    m_rowScaleB->addChild(rowBMenu, 4);

    makeGdStepper(m_rowScaleB, rowBMenu, loc("Scale Y", "Esc Y"), P, 11.f, 38.f, 44.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", m_lblScaleB, m_valScaleB,
        [this, bigStep](int s) {
            auto* el = this->primaryElement();
            if (m_closing || !el || this->isLocked(*el)) return;
            if (el->isShape) {
                auto& sh = m_liveShapes[el->id];
                sh.height = std::clamp(sh.height + s * (bigStep() ? 16.f : 4.f), kMinShapeSize, kMaxShapeSize);
                this->applyElementToScene(*el);
            } else {
                float factor = s > 0 ? (bigStep() ? 1.15f : 1.05f) : (bigStep() ? 1.f / 1.15f : 1.f / 1.05f);
                auto& b = m_liveButtons[el->id];
                this->setSelectionScale(b.scaleX, b.scaleY * factor);
            }
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || this->isLocked(*el)) return;
            if (el->isShape) {
                auto& sh = m_liveShapes[el->id];
                studio_ui::openNumericInputPopup(tr("menu_layout.draw_shape_height"), sh.height, kMinShapeSize, kMaxShapeSize, false,
                    [this](float v) {
                        auto* cur = this->primaryElement();
                        if (!cur || !cur->isShape) return;
                        m_liveShapes[cur->id].height = v;
                        this->applyElementToScene(*cur);
                        this->pushHistory(); this->refreshInspector(); this->redraw();
                    });
            } else {
                auto& b = m_liveButtons[el->id];
                studio_ui::openNumericInputPopup("Scale Y %", b.scaleY * 100.f, kMinScale * 100.f, kMaxScale * 100.f, false,
                    [this](float v) {
                        auto* cur = this->primaryElement();
                        if (!cur || cur->isShape) return;
                        this->setSelectionScale(m_liveButtons[cur->id].scaleX, v / 100.f);
                        this->pushHistory(); this->refreshInspector(); this->redraw();
                    });
            }
        });

    auto* reset11Btn = studio_ui::makeGdButton("1:1", 34.f, 16.f, paimon::ui::Btn::Cyan, [this] {
        auto* el = this->primaryElement();
        if (m_closing || !el || this->isLocked(*el)) return;
        if (el->isShape) {
            auto& sh = m_liveShapes[el->id];
            float m = std::max(sh.width, sh.height);
            sh.width = m;
            sh.height = m;
            this->applyElementToScene(*el);
        } else {
            this->setSelectionScale(1.f, 1.f);
        }
        this->pushHistory(); this->refreshInspector(); this->redraw();
    });
    reset11Btn->setPosition({ W - 27.f, 11.f });
    rowBMenu->addChild(reset11Btn);

    makeGdStepper(m_tabTransformRoot, trMenu, tr("menu_layout.field_rotation"), P, 82.f, 38.f, 44.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", dummyLbl, m_valRot,
        [this, bigStep](int s) {
            auto* el = this->primaryElement();
            if (m_closing || !el || this->isLocked(*el)) return;
            this->rotateSelection(s * (bigStep() ? 15.f : 5.f));
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || this->isLocked(*el)) return;
            studio_ui::openNumericInputPopup(tr("menu_layout.field_rotation"), this->getRotation(*el), -360.f, 360.f, false,
                [this](float v) {
                    auto* cur = this->primaryElement();
                    if (!cur) return;
                    if (cur->isShape) m_liveShapes[cur->id].rotation = v;
                    else m_liveButtons[cur->id].rotation = v;
                    this->applyElementToScene(*cur);
                    this->pushHistory(); this->refreshInspector(); this->redraw();
                });
        });

    auto* zeroRotBtn = studio_ui::makeGdButton("0", 34.f, 16.f, paimon::ui::Btn::Cyan, [this] {
        auto* el = this->primaryElement();
        if (m_closing || !el || this->isLocked(*el)) return;
        if (el->isShape) m_liveShapes[el->id].rotation = 0.f;
        else m_liveButtons[el->id].rotation = 0.f;
        this->applyElementToScene(*el);
        this->pushHistory(); this->refreshInspector(); this->redraw();
    });
    zeroRotBtn->setPosition({ W - 27.f, 82.f });
    trMenu->addChild(zeroRotBtn);

    makeGdStepper(m_tabTransformRoot, trMenu, tr("menu_layout.field_layer"), P, 58.f, 38.f, 44.f,
        "edit_downBtn_001.png", "edit_upBtn_001.png", dummyLbl, m_valLayer,
        [this, bigStep](int s) {
            if (m_closing || !this->primaryElement()) return;
            this->setSelectionLayer(s * (bigStep() ? 5 : 1));
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el) return;
            studio_ui::openNumericInputPopup(tr("menu_layout.field_layer"), static_cast<float>(this->getLayerZ(*el)), -100.f, 100.f, true,
                [this](float v) {
                    auto* cur = this->primaryElement();
                    if (!cur) return;
                    int z = static_cast<int>(std::round(v));
                    if (cur->isShape) m_liveShapes[cur->id].layer = z;
                    else m_liveButtons[cur->id].layer = z;
                    this->applyElementToScene(*cur);
                    this->pushHistory(); this->refreshInspector(); this->redraw();
                });
        });

    auto* backBtn = studio_ui::makeGdButton("-10", 24.f, 16.f, paimon::ui::Btn::Gray, [this] {
        if (m_closing || !this->primaryElement()) return;
        this->setSelectionLayer(-10);
        this->pushHistory(); this->refreshInspector(); this->redraw();
    });
    backBtn->setPosition({ W - 44.f, 58.f });
    trMenu->addChild(backBtn);

    auto* frontBtn = studio_ui::makeGdButton("+10", 24.f, 16.f, paimon::ui::Btn::Gray, [this] {
        if (m_closing || !this->primaryElement()) return;
        this->setSelectionLayer(10);
        this->pushHistory(); this->refreshInspector(); this->redraw();
    });
    frontBtn->setPosition({ W - 18.f, 58.f });
    trMenu->addChild(frontBtn);

    auto* cxBtn = studio_ui::makeGdButton(tr("menu_layout.action_center_x"), 58.f, 20.f, paimon::ui::Btn::Blue,
        [this] { if (!m_closing) this->centerSelection(true, false); }, "goldFont.fnt");
    cxBtn->setPosition({ 38.f, 24.f });
    trMenu->addChild(cxBtn);

    auto* cyBtn = studio_ui::makeGdButton(tr("menu_layout.action_center_y"), 58.f, 20.f, paimon::ui::Btn::Blue,
        [this] { if (!m_closing) this->centerSelection(false, true); }, "goldFont.fnt");
    cyBtn->setPosition({ 102.f, 24.f });
    trMenu->addChild(cyBtn);

    auto* cBothBtn = studio_ui::makeGdButton(loc("Center", "Centro"), 58.f, 20.f, paimon::ui::Btn::Cyan,
        [this] { if (!m_closing) this->centerSelection(true, true); }, "goldFont.fnt");
    cBothBtn->setPosition({ 166.f, 24.f });
    trMenu->addChild(cBothBtn);

    // --- Tab 1: Style ---
    m_tabStyleRoot = CCNode::create();
    m_tabStyleRoot->setContentSize({ W, 168.f });
    m_tabStyleRoot->setVisible(false);
    m_inspSingleRoot->addChild(m_tabStyleRoot, 2);

    auto* stMenu = CCMenu::create();
    stMenu->setPosition({ 0.f, 0.f });
    m_tabStyleRoot->addChild(stMenu, 4);

    auto* opTitle = CCLabelBMFont::create(tr("menu_layout.field_opacity").c_str(), "chatFont.fnt");
    opTitle->setScale(0.46f);
    opTitle->setColor(studio_ui::kTextMuted);
    opTitle->setAnchorPoint({ 0.f, 0.5f });
    opTitle->setPosition({ P, 154.f });
    m_tabStyleRoot->addChild(opTitle, 2);

    auto* opFace = CCNode::create();
    opFace->setContentSize({ 46.f, 17.f });
    if (auto* obg = paimon::ui::makeInset({ 46.f, 17.f }, 135)) {
        opFace->addChild(obg, 0);
    }
    m_valOpacity = CCLabelBMFont::create("100%", "bigFont.fnt");
    m_valOpacity->setScale(0.28f);
    m_valOpacity->setPosition({ 23.f, 8.5f });
    opFace->addChild(m_valOpacity, 1);
    auto* opValBtn = CCMenuItemExt::createSpriteExtra(opFace, [this](CCMenuItemSpriteExtra*) {
        auto* el = this->primaryElement();
        if (!el || this->isLocked(*el)) return;
        studio_ui::openNumericInputPopup(tr("menu_layout.field_opacity"), this->getOpacity(*el) * 100.f, 0.f, 100.f, false,
            [this](float v) {
                this->setSelectionOpacity(v / 100.f);
                this->pushHistory();
                this->refreshInspector();
                this->redraw();
            });
    });
    opValBtn->m_baseScale = 1.f;
    opValBtn->setPosition({ W - 33.f, 154.f });
    stMenu->addChild(opValBtn);

    m_opacitySlider = Slider::create(this, menu_selector(MainMenuLayoutEditor::onOpacityChanged), 1.f);
    if (m_opacitySlider) {
        float const track = m_opacitySlider->m_width > 1.f ? m_opacitySlider->m_width : 170.f;
        m_opacitySlider->setScale((W - 2.f * P - 12.f) / track);
        m_opacitySlider->setValue(1.f);
        m_opacitySlider->setPosition({ W * 0.5f, 136.f });
        m_tabStyleRoot->addChild(m_opacitySlider, 3);
    }

    auto* colLbl = CCLabelBMFont::create(tr("menu_layout.draw_shape_color").c_str(), "chatFont.fnt");
    colLbl->setScale(0.46f);
    colLbl->setColor(studio_ui::kTextMuted);
    colLbl->setAnchorPoint({ 0.f, 0.5f });
    colLbl->setPosition({ P, 112.f });
    m_tabStyleRoot->addChild(colLbl, 2);

    auto* swNode = CCNode::create();
    swNode->setContentSize({ 34.f, 16.f });
    auto* swBorder = paimon::ui::makeInset({ 34.f, 16.f }, 220, { 255, 255, 255 });
    swNode->addChild(swBorder, 0);
    m_colorSwatch = paimon::ui::makeInset({ 30.f, 12.f }, 255, { 255, 255, 255 });
    if (m_colorSwatch) {
        m_colorSwatch->setPosition({ 2.f, 2.f });
        swNode->addChild(m_colorSwatch, 1);
    }
    auto* swBtn = CCMenuItemExt::createSpriteExtra(swNode, [this](CCMenuItemSpriteExtra*) {
        this->pickSelectedColor(false);
    });
    swBtn->m_baseScale = 1.f;
    swBtn->setPosition({ 76.f, 112.f });
    stMenu->addChild(swBtn);

    auto* pickColBtn = studio_ui::makeGdButton(tr("menu_layout.draw_shape_pick_color"), 44.f, 17.f,
        paimon::ui::Btn::Blue, [this] { this->pickSelectedColor(false); });
    pickColBtn->setPosition({ 122.f, 112.f });
    stMenu->addChild(pickColBtn);

    auto* resetColBtn = studio_ui::makeGdButton(tr("menu_layout.action_reset"), 44.f, 17.f,
        paimon::ui::Btn::Red, [this] { this->resetSelectedColor(); });
    resetColBtn->setPosition({ 170.f, 112.f });
    stMenu->addChild(resetColBtn);

    m_fontRow = CCNode::create();
    m_fontRow->setContentSize({ W, 24.f });
    m_fontRow->setPosition({ 0.f, 74.f });
    m_tabStyleRoot->addChild(m_fontRow, 3);

    auto* fontMenu = CCMenu::create();
    fontMenu->setPosition({ 0.f, 0.f });
    m_fontRow->addChild(fontMenu, 4);

    auto* fontLbl = CCLabelBMFont::create(tr("menu_layout.context_font").c_str(), "chatFont.fnt");
    fontLbl->setScale(0.46f);
    fontLbl->setColor(studio_ui::kTextMuted);
    fontLbl->setAnchorPoint({ 0.f, 0.5f });
    fontLbl->setPosition({ P, 12.f });
    m_fontRow->addChild(fontLbl, 2);

    m_valFont = CCLabelBMFont::create("Normal", "chatFont.fnt");
    m_valFont->setScale(0.44f);
    m_valFont->setColor(paimon::ui::palette::gold);
    m_valFont->setAnchorPoint({ 0.f, 0.5f });
    m_valFont->setPosition({ 44.f, 12.f });
    m_fontRow->addChild(m_valFont, 2);

    auto* pickFontBtn = studio_ui::makeGdButton(tr("menu_layout.context_pick_font"), 44.f, 17.f,
        paimon::ui::Btn::Blue, [this] { this->pickSelectedFont(); });
    pickFontBtn->setPosition({ 122.f, 12.f });
    fontMenu->addChild(pickFontBtn);

    auto* resetFontBtn = studio_ui::makeGdButton(tr("menu_layout.action_reset"), 44.f, 17.f,
        paimon::ui::Btn::Red, [this] { this->resetSelectedFont(); });
    resetFontBtn->setPosition({ 170.f, 12.f });
    fontMenu->addChild(resetFontBtn);

    m_shapeStyleRoot = CCNode::create();
    m_shapeStyleRoot->setContentSize({ W, 68.f });
    m_shapeStyleRoot->setPosition({ 0.f, 32.f });
    m_tabStyleRoot->addChild(m_shapeStyleRoot, 3);

    auto* shMenu = CCMenu::create();
    shMenu->setPosition({ 0.f, 0.f });
    m_shapeStyleRoot->addChild(shMenu, 4);

    struct KindOpt { DrawShapeKind k; char const* key; };
    static constexpr KindOpt kKinds[3] = {
        { DrawShapeKind::Rectangle, "menu_layout.draw_add_rect" },
        { DrawShapeKind::RoundedRect, "menu_layout.draw_add_round" },
        { DrawShapeKind::Circle, "menu_layout.draw_add_circle" },
    };
    for (int i = 0; i < 3; ++i) {
        m_shapeKindBtns[i] = studio_ui::makeGdButton(tr(kKinds[i].key), 58.f, 17.f, paimon::ui::Btn::Gray,
            [this, k = kKinds[i].k] {
                auto* el = this->primaryElement();
                if (!el || !el->isShape || this->isLocked(*el)) return;
                m_liveShapes[el->id].kind = k;
                this->applyElementToScene(*el);
                this->pushHistory(); this->refreshInspector(); this->rebuildLayersList(); this->redraw();
            });
        m_shapeKindBtns[i]->setPosition({ 38.f + i * 64.f, 56.f });
        shMenu->addChild(m_shapeKindBtns[i]);
    }

    makeGdStepper(m_shapeStyleRoot, shMenu, loc("Rad", "Rad"), P, 33.f, 22.f, 28.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", dummyLbl, m_valRadius,
        [this, bigStep](int s) {
            auto* el = this->primaryElement();
            if (!el || !el->isShape || this->isLocked(*el)) return;
            auto& sh = m_liveShapes[el->id];
            sh.cornerRadius = std::clamp(sh.cornerRadius + s * (bigStep() ? 8.f : 2.f), 0.f, 200.f);
            this->applyElementToScene(*el);
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || !el->isShape || this->isLocked(*el)) return;
            studio_ui::openNumericInputPopup(tr("menu_layout.draw_shape_radius"), m_liveShapes[el->id].cornerRadius, 0.f, 200.f, false,
                [this](float v) {
                    auto* cur = this->primaryElement();
                    if (!cur || !cur->isShape) return;
                    m_liveShapes[cur->id].cornerRadius = v;
                    this->applyElementToScene(*cur);
                    this->pushHistory(); this->refreshInspector(); this->redraw();
                });
        });

    makeGdStepper(m_shapeStyleRoot, shMenu, loc("Bdr", "Brd"), P + 94.f, 33.f, 22.f, 28.f,
        "edit_leftBtn_001.png", "edit_rightBtn_001.png", dummyLbl, m_valStroke,
        [this, bigStep](int s) {
            auto* el = this->primaryElement();
            if (!el || !el->isShape || this->isLocked(*el)) return;
            auto& sh = m_liveShapes[el->id];
            sh.strokeWidth = std::clamp(sh.strokeWidth + s * (bigStep() ? 2.f : 0.5f), 0.f, 24.f);
            this->applyElementToScene(*el);
            this->pushHistory(); this->refreshInspector(); this->redraw();
        },
        [this] {
            auto* el = this->primaryElement();
            if (!el || !el->isShape || this->isLocked(*el)) return;
            studio_ui::openNumericInputPopup(loc("Border Width", "Grosor Borde"), m_liveShapes[el->id].strokeWidth, 0.f, 24.f, false,
                [this](float v) {
                    auto* cur = this->primaryElement();
                    if (!cur || !cur->isShape) return;
                    m_liveShapes[cur->id].strokeWidth = v;
                    this->applyElementToScene(*cur);
                    this->pushHistory(); this->refreshInspector(); this->redraw();
                });
        });

    auto* bSwNode = CCNode::create();
    bSwNode->setContentSize({ 34.f, 16.f });
    bSwNode->addChild(paimon::ui::makeInset({ 34.f, 16.f }, 220, { 255, 255, 255 }), 0);
    m_strokeSwatch = paimon::ui::makeInset({ 30.f, 12.f }, 255, { 255, 255, 255 });
    if (m_strokeSwatch) {
        m_strokeSwatch->setPosition({ 2.f, 2.f });
        bSwNode->addChild(m_strokeSwatch, 1);
    }
    auto* bSwBtn = CCMenuItemExt::createSpriteExtra(bSwNode, [this](CCMenuItemSpriteExtra*) {
        this->pickSelectedColor(true);
    });
    bSwBtn->m_baseScale = 1.f;
    bSwBtn->setPosition({ 27.f, 10.f });
    shMenu->addChild(bSwBtn);

    auto* dupBtn = studio_ui::makeGdButton(loc("Duplicate", "Duplicar"), 64.f, 17.f,
        paimon::ui::Btn::Blue, [this] { this->duplicateSelectedShape(); });
    dupBtn->setPosition({ 94.f, 10.f });
    shMenu->addChild(dupBtn);

    auto* delBtn = studio_ui::makeGdButton(tr("menu_layout.delete_selected"), 56.f, 17.f,
        paimon::ui::Btn::Red, [this] { this->deleteSelectedShapes(); });
    delBtn->setPosition({ 164.f, 10.f });
    shMenu->addChild(delBtn);

    auto* copyStyleBtn = studio_ui::makeGdButton(loc("Copy Style", "Copiar Est."), 88.f, 19.f,
        paimon::ui::Btn::Blue, [this] { this->copySelectedStyle(); }, "goldFont.fnt");
    copyStyleBtn->setPosition({ 55.f, 14.f });
    stMenu->addChild(copyStyleBtn);

    m_pasteStyleBtn = studio_ui::makeGdButton(loc("Paste Style", "Pegar Est."), 88.f, 19.f,
        paimon::ui::Btn::Gray, [this] { this->pasteStyleToSelected(); }, "goldFont.fnt");
    m_pasteStyleBtn->setPosition({ 149.f, 14.f });
    stMenu->addChild(m_pasteStyleBtn);

    // --- Tab 2: Group ---
    m_tabGroupRoot = CCNode::create();
    m_tabGroupRoot->setContentSize({ W, 168.f });
    m_tabGroupRoot->setVisible(false);
    m_inspSingleRoot->addChild(m_tabGroupRoot, 2);

    auto* grMenu = CCMenu::create();
    grMenu->setPosition({ 0.f, 0.f });
    m_tabGroupRoot->addChild(grMenu, 4);

    m_hideBtn = studio_ui::makeGdButton(tr("menu_layout.action_hide"), 88.f, 22.f,
        paimon::ui::Btn::Gray, [this] { this->toggleSelectionHidden(); }, "goldFont.fnt");
    m_hideBtn->setPosition({ 55.f, 148.f });
    grMenu->addChild(m_hideBtn);

    m_lockBtn = studio_ui::makeGdButton(tr("menu_layout.action_lock"), 88.f, 22.f,
        paimon::ui::Btn::Gray, [this] { this->toggleSelectionLock(); }, "goldFont.fnt");
    m_lockBtn->setPosition({ 149.f, 148.f });
    grMenu->addChild(m_lockBtn);

    auto* grpTitle = CCLabelBMFont::create(loc("Link Group", "Grupo Link"), "chatFont.fnt");
    grpTitle->setScale(0.46f);
    grpTitle->setColor(studio_ui::kTextMuted);
    grpTitle->setAnchorPoint({ 0.f, 0.5f });
    grpTitle->setPosition({ P, 121.f });
    m_tabGroupRoot->addChild(grpTitle, 2);

    auto* selGrpBtn = studio_ui::makeGdButton(loc("Select Group", "Sel. Grupo"), 76.f, 17.f,
        paimon::ui::Btn::Cyan, [this] {
            if (auto* el = this->primaryElement()) {
                auto g = this->getLinkGroup(*el);
                if (!g.empty()) this->selectLinkGroup(g);
            }
        });
    selGrpBtn->setPosition({ W - 48.f, 121.f });
    grMenu->addChild(selGrpBtn);

    struct GroupOpt { char const* id; char const* label; };
    static constexpr GroupOpt kGroups[6] = {
        { "", "None" }, { "G1", "G1" }, { "G2", "G2" },
        { "G3", "G3" }, { "G4", "G4" }, { "G5", "G5" },
    };
    float gx = 10.f;
    for (int i = 0; i < 6; ++i) {
        float bw = (i == 0) ? 36.f : 26.f;
        auto* gBtn = studio_ui::makeGdButton(kGroups[i].label, bw, 18.f, paimon::ui::Btn::Gray,
            [this, gid = std::string(kGroups[i].id)] {
                for (auto* el : this->selectedElements()) {
                    if (el->isShape) m_liveShapes[el->id].linkGroup = gid;
                    else m_liveButtons[el->id].linkGroup = gid;
                }
                this->pushHistory();
                this->refreshInspector();
                this->rebuildLayersList();
                this->redraw();
            });
        gBtn->setPosition({ gx + bw * 0.5f, 96.f });
        grMenu->addChild(gBtn);
        m_groupBtns.emplace_back(kGroups[i].id, gBtn);
        gx += bw + 3.f;
    }

    auto* snapNowBtn = studio_ui::makeGdButton(loc("Snap Grid", "Ajustar Grid"), 88.f, 20.f,
        paimon::ui::Btn::Blue, [this] {
            for (auto* el : this->selectedElements()) {
                if (this->isLocked(*el) || !el->target.node || !el->target.node->getParent()) continue;
                auto w = worldPos(el->target.node);
                float gxPos = std::round(w.x / m_settings.gridStep) * m_settings.gridStep;
                float gyPos = std::round(w.y / m_settings.gridStep) * m_settings.gridStep;
                this->moveElementAndPeers(*el, { gxPos - w.x, gyPos - w.y }, false);
            }
            this->pushHistory(); this->refreshInspector(); this->redraw();
        });
    snapNowBtn->setPosition({ 55.f, 60.f });
    grMenu->addChild(snapNowBtn);

    auto* selAllBtn = studio_ui::makeGdButton(loc("Select All", "Sel. Todo"), 88.f, 20.f,
        paimon::ui::Btn::Blue, [this] { this->selectAllElements(); });
    selAllBtn->setPosition({ 149.f, 60.f });
    grMenu->addChild(selAllBtn);

    auto* resetElemBtn = studio_ui::makeGdButton(loc("Reset Element", "Restaurar Elemento"), 184.f, 22.f,
        paimon::ui::Btn::Red, [this] {
            if (auto* el = this->primaryElement()) {
                this->resetElementToDefault(*el);
                this->pushHistory(); this->refreshInspector(); this->rebuildLayersList(); this->redraw();
            }
        }, "goldFont.fnt");
    resetElemBtn->setPosition({ W * 0.5f, 25.f });
    grMenu->addChild(resetElemBtn);

    // --- Multi-Selection Root ---
    m_inspMultiRoot = CCNode::create();
    m_inspMultiRoot->setContentSize({ W, H - 36.f });
    m_inspMultiRoot->setVisible(false);
    m_inspector->addChild(m_inspMultiRoot, 3);

    auto* muMenu = CCMenu::create();
    muMenu->setPosition({ 0.f, 0.f });
    m_inspMultiRoot->addChild(muMenu, 4);

    auto* alignLbl = CCLabelBMFont::create(loc("Align", "Alinear"), "chatFont.fnt");
    alignLbl->setScale(0.46f);
    alignLbl->setColor(studio_ui::kTextMuted);
    alignLbl->setAnchorPoint({ 0.f, 0.5f });
    alignLbl->setPosition({ P, 178.f });
    m_inspMultiRoot->addChild(alignLbl, 2);

    m_alignTargetBtn = studio_ui::makeGdButton(loc("To Selection", "A Seleccion"), 80.f, 17.f,
        paimon::ui::Btn::Cyan, [this] {
            m_alignToCanvas = !m_alignToCanvas;
            this->refreshInspector();
        });
    m_alignTargetBtn->setPosition({ W - 50.f, 178.f });
    muMenu->addChild(m_alignTargetBtn);

    struct AlignDef { int mode; char const* en; char const* es; float x; float y; };
    static constexpr AlignDef kAligns[6] = {
        { 0, "Left", "Izq", 38.f, 154.f },
        { 1, "Center X", "Centro X", 102.f, 154.f },
        { 2, "Right", "Der", 166.f, 154.f },
        { 3, "Top", "Arriba", 38.f, 130.f },
        { 4, "Center Y", "Centro Y", 102.f, 130.f },
        { 5, "Bottom", "Abajo", 166.f, 130.f },
    };
    for (auto const& ad : kAligns) {
        auto* btn = studio_ui::makeGdButton(loc(ad.en, ad.es), 58.f, 19.f, paimon::ui::Btn::Blue,
            [this, m = ad.mode] { this->alignSelection(m); });
        btn->setPosition({ ad.x, ad.y });
        muMenu->addChild(btn);
    }

    m_distHBtn = studio_ui::makeGdButton(loc("Distrib H", "Distrib H"), 88.f, 19.f,
        paimon::ui::Btn::Cyan, [this] { this->distributeSelection(true); });
    m_distHBtn->setPosition({ 55.f, 102.f });
    muMenu->addChild(m_distHBtn);

    m_distVBtn = studio_ui::makeGdButton(loc("Distrib V", "Distrib V"), 88.f, 19.f,
        paimon::ui::Btn::Cyan, [this] { this->distributeSelection(false); });
    m_distVBtn->setPosition({ 149.f, 102.f });
    muMenu->addChild(m_distVBtn);

    auto* matchScaleBtn = studio_ui::makeGdButton(loc("Match Scale", "Igual Escala"), 88.f, 19.f,
        paimon::ui::Btn::Gray, [this] { this->matchSelectionScale(); });
    matchScaleBtn->setPosition({ 55.f, 76.f });
    muMenu->addChild(matchScaleBtn);

    auto* matchOpBtn = studio_ui::makeGdButton(loc("Match Opac.", "Igual Opac."), 88.f, 19.f,
        paimon::ui::Btn::Gray, [this] { this->matchSelectionOpacity(); });
    matchOpBtn->setPosition({ 149.f, 76.f });
    muMenu->addChild(matchOpBtn);

    auto* linkSelBtn = studio_ui::makeGdButton(loc("Link Group", "Unir Grupo"), 88.f, 19.f,
        paimon::ui::Btn::Green, [this] { this->linkSelectedTogether(); });
    linkSelBtn->setPosition({ 55.f, 50.f });
    muMenu->addChild(linkSelBtn);

    auto* unlinkSelBtn = studio_ui::makeGdButton(loc("Unlink All", "Desvincular"), 88.f, 19.f,
        paimon::ui::Btn::Red, [this] { this->unlinkSelected(); });
    unlinkSelBtn->setPosition({ 149.f, 50.f });
    muMenu->addChild(unlinkSelBtn);

    auto* hideAllBtn = studio_ui::makeGdButton(tr("menu_layout.action_hide"), 58.f, 19.f,
        paimon::ui::Btn::Gray, [this] {
            for (auto* el : this->selectedElements()) {
                if (el->isShape) m_liveShapes[el->id].hidden = true;
                else m_liveButtons[el->id].hidden = true;
                this->applyElementToScene(*el);
            }
            this->pushHistory(); this->rebuildLayersList(); this->redraw();
        });
    hideAllBtn->setPosition({ 38.f, 22.f });
    muMenu->addChild(hideAllBtn);

    auto* showAllBtn = studio_ui::makeGdButton(tr("menu_layout.action_show"), 58.f, 19.f,
        paimon::ui::Btn::Gray, [this] {
            for (auto* el : this->selectedElements()) {
                if (el->isShape) m_liveShapes[el->id].hidden = false;
                else m_liveButtons[el->id].hidden = false;
                this->applyElementToScene(*el);
            }
            this->pushHistory(); this->rebuildLayersList(); this->redraw();
        });
    showAllBtn->setPosition({ 102.f, 22.f });
    muMenu->addChild(showAllBtn);

    auto* lockAllBtn = studio_ui::makeGdButton(tr("menu_layout.action_lock"), 58.f, 19.f,
        paimon::ui::Btn::Gray, [this] { this->toggleSelectionLock(); });
    lockAllBtn->setPosition({ 166.f, 22.f });
    muMenu->addChild(lockAllBtn);

    this->placeInspector();
}

void MainMenuLayoutEditor::buildPreviewBadge() {
    auto winSize = CCDirector::get()->getWinSize();

    m_previewPill = CCNode::create();
    m_previewPill->setContentSize({ 104.f, 22.f });
    m_previewPill->setPosition({ winSize.width - 58.f, winSize.height - 18.f });
    m_previewPill->setVisible(false);
    this->addChild(m_previewPill, 50);

    m_previewPillMenu = CCMenu::create();
    m_previewPillMenu->setPosition({ 0.f, 0.f });
    m_previewPill->addChild(m_previewPillMenu, 1);

    auto* exitBtn = studio_ui::makeGdButton(loc("Exit Preview", "Salir Vista"), 100.f, 20.f,
        paimon::ui::Btn::Blue, [this] { this->setPreviewMode(false); }, "goldFont.fnt");
    exitBtn->setPosition({ 0.f, 0.f });
    m_previewPillMenu->addChild(exitBtn);
}

void MainMenuLayoutEditor::placeInspector() {
    if (!m_inspector) return;
    auto winSize = CCDirector::get()->getWinSize();
    float const bottom = m_collapsed ? 6.f : (kDockMarginB + kDockH + 8.f);
    float const top = winSize.height - kTopBarH - 6.f;

    bool onRight = true;
    if (!m_layersOpen && m_selectedIds.size() == 1) {
        if (auto* sel = this->primaryElement(); sel && sel->target.node && sel->target.node->getParent()) {
            onRight = worldPos(sel->target.node).x <= winSize.width * 0.5f;
        }
    }
    m_inspector->setAnchorPoint({ onRight ? 1.f : 0.f, 0.5f });
    m_inspector->setPosition({ onRight ? winSize.width - kCardMargin : kCardMargin, (bottom + top) * 0.5f });
}

void MainMenuLayoutEditor::captureInterfaceNodes(CCNode* node) {
    if (!node) return;
    if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(node)) {
        m_interfaceNodes.emplace_back(rgba, rgba->getOpacity());
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            this->captureInterfaceNodes(child);
        }
    }
}

void MainMenuLayoutEditor::applyInterfaceOpacity() {
    bool const draggingHudHidden = m_settings.autoHideHud && m_drag != DragMode::None && m_drag != DragMode::Marquee;
    if (m_dark) {
        float const darkAlpha = (m_previewMode || draggingHudHidden) ? 0.f : (38.f * m_interfaceOpacity);
        m_dark->setOpacity(static_cast<GLubyte>(darkAlpha));
    }
    for (auto const& [node, alpha] : m_interfaceNodes) {
        if (auto* rgba = node.lock().data()) {
            rgba->setOpacity(static_cast<GLubyte>(alpha * m_interfaceOpacity));
        }
    }
}

void MainMenuLayoutEditor::syncHudVisibility() {
    bool const draggingHide = m_settings.autoHideHud && m_drag != DragMode::None && m_drag != DragMode::Marquee;
    bool const showHud = !m_previewMode && !m_closing && !draggingHide;
    if (m_hudRoot) m_hudRoot->setVisible(showHud);
    if (m_layersPanel) m_layersPanel->setVisible(showHud && m_layersOpen);
    if (m_shapeBar) m_shapeBar->setVisible(showHud && m_shapeBarOpen);
    if (m_inspector) m_inspector->setVisible(showHud && !m_selectedIds.empty());

    bool const showCanvas = !m_previewMode;
    if (m_grid) m_grid->setVisible(showCanvas && (m_settings.gridOn || m_settings.safeZone));
    if (m_highlights) m_highlights->setVisible(showCanvas);
    if (m_outline) m_outline->setVisible(showCanvas);
    if (m_grip) m_grip->setVisible(showCanvas);
    if (m_previewPill) m_previewPill->setVisible(m_previewMode && !m_closing);
    this->applyInterfaceOpacity();
}

void MainMenuLayoutEditor::beginClose(bool saved) {
    m_closing = true;
    m_saved = saved;
    m_interfaceElapsed = 0.f;
    m_closeOpacity = m_interfaceOpacity;
    m_drag = DragMode::None;
    m_dragChanged = false;
    m_previewMode = false;
    if (m_guideX) m_guideX->setVisible(false);
    if (m_guideY) m_guideY->setVisible(false);
    if (m_liveBadge) m_liveBadge->setVisible(false);
    if (m_dockMenu) m_dockMenu->setEnabled(false);
    if (m_topMenu) m_topMenu->setEnabled(false);
    if (m_inspMenu) m_inspMenu->setEnabled(false);
    if (m_layersMenu) m_layersMenu->setEnabled(false);
    if (m_collapseBtn) m_collapseBtn->setEnabled(false);
    this->syncHudVisibility();
}

void MainMenuLayoutEditor::setPreviewMode(bool on) {
    m_previewMode = on;
    this->syncHudVisibility();
}

void MainMenuLayoutEditor::setLayersOpen(bool open) {
    m_layersOpen = open;
    if (open) this->rebuildLayersList();
    this->placeInspector();
    this->syncHudVisibility();
    this->refreshHudState();
}

void MainMenuLayoutEditor::setShapeBarOpen(bool open) {
    m_shapeBarOpen = open;
    this->syncHudVisibility();
    this->refreshHudState();
}

void MainMenuLayoutEditor::setCollapsed(bool collapsed) {
    m_collapsed = collapsed;
    auto winSize = CCDirector::get()->getWinSize();
    if (m_dock) {
        m_dock->stopAllActions();
        m_dock->runAction(CCEaseSineOut::create(CCMoveTo::create(
            0.20f, { winSize.width * 0.5f, m_collapsed ? -(kDockH + 12.f) : kDockMarginB })));
    }
    if (m_collapseBtn) {
        m_collapseBtn->stopAllActions();
        m_collapseBtn->runAction(CCEaseSineOut::create(CCMoveTo::create(
            0.20f, { winSize.width * 0.5f, m_collapsed ? 7.f : kDockMarginB + kDockH + 6.f })));
    }
    if (m_collapseArrow) {
        m_collapseArrow->stopAllActions();
        m_collapseArrow->runAction(CCEaseSineOut::create(CCRotateTo::create(0.20f, m_collapsed ? 90.f : -90.f)));
    }
    if (m_collapsed && m_shapeBarOpen) {
        this->setShapeBarOpen(false);
    }
    this->placeInspector();
}

void MainMenuLayoutEditor::setInspectorTab(InspectorTab tab) {
    m_inspTab = tab;
    if (m_tabTransformRoot) m_tabTransformRoot->setVisible(tab == InspectorTab::Transform);
    if (m_tabStyleRoot) m_tabStyleRoot->setVisible(tab == InspectorTab::Style);
    if (m_tabGroupRoot) m_tabGroupRoot->setVisible(tab == InspectorTab::Group);
    for (int i = 0; i < 3; ++i) {
        bool active = static_cast<int>(tab) == i;
        studio_ui::setGdButtonState(m_tabBtns[i], active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
}

void MainMenuLayoutEditor::setLayerFilter(LayerFilter filter) {
    m_layerFilter = filter;
    this->rebuildLayersList();
}

void MainMenuLayoutEditor::rebuildLayersList() {
    if (!m_layersScroll) return;
    auto* content = m_layersScroll->m_contentLayer;
    if (!content) return;
    content->removeAllChildrenWithCleanup(true);

    for (auto const& [f, btn] : m_layerFilterBtns) {
        bool active = f == m_layerFilter;
        studio_ui::setGdButtonState(btn, active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }

    std::vector<EditorElement const*> filtered;
    filtered.reserve(m_elements.size());
    for (auto const& elem : m_elements) {
        if (this->isBackgroundElement(elem)) continue;
        bool const hid = this->isHidden(elem);
        switch (m_layerFilter) {
            case LayerFilter::Buttons: if (elem.info.category != ElementCategory::Button) continue; break;
            case LayerFilter::Art:
                if (elem.info.category != ElementCategory::Label && elem.info.category != ElementCategory::Sprite) continue;
                break;
            case LayerFilter::Shapes: if (!elem.isShape) continue; break;
            case LayerFilter::Hidden: if (!hid) continue; break;
            case LayerFilter::All: default: break;
        }
        filtered.push_back(&elem);
    }

    std::stable_sort(filtered.begin(), filtered.end(), [this](EditorElement const* a, EditorElement const* b) {
        if (a->isShape != b->isShape) return a->isShape > b->isShape;
        int za = this->getLayerZ(*a), zb = this->getLayerZ(*b);
        if (za != zb) return za > zb;
        return a->info.displayName < b->info.displayName;
    });

    if (m_layersCountLbl) {
        m_layersCountLbl->setString(fmt::format("{} ({})", tr("menu_layout.layers_title"), filtered.size()).c_str());
        m_layersCountLbl->limitLabelWidth(kLayersW - 36.f, 0.42f, 0.16f);
    }

    float const viewW = m_layersScroll->getContentSize().width;
    float const viewH = m_layersScroll->getContentSize().height;
    constexpr float rowH = 22.f;
    float const totalH = std::max(viewH, static_cast<float>(filtered.size()) * rowH);
    content->setContentSize({ viewW, totalH });

    for (std::size_t i = 0; i < filtered.size(); ++i) {
        auto const* elem = filtered[i];
        std::string const elemId = elem->id;
        bool const selected = this->isSelected(elemId);
        bool const hidden = this->isHidden(*elem);
        bool const locked = this->isLocked(*elem);
        std::string const linkGrp = this->getLinkGroup(*elem);
        float const y = totalH - static_cast<float>(i + 1) * rowH;

        auto* row = CCNode::create();
        row->setContentSize({ viewW, rowH });
        row->setPosition({ 0.f, y });
        content->addChild(row);

        ccColor3B rowCol = selected ? ccColor3B{ 55, 130, 210 }
                                    : (i % 2 == 0 ? ccColor3B{ 20, 24, 34 } : ccColor3B{ 12, 16, 24 });
        if (auto* bg = paimon::ui::makeInset({ viewW - 4.f, rowH - 2.f }, selected ? 185 : 130, rowCol)) {
            bg->setPosition({ 2.f, 1.f });
            row->addChild(bg, 0);
        }

        if (auto* dot = CCLayerColor::create({
                studio_ui::categoryColor(elem->info.category).r,
                studio_ui::categoryColor(elem->info.category).g,
                studio_ui::categoryColor(elem->info.category).b,
                230
            }, 3.f, rowH - 6.f)) {
            dot->setPosition({ 5.f, 3.f });
            row->addChild(dot, 1);
        }

        auto* rowMenu = studio_ui::ClippedScrollMenu::create(m_layersScroll);
        rowMenu->setPosition({ 0.f, 0.f });
        rowMenu->setContentSize({ viewW, rowH });
        row->addChild(rowMenu, 3);

        float const hitW = viewW - 44.f;
        auto* hitFace = CCNode::create();
        hitFace->setContentSize({ hitW, rowH });

        auto* nameLbl = CCLabelBMFont::create(elem->info.displayName.c_str(), "chatFont.fnt");
        nameLbl->setScale(0.42f);
        nameLbl->setAnchorPoint({ 0.f, 0.5f });
        nameLbl->setPosition({ 11.f, rowH * 0.5f });
        nameLbl->setColor(hidden ? studio_ui::kTextMuted : (selected ? paimon::ui::palette::gold : studio_ui::kTextPrimary));
        nameLbl->limitLabelWidth(hitW - (linkGrp.empty() ? 14.f : 30.f), 0.42f, 0.16f);
        hitFace->addChild(nameLbl, 1);

        if (!linkGrp.empty()) {
            auto* gLbl = CCLabelBMFont::create(linkGrp.c_str(), "bigFont.fnt");
            gLbl->setScale(0.22f);
            gLbl->setColor(paimon::ui::palette::gold);
            gLbl->setAnchorPoint({ 1.f, 0.5f });
            gLbl->setPosition({ hitW - 2.f, rowH * 0.5f });
            hitFace->addChild(gLbl, 1);
        }

        auto* selBtn = CCMenuItemExt::createSpriteExtra(hitFace, [this, elemId](CCMenuItemSpriteExtra*) {
            if (m_closing) return;
            auto* kd = CCKeyboardDispatcher::get();
            bool addMode = m_multiSelectMode || (kd && kd->getShiftKeyPressed());
            if (addMode) this->toggleSelection(elemId);
            else this->selectSingle(elemId);
            this->rebuildLayersList();
            this->redraw();
        });
        selBtn->m_baseScale = 1.f;
        selBtn->setPosition({ hitW * 0.5f, rowH * 0.5f });
        rowMenu->addChild(selBtn);

        // Native GD visibility checkbox
        auto* visSpr = studio_ui::createBestSprite(
            { hidden ? "GJ_checkOff_001.png" : "GJ_checkOn_001.png" },
            13.f);
        auto* visBtn = CCMenuItemExt::createSpriteExtra(visSpr, [this, elemId](CCMenuItemSpriteExtra*) {
            if (m_closing) return;
            if (auto* el = this->findElement(elemId)) {
                if (el->isShape) m_liveShapes[el->id].hidden = !m_liveShapes[el->id].hidden;
                else m_liveButtons[el->id].hidden = !m_liveButtons[el->id].hidden;
                this->applyElementToScene(*el);
                this->pushHistory();
                this->refreshInspector();
                this->rebuildLayersList();
                this->redraw();
            }
        });
        visBtn->setPosition({ viewW - 29.f, rowH * 0.5f });
        rowMenu->addChild(visBtn);

        // Native GD lock icon
        auto* lockSpr = studio_ui::createBestSprite(
            { "GJ_lock_001.png" },
            12.f,
            locked ? paimon::ui::palette::warning : ccColor3B{ 125, 130, 145 });
        if (!locked && lockSpr) lockSpr->setOpacity(110);
        auto* lockBtn = CCMenuItemExt::createSpriteExtra(lockSpr, [this, elemId](CCMenuItemSpriteExtra*) {
            if (m_closing) return;
            if (auto* el = this->findElement(elemId)) {
                if (el->isShape) m_liveShapes[el->id].locked = !m_liveShapes[el->id].locked;
                else m_liveButtons[el->id].locked = !m_liveButtons[el->id].locked;
                this->pushHistory();
                this->refreshInspector();
                this->rebuildLayersList();
                this->redraw();
            }
        });
        lockBtn->setPosition({ viewW - 12.f, rowH * 0.5f });
        rowMenu->addChild(lockBtn);
    }

    m_layersScroll->moveToTop();
}

void MainMenuLayoutEditor::rebuildGrid() {
    if (!m_grid) return;
    m_grid->clear();
    bool const showAny = !m_previewMode && (m_settings.gridOn || m_settings.safeZone);
    m_grid->setVisible(showAny);
    if (!showAny) return;

    auto winSize = CCDirector::get()->getWinSize();

    if (m_settings.gridOn && m_settings.gridStep >= 4.f) {
        ccColor4F minor = { 1.f, 1.f, 1.f, 0.13f };
        ccColor4F axis = { 0.35f, 0.95f, 0.65f, 0.36f };
        for (float x = m_settings.gridStep; x < winSize.width; x += m_settings.gridStep) {
            m_grid->drawSegment({ x, 0.f }, { x, winSize.height }, 0.4f, minor);
        }
        for (float y = m_settings.gridStep; y < winSize.height; y += m_settings.gridStep) {
            m_grid->drawSegment({ 0.f, y }, { winSize.width, y }, 0.4f, minor);
        }
        m_grid->drawSegment({ winSize.width * 0.5f, 0.f }, { winSize.width * 0.5f, winSize.height }, 0.75f, axis);
        m_grid->drawSegment({ 0.f, winSize.height * 0.5f }, { winSize.width, winSize.height * 0.5f }, 0.75f, axis);
    }

    if (m_settings.safeZone) {
        CCRect safe(kSafeMargin, kSafeMargin, winSize.width - kSafeMargin * 2.f, winSize.height - kSafeMargin * 2.f);
        drawDashedRect(m_grid, safe, { 1.f, 0.82f, 0.3f, 0.45f }, 0.8f, 6.f, 4.f);
    }
}

void MainMenuLayoutEditor::updateAnimations(float dt) {
    m_interfaceElapsed += dt;
    float const dur = m_closing ? kCloseDuration : kIntroDuration;
    float const progress = std::clamp(m_interfaceElapsed / dur, 0.f, 1.f);
    float const eased = progress * progress * (3.f - 2.f * progress);
    m_interfaceOpacity = m_closing ? m_closeOpacity * (1.f - eased) : eased;
    this->applyInterfaceOpacity();

    for (auto const& elem : m_elements) {
        if (elem.isShape) continue;
        auto it = m_transitions.find(elem.id);
        if (it == m_transitions.end()) continue;
        auto targetIt = m_liveButtons.find(elem.id);
        if (targetIt == m_liveButtons.end() || !elem.target.node || !elem.target.node->getParent()) {
            m_transitions.erase(it);
            continue;
        }
        auto& trState = it->second;
        trState.elapsed += dt;
        float const phase = std::clamp(trState.elapsed / trState.duration, 0.f, 1.f);
        if (phase >= 1.f) {
            MainMenuLayoutManager::applyLayout(elem.target, targetIt->second);
            m_transitions.erase(it);
            continue;
        }
        float const t = phase * phase * (3.f - 2.f * phase);
        auto const& from = trState.from;
        auto const& to = targetIt->second;
        auto frame = to;
        auto mix = [t](float a, float b) { return a + (b - a) * t; };
        frame.position = from.position + (to.position - from.position) * t;
        frame.scale = mix(from.scale, to.scale);
        frame.scaleX = mix(from.scaleX, to.scaleX);
        frame.scaleY = mix(from.scaleY, to.scaleY);
        frame.rotation = mix(from.rotation, to.rotation);
        float const sOp = from.hidden ? 0.f : from.opacity;
        float const eOp = to.hidden ? 0.f : to.opacity;
        frame.opacity = mix(sOp, eOp);
        frame.hidden = from.hidden && to.hidden;
        frame.fontFile.clear();
        MainMenuLayoutManager::applyLayout(elem.target, frame);
    }
}

void MainMenuLayoutEditor::animateButtonToLive(EditorElement const& elem) {
    if (elem.isShape || !elem.target.node || !elem.target.node->getParent()) return;
    m_transitions[elem.id] = { MainMenuLayoutManager::readLayout(elem.target.node), 0.f, 0.34f };
}

MainMenuLayoutEditor::EditorElement* MainMenuLayoutEditor::findElement(std::string const& id) {
    for (auto& e : m_elements) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

MainMenuLayoutEditor::EditorElement const* MainMenuLayoutEditor::findElement(std::string const& id) const {
    for (auto const& e : m_elements) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

MainMenuLayoutEditor::EditorElement* MainMenuLayoutEditor::primaryElement() {
    if (m_selectedIds.empty()) return nullptr;
    return this->findElement(m_selectedIds.back());
}

MainMenuLayoutEditor::EditorElement const* MainMenuLayoutEditor::primaryElement() const {
    if (m_selectedIds.empty()) return nullptr;
    return this->findElement(m_selectedIds.back());
}

std::vector<MainMenuLayoutEditor::EditorElement*> MainMenuLayoutEditor::selectedElements() {
    std::vector<EditorElement*> out;
    out.reserve(m_selectedIds.size());
    for (auto const& id : m_selectedIds) {
        if (auto* el = this->findElement(id)) out.push_back(el);
    }
    return out;
}

bool MainMenuLayoutEditor::isSelected(std::string const& id) const {
    return std::find(m_selectedIds.begin(), m_selectedIds.end(), id) != m_selectedIds.end();
}

void MainMenuLayoutEditor::selectSingle(std::string const& id) {
    m_selectedIds.clear();
    if (!id.empty() && this->findElement(id)) {
        m_selectedIds.push_back(id);
    }
    if (auto* el = this->primaryElement(); el && !el->isShape) {
        auto const& b = m_liveButtons[el->id];
        m_uniformScale = std::abs(b.scaleX - b.scaleY) <= 0.005f;
    }
    this->placeInspector();
    this->syncHudVisibility();
    this->refreshInspector();
}

void MainMenuLayoutEditor::toggleSelection(std::string const& id) {
    if (id.empty() || !this->findElement(id)) return;
    auto it = std::find(m_selectedIds.begin(), m_selectedIds.end(), id);
    if (it != m_selectedIds.end()) m_selectedIds.erase(it);
    else m_selectedIds.push_back(id);
    this->placeInspector();
    this->syncHudVisibility();
    this->refreshInspector();
}

void MainMenuLayoutEditor::clearSelection() {
    m_selectedIds.clear();
    this->placeInspector();
    this->syncHudVisibility();
    this->refreshInspector();
}

void MainMenuLayoutEditor::selectAllElements() {
    m_selectedIds.clear();
    for (auto const& el : m_elements) {
        if (!this->isBackgroundElement(el)) {
            m_selectedIds.push_back(el.id);
        }
    }
    this->placeInspector();
    this->syncHudVisibility();
    this->refreshInspector();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::selectLinkGroup(std::string const& group) {
    if (group.empty()) return;
    m_selectedIds.clear();
    for (auto const& el : m_elements) {
        if (this->getLinkGroup(el) == group) {
            m_selectedIds.push_back(el.id);
        }
    }
    this->placeInspector();
    this->syncHudVisibility();
    this->refreshInspector();
    this->rebuildLayersList();
    this->redraw();
}

CCRect MainMenuLayoutEditor::elementRect(EditorElement const& elem) const {
    if (!elem.target.node || !elem.target.node->getParent()) return { 0.f, 0.f, 0.f, 0.f };
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        if (it == m_liveShapes.end()) return nodeWorldRect(elem.target.node, kMinHit);
        auto const& sh = it->second;
        auto wPos = worldPos(elem.target.node);
        float w = std::max(kMinHit, std::abs(sh.width * sh.scaleX));
        float h = std::max(kMinHit, std::abs(sh.height * sh.scaleY));
        if (sh.kind == DrawShapeKind::Circle) {
            float d = std::max(w, h);
            w = d;
            h = d;
        }
        return { wPos.x - w * 0.5f, wPos.y - h * 0.5f, w, h };
    }

    auto rect = nodeWorldRect(elem.target.node, kMinHit);
    for (auto const& fol : elem.target.labelGroupFollowers) {
        if (!fol || !fol->getParent()) continue;
        auto fr = nodeWorldRect(fol, kMinHit);
        float minX = std::min(rect.getMinX(), fr.getMinX());
        float minY = std::min(rect.getMinY(), fr.getMinY());
        float maxX = std::max(rect.getMaxX(), fr.getMaxX());
        float maxY = std::max(rect.getMaxY(), fr.getMaxY());
        rect = { minX, minY, maxX - minX, maxY - minY };
    }
    return rect;
}

CCRect MainMenuLayoutEditor::outlineRect(EditorElement const& elem) const {
    auto r = this->elementRect(elem);
    r.origin.x -= kOutlinePad;
    r.origin.y -= kOutlinePad;
    r.size.width += kOutlinePad * 2.f;
    r.size.height += kOutlinePad * 2.f;
    return r;
}

bool MainMenuLayoutEditor::isBackgroundElement(EditorElement const& elem) const {
    if (elem.isShape || !elem.target.node || !elem.target.node->getParent()) return false;
    auto win = CCDirector::get()->getWinSize();
    auto r = this->elementRect(elem);
    return r.size.width >= win.width * 0.8f && r.size.height >= win.height * 0.8f;
}

MainMenuLayoutEditor::EditorElement* MainMenuLayoutEditor::findElementAt(CCPoint wp) {
    EditorElement* best = nullptr;
    float bestArea = FLT_MAX;
    int bestZ = INT_MIN;

    for (auto& elem : m_elements) {
        if (!elem.target.node || !elem.target.node->getParent()) continue;
        if (this->isBackgroundElement(elem)) continue;
        bool const hid = this->isHidden(elem);
        if (hid && !this->isSelected(elem.id) && !m_settings.showHiddenOutlines) continue;
        auto rect = this->elementRect(elem);
        if (!rect.containsPoint(wp)) continue;

        float const area = rect.size.width * rect.size.height;
        int const z = this->getLayerZ(elem);
        if (area < bestArea * 0.85f || (std::abs(area - bestArea) <= bestArea * 0.15f && (elem.isShape < (best && best->isShape) || z > bestZ))) {
            best = &elem;
            bestArea = area;
            bestZ = z;
        }
    }
    return best;
}

bool MainMenuLayoutEditor::isPointInHud(CCPoint wp) const {
    if (m_previewMode) {
        if (m_previewPill && m_previewPill->isVisible()) {
            CCRect r(m_previewPill->getPositionX() - 54.f, m_previewPill->getPositionY() - 12.f, 108.f, 24.f);
            if (r.containsPoint(wp)) return true;
        }
        return false;
    }

    if (!m_hudRoot || !m_hudRoot->isVisible()) return false;
    auto winSize = CCDirector::get()->getWinSize();
    if (m_topBar && m_topBar->isVisible() && wp.y >= winSize.height - kTopBarH) return true;
    if (!m_collapsed && m_dock && m_dock->isVisible() && nodeWorldBoxRect(m_dock).containsPoint(wp)) return true;
    if (m_collapseBtn && m_collapseBtn->getParent()) {
        auto c = m_collapseBtn->getParent()->convertToWorldSpace(m_collapseBtn->getPosition());
        if (std::abs(wp.x - c.x) <= 22.f && std::abs(wp.y - c.y) <= 10.f) return true;
    }
    if (m_shapeBar && m_shapeBar->isVisible() && nodeWorldBoxRect(m_shapeBar).containsPoint(wp)) return true;
    if (m_layersPanel && m_layersPanel->isVisible() && nodeWorldBoxRect(m_layersPanel).containsPoint(wp)) return true;
    if (m_inspector && m_inspector->isVisible() && nodeWorldBoxRect(m_inspector).containsPoint(wp)) return true;
    return false;
}

bool MainMenuLayoutEditor::hasModalPopupOnTop() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    if (auto* children = scene->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child || child == this || !child->isVisible()) continue;
            if (typeinfo_cast<FLAlertLayer*>(child)) return true;
            if (child->getZOrder() > this->getZOrder()) return true;
        }
    }
    return false;
}

bool MainMenuLayoutEditor::isLocked(EditorElement const& elem) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        return it != m_liveShapes.end() && it->second.locked;
    }
    auto it = m_liveButtons.find(elem.id);
    return it != m_liveButtons.end() && it->second.locked;
}

bool MainMenuLayoutEditor::isHidden(EditorElement const& elem) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        return it != m_liveShapes.end() && it->second.hidden;
    }
    auto it = m_liveButtons.find(elem.id);
    return it != m_liveButtons.end() && it->second.hidden;
}

float MainMenuLayoutEditor::getOpacity(EditorElement const& elem) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        return it != m_liveShapes.end() ? it->second.opacity : 1.f;
    }
    auto it = m_liveButtons.find(elem.id);
    return it != m_liveButtons.end() ? it->second.opacity : 1.f;
}

float MainMenuLayoutEditor::getRotation(EditorElement const& elem) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        return it != m_liveShapes.end() ? it->second.rotation : 0.f;
    }
    auto it = m_liveButtons.find(elem.id);
    return it != m_liveButtons.end() ? it->second.rotation : 0.f;
}

int MainMenuLayoutEditor::getLayerZ(EditorElement const& elem) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        return it != m_liveShapes.end() ? it->second.layer : 0;
    }
    auto it = m_liveButtons.find(elem.id);
    return it != m_liveButtons.end() ? it->second.layer : 0;
}

std::string MainMenuLayoutEditor::getLinkGroup(EditorElement const& elem) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        return it != m_liveShapes.end() ? it->second.linkGroup : std::string();
    }
    auto it = m_liveButtons.find(elem.id);
    return it != m_liveButtons.end() ? it->second.linkGroup : std::string();
}

ccColor3B MainMenuLayoutEditor::getColor(EditorElement const& elem, bool* hasColorOut) const {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        if (hasColorOut) *hasColorOut = true;
        return it != m_liveShapes.end() ? it->second.color : ccColor3B{ 255, 255, 255 };
    }
    auto it = m_liveButtons.find(elem.id);
    if (it != m_liveButtons.end()) {
        if (hasColorOut) *hasColorOut = it->second.hasColor;
        return it->second.color;
    }
    if (hasColorOut) *hasColorOut = false;
    return { 255, 255, 255 };
}

void MainMenuLayoutEditor::applyElementToScene(EditorElement const& elem) {
    if (elem.isShape) {
        auto it = m_liveShapes.find(elem.id);
        if (it != m_liveShapes.end() && elem.target.node) {
            MainMenuLayoutManager::applyShapeLayout(elem.target.node, it->second);
        }
        return;
    }
    m_transitions.erase(elem.id);
    auto it = m_liveButtons.find(elem.id);
    if (it != m_liveButtons.end()) {
        MainMenuLayoutManager::applyLayout(elem.target, it->second);
    }
}

std::vector<DrawShapeLayout> MainMenuLayoutEditor::collectSceneShapesVector() const {
    std::vector<DrawShapeLayout> out;
    out.reserve(m_shapeOrder.size());
    for (auto const& id : m_shapeOrder) {
        auto it = m_liveShapes.find(id);
        if (it != m_liveShapes.end()) {
            auto s = it->second;
            s.scene = m_sceneName;
            out.push_back(std::move(s));
        }
    }
    return out;
}

float MainMenuLayoutEditor::snapToGrid(float value) const {
    if (!m_settings.snapEnabled || !m_settings.snapToGrid || m_settings.gridStep <= 1.f) return value;
    return std::round(value / m_settings.gridStep) * m_settings.gridStep;
}

CCPoint MainMenuLayoutEditor::snapWorld(EditorElement const& elem, CCPoint proposed) {
    if (!m_settings.snapEnabled) {
        if (m_guideX) m_guideX->setVisible(false);
        if (m_guideY) m_guideY->setVisible(false);
        return proposed;
    }

    if (m_settings.snapToGrid) {
        proposed.x = this->snapToGrid(proposed.x);
        proposed.y = this->snapToGrid(proposed.y);
    }

    if (!m_settings.smartGuides) {
        if (m_guideX) m_guideX->setVisible(false);
        if (m_guideY) m_guideY->setVisible(false);
        return proposed;
    }

    float const snapDist = m_settings.snapDistance;
    auto winSize = CCDirector::get()->getWinSize();
    auto rect = this->elementRect(elem);
    float const halfW = rect.size.width * 0.5f;
    float const halfH = rect.size.height * 0.5f;

    float bestX = proposed.x, bestY = proposed.y;
    float bdX = snapDist + 1.f, bdY = snapDist + 1.f;
    float guideLineX = proposed.x, guideLineY = proposed.y;
    bool sX = false, sY = false;

    auto considerX = [&](float targetCenter, float lineX) {
        float d = std::abs(proposed.x - targetCenter);
        if (d <= snapDist && d < bdX) { bdX = d; bestX = targetCenter; guideLineX = lineX; sX = true; }
    };
    auto considerY = [&](float targetCenter, float lineY) {
        float d = std::abs(proposed.y - targetCenter);
        if (d <= snapDist && d < bdY) { bdY = d; bestY = targetCenter; guideLineY = lineY; sY = true; }
    };

    considerX(winSize.width * 0.5f, winSize.width * 0.5f);
    considerY(winSize.height * 0.5f, winSize.height * 0.5f);
    considerX(halfW, 0.f);
    considerX(winSize.width - halfW, winSize.width);
    considerY(halfH, 0.f);
    considerY(winSize.height - halfH, winSize.height);
    if (m_settings.safeZone) {
        considerX(kSafeMargin + halfW, kSafeMargin);
        considerX(winSize.width - kSafeMargin - halfW, winSize.width - kSafeMargin);
        considerY(kSafeMargin + halfH, kSafeMargin);
        considerY(winSize.height - kSafeMargin - halfH, winSize.height - kSafeMargin);
    }

    for (auto const& other : m_elements) {
        if (&other == &elem || this->isSelected(other.id)) continue;
        if (!other.target.node || !other.target.node->getParent()) continue;
        if (this->isBackgroundElement(other) || this->isHidden(other)) continue;
        auto ow = worldPos(other.target.node);
        auto oRect = this->elementRect(other);
        considerX(ow.x, ow.x);
        considerY(ow.y, ow.y);
        considerX(oRect.getMinX() + halfW, oRect.getMinX());
        considerX(oRect.getMaxX() - halfW, oRect.getMaxX());
        considerY(oRect.getMinY() + halfH, oRect.getMinY());
        considerY(oRect.getMaxY() - halfH, oRect.getMaxY());
    }

    ccColor4F guideColor = { 0.25f, 0.96f, 0.65f, 0.88f };
    if (m_guideX) {
        m_guideX->clear();
        m_guideX->setVisible(sX);
        if (sX) m_guideX->drawSegment({ guideLineX, 0.f }, { guideLineX, winSize.height }, 0.9f, guideColor);
    }
    if (m_guideY) {
        m_guideY->clear();
        m_guideY->setVisible(sY);
        if (sY) m_guideY->drawSegment({ 0.f, guideLineY }, { winSize.width, guideLineY }, 0.9f, guideColor);
    }
    return { sX ? bestX : proposed.x, sY ? bestY : proposed.y };
}

void MainMenuLayoutEditor::moveElementAndPeers(EditorElement& elem, CCPoint deltaWorld, bool includeGroupPeers) {
    auto moveOne = [&](EditorElement& target) {
        if (!target.target.node || !target.target.node->getParent() || this->isLocked(target)) return;
        CCPoint w = worldPos(target.target.node) + deltaWorld;
        CCPoint local = target.target.node->getParent()->convertToNodeSpace(w);
        if (target.isShape) m_liveShapes[target.id].position = local;
        else m_liveButtons[target.id].position = local;
        this->applyElementToScene(target);
    };

    moveOne(elem);

    if (includeGroupPeers) {
        std::string const grp = this->getLinkGroup(elem);
        if (!grp.empty()) {
            for (auto& other : m_elements) {
                if (other.id == elem.id || this->isSelected(other.id)) continue;
                if (this->getLinkGroup(other) == grp) {
                    moveOne(other);
                }
            }
        }
    }
}

void MainMenuLayoutEditor::nudgeSelection(CCPoint deltaWorld) {
    for (auto* elem : this->selectedElements()) {
        this->moveElementAndPeers(*elem, deltaWorld, true);
    }
}

void MainMenuLayoutEditor::scaleSelection(float factor) {
    for (auto* elem : this->selectedElements()) {
        if (this->isLocked(*elem)) continue;
        if (elem->isShape) {
            auto& sh = m_liveShapes[elem->id];
            sh.width = std::clamp(sh.width * factor, kMinShapeSize, kMaxShapeSize);
            sh.height = std::clamp(sh.height * factor, kMinShapeSize, kMaxShapeSize);
        } else {
            auto& b = m_liveButtons[elem->id];
            b.scaleX = std::clamp(b.scaleX * factor, kMinScale, kMaxScale);
            b.scaleY = std::clamp(b.scaleY * factor, kMinScale, kMaxScale);
            b.scale = (b.scaleX + b.scaleY) * 0.5f;
        }
        this->applyElementToScene(*elem);
    }
}

void MainMenuLayoutEditor::setSelectionScale(float sx, float sy) {
    float cx = std::clamp(sx, kMinScale, kMaxScale);
    float cy = std::clamp(sy, kMinScale, kMaxScale);
    for (auto* elem : this->selectedElements()) {
        if (this->isLocked(*elem) || elem->isShape) continue;
        auto& b = m_liveButtons[elem->id];
        b.scaleX = cx;
        b.scaleY = cy;
        b.scale = (cx + cy) * 0.5f;
        this->applyElementToScene(*elem);
    }
}

void MainMenuLayoutEditor::rotateSelection(float deltaDegrees) {
    for (auto* elem : this->selectedElements()) {
        if (this->isLocked(*elem)) continue;
        if (elem->isShape) {
            auto& sh = m_liveShapes[elem->id];
            sh.rotation = std::fmod(sh.rotation + deltaDegrees, 360.f);
        } else {
            auto& b = m_liveButtons[elem->id];
            b.rotation = std::fmod(b.rotation + deltaDegrees, 360.f);
        }
        this->applyElementToScene(*elem);
    }
}

void MainMenuLayoutEditor::setSelectionOpacity(float opacity) {
    float op = std::clamp(opacity, 0.f, 1.f);
    for (auto* elem : this->selectedElements()) {
        if (this->isLocked(*elem)) continue;
        if (elem->isShape) {
            auto& sh = m_liveShapes[elem->id];
            sh.opacity = op;
            sh.hidden = false;
        } else {
            auto& b = m_liveButtons[elem->id];
            b.opacity = op;
            b.hidden = false;
        }
        this->applyElementToScene(*elem);
    }
    if (m_opacitySlider) m_opacitySlider->setValue(op);
}

void MainMenuLayoutEditor::setSelectionLayer(int deltaZ) {
    for (auto* elem : this->selectedElements()) {
        if (elem->isShape) {
            auto& sh = m_liveShapes[elem->id];
            sh.layer = std::clamp(sh.layer + deltaZ, -100, 100);
            sh.zOrder = sh.layer;
        } else {
            auto& b = m_liveButtons[elem->id];
            b.layer = std::clamp(b.layer + deltaZ, -100, 100);
        }
        this->applyElementToScene(*elem);
    }
}

void MainMenuLayoutEditor::toggleSelectionLock() {
    auto* prim = this->primaryElement();
    if (!prim) return;
    bool const targetLock = !this->isLocked(*prim);
    for (auto* el : this->selectedElements()) {
        if (el->isShape) m_liveShapes[el->id].locked = targetLock;
        else m_liveButtons[el->id].locked = targetLock;
    }
    this->pushHistory();
    this->refreshInspector();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::toggleSelectionHidden() {
    auto* prim = this->primaryElement();
    if (!prim) return;
    bool const targetHide = !this->isHidden(*prim);
    for (auto* el : this->selectedElements()) {
        if (el->isShape) {
            auto& sh = m_liveShapes[el->id];
            sh.hidden = targetHide;
            if (!sh.hidden && sh.opacity <= 0.01f) sh.opacity = 1.f;
        } else {
            auto& b = m_liveButtons[el->id];
            b.hidden = targetHide;
            if (!b.hidden && b.opacity <= 0.01f) b.opacity = 1.f;
        }
        this->applyElementToScene(*el);
    }
    this->pushHistory();
    this->refreshInspector();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::centerSelection(bool horizontal, bool vertical) {
    auto winSize = CCDirector::get()->getWinSize();
    for (auto* el : this->selectedElements()) {
        if (this->isLocked(*el) || !el->target.node || !el->target.node->getParent()) continue;
        auto w = worldPos(el->target.node);
        CCPoint delta{
            horizontal ? (winSize.width * 0.5f - w.x) : 0.f,
            vertical ? (winSize.height * 0.5f - w.y) : 0.f
        };
        this->moveElementAndPeers(*el, delta, m_selectedIds.size() == 1);
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::alignSelection(int mode) {
    auto elems = this->selectedElements();
    if (elems.empty()) return;

    auto winSize = CCDirector::get()->getWinSize();
    CCRect bounds(0.f, 0.f, winSize.width, winSize.height);
    if (!m_alignToCanvas && elems.size() >= 2) {
        float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
        for (auto* el : elems) {
            auto r = this->elementRect(*el);
            minX = std::min(minX, r.getMinX());
            minY = std::min(minY, r.getMinY());
            maxX = std::max(maxX, r.getMaxX());
            maxY = std::max(maxY, r.getMaxY());
        }
        bounds = { minX, minY, maxX - minX, maxY - minY };
    }

    for (auto* el : elems) {
        if (this->isLocked(*el) || !el->target.node || !el->target.node->getParent()) continue;
        auto r = this->elementRect(*el);
        CCPoint delta{ 0.f, 0.f };
        switch (mode) {
            case 0: delta.x = bounds.getMinX() - r.getMinX(); break;
            case 1: delta.x = bounds.getMidX() - r.getMidX(); break;
            case 2: delta.x = bounds.getMaxX() - r.getMaxX(); break;
            case 3: delta.y = bounds.getMaxY() - r.getMaxY(); break;
            case 4: delta.y = bounds.getMidY() - r.getMidY(); break;
            case 5: delta.y = bounds.getMinY() - r.getMinY(); break;
            default: break;
        }
        this->moveElementAndPeers(*el, delta, false);
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::distributeSelection(bool horizontal) {
    auto elems = this->selectedElements();
    if (elems.size() < 3) return;

    std::sort(elems.begin(), elems.end(), [this, horizontal](EditorElement* a, EditorElement* b) {
        auto wa = worldPos(a->target.node);
        auto wb = worldPos(b->target.node);
        return horizontal ? (wa.x < wb.x) : (wa.y < wb.y);
    });

    float first = horizontal ? worldPos(elems.front()->target.node).x : worldPos(elems.front()->target.node).y;
    float last = horizontal ? worldPos(elems.back()->target.node).x : worldPos(elems.back()->target.node).y;
    float step = (last - first) / static_cast<float>(elems.size() - 1);

    for (std::size_t i = 1; i + 1 < elems.size(); ++i) {
        auto* el = elems[i];
        if (this->isLocked(*el)) continue;
        float cur = horizontal ? worldPos(el->target.node).x : worldPos(el->target.node).y;
        float target = first + step * static_cast<float>(i);
        CCPoint delta = horizontal ? CCPoint{ target - cur, 0.f } : CCPoint{ 0.f, target - cur };
        this->moveElementAndPeers(*el, delta, false);
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::matchSelectionScale() {
    auto* prim = this->primaryElement();
    if (!prim || prim->isShape) return;
    auto const& ref = m_liveButtons[prim->id];
    for (auto* el : this->selectedElements()) {
        if (el == prim || el->isShape || this->isLocked(*el)) continue;
        auto& b = m_liveButtons[el->id];
        b.scale = ref.scale;
        b.scaleX = ref.scaleX;
        b.scaleY = ref.scaleY;
        this->applyElementToScene(*el);
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::matchSelectionOpacity() {
    auto* prim = this->primaryElement();
    if (!prim) return;
    float op = this->getOpacity(*prim);
    for (auto* el : this->selectedElements()) {
        if (el == prim || this->isLocked(*el)) continue;
        if (el->isShape) m_liveShapes[el->id].opacity = op;
        else m_liveButtons[el->id].opacity = op;
        this->applyElementToScene(*el);
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::linkSelectedTogether() {
    auto elems = this->selectedElements();
    if (elems.size() < 2) return;

    std::string chosen;
    for (auto* el : elems) {
        auto g = this->getLinkGroup(*el);
        if (!g.empty()) { chosen = g; break; }
    }
    if (chosen.empty()) {
        std::unordered_set<std::string> used;
        for (auto const& e : m_elements) {
            auto g = this->getLinkGroup(e);
            if (!g.empty()) used.insert(g);
        }
        for (int i = 1; i <= 20; ++i) {
            auto cand = fmt::format("G{}", i);
            if (!used.contains(cand)) { chosen = cand; break; }
        }
        if (chosen.empty()) chosen = "G1";
    }

    for (auto* el : elems) {
        if (el->isShape) m_liveShapes[el->id].linkGroup = chosen;
        else m_liveButtons[el->id].linkGroup = chosen;
    }
    this->pushHistory();
    this->refreshInspector();
    this->rebuildLayersList();
    this->redraw();
    PaimonNotify::show(fmt::format("{} {}", loc("Linked to", "Vinculado a"), chosen), NotificationIcon::Success);
}

void MainMenuLayoutEditor::unlinkSelected() {
    for (auto* el : this->selectedElements()) {
        if (el->isShape) m_liveShapes[el->id].linkGroup.clear();
        else m_liveButtons[el->id].linkGroup.clear();
    }
    this->pushHistory();
    this->refreshInspector();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::copySelectedStyle() {
    auto* el = this->primaryElement();
    if (!el) return;
    m_styleClipboard = {};
    m_styleClipboard.valid = true;
    if (el->isShape) {
        auto const& sh = m_liveShapes[el->id];
        m_styleClipboard.scaleX = sh.scaleX;
        m_styleClipboard.scaleY = sh.scaleY;
        m_styleClipboard.rotation = sh.rotation;
        m_styleClipboard.opacity = sh.opacity;
        m_styleClipboard.hasColor = true;
        m_styleClipboard.color = sh.color;
        m_styleClipboard.strokeWidth = sh.strokeWidth;
        m_styleClipboard.strokeColor = sh.strokeColor;
        m_styleClipboard.cornerRadius = sh.cornerRadius;
    } else {
        auto const& b = m_liveButtons[el->id];
        m_styleClipboard.scaleX = b.scaleX;
        m_styleClipboard.scaleY = b.scaleY;
        m_styleClipboard.rotation = b.rotation;
        m_styleClipboard.opacity = b.opacity;
        m_styleClipboard.hasColor = b.hasColor;
        m_styleClipboard.color = b.color;
        m_styleClipboard.fontFile = b.fontFile;
    }
    this->refreshInspector();
    PaimonNotify::show(loc("Style copied", "Estilo copiado"), NotificationIcon::Info);
}

void MainMenuLayoutEditor::pasteStyleToSelected() {
    if (!m_styleClipboard.valid) return;
    for (auto* el : this->selectedElements()) {
        if (this->isLocked(*el)) continue;
        if (el->isShape) {
            auto& sh = m_liveShapes[el->id];
            sh.rotation = m_styleClipboard.rotation;
            sh.opacity = m_styleClipboard.opacity;
            if (m_styleClipboard.hasColor) sh.color = m_styleClipboard.color;
            sh.strokeWidth = m_styleClipboard.strokeWidth;
            sh.strokeColor = m_styleClipboard.strokeColor;
            sh.cornerRadius = m_styleClipboard.cornerRadius;
        } else {
            auto& b = m_liveButtons[el->id];
            b.scaleX = m_styleClipboard.scaleX;
            b.scaleY = m_styleClipboard.scaleY;
            b.scale = (b.scaleX + b.scaleY) * 0.5f;
            b.rotation = m_styleClipboard.rotation;
            b.opacity = m_styleClipboard.opacity;
            if (m_styleClipboard.hasColor) {
                b.hasColor = true;
                b.color = m_styleClipboard.color;
            }
            if (!m_styleClipboard.fontFile.empty() && findEditableLabel(el->target.node.data())) {
                b.fontFile = m_styleClipboard.fontFile;
            }
        }
        this->applyElementToScene(*el);
    }
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::resetElementToDefault(EditorElement& elem) {
    if (elem.isShape) {
        auto it = std::find_if(m_initialShapes.begin(), m_initialShapes.end(), [&elem](DrawShapeLayout const& s) {
            return s.id == elem.id;
        });
        if (it != m_initialShapes.end()) {
            m_liveShapes[elem.id] = *it;
        } else {
            auto& sh = m_liveShapes[elem.id];
            auto win = CCDirector::get()->getWinSize();
            sh.position = CCPoint{ win.width * 0.5f, win.height * 0.5f };
            sh.scale = sh.scaleX = sh.scaleY = 1.f;
            sh.rotation = 0.f;
            sh.opacity = 0.78f;
            sh.hidden = false;
            sh.locked = false;
        }
        this->applyElementToScene(elem);
        return;
    }

    auto& mgr = MainMenuLayoutManager::get();
    auto def = mgr.getSessionDefaultLayout(elem.id);
    if (!def) def = mgr.getDefaultLayout(elem.id);
    if (!def) {
        auto it = m_initialButtons.find(elem.id);
        if (it == m_initialButtons.end()) return;
        def = it->second;
    }
    m_liveButtons[elem.id] = *def;
    this->animateButtonToLive(elem);
}

void MainMenuLayoutEditor::addDrawShape(DrawShapeKind kind) {
    auto* root = this->getTargetRoot();
    if (!root) return;

    auto winSize = CCDirector::get()->getWinSize();
    DrawShapeLayout shape;
    do {
        shape.id = fmt::format("shape-{}-{}", m_sceneName, m_nextShapeCounter++);
    } while (m_liveShapes.contains(shape.id));

    shape.kind = kind;
    shape.scene = m_sceneName;
    float const offset = static_cast<float>((m_shapeOrder.size() % 5) * 10);
    shape.position = CCPoint{ winSize.width * 0.5f + offset, winSize.height * 0.5f - offset };
    shape.width = kind == DrawShapeKind::Circle ? 86.f : 126.f;
    shape.height = kind == DrawShapeKind::Circle ? 86.f : 72.f;
    shape.cornerRadius = kind == DrawShapeKind::RoundedRect ? 16.f : 0.f;
    shape.color = kind == DrawShapeKind::Circle ? ccColor3B{ 96, 218, 255 }
                : kind == DrawShapeKind::Rectangle ? ccColor3B{ 18, 24, 38 }
                : ccColor3B{ 16, 26, 44 };
    shape.opacity = 0.78f;
    shape.layer = -2;
    shape.zOrder = -2;

    m_liveShapes[shape.id] = shape;
    m_shapeOrder.push_back(shape.id);
    MainMenuLayoutManager::get().syncShapes(root, this->collectSceneShapesVector());
    this->rebuildShapeElements();
    this->selectSingle(shape.id);
    this->setInspectorTab(InspectorTab::Style);
    this->pushHistory();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::duplicateSelectedShape() {
    auto* el = this->primaryElement();
    auto* root = this->getTargetRoot();
    if (!el || !el->isShape || !root) return;

    DrawShapeLayout copy = m_liveShapes[el->id];
    do {
        copy.id = fmt::format("shape-{}-{}", m_sceneName, m_nextShapeCounter++);
    } while (m_liveShapes.contains(copy.id));
    copy.position = CCPoint{ copy.position.x + 14.f, copy.position.y - 14.f };
    copy.locked = false;

    m_liveShapes[copy.id] = copy;
    m_shapeOrder.push_back(copy.id);
    MainMenuLayoutManager::get().syncShapes(root, this->collectSceneShapesVector());
    this->rebuildShapeElements();
    this->selectSingle(copy.id);
    this->pushHistory();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::deleteSelectedShapes() {
    auto* root = this->getTargetRoot();
    if (!root) return;

    bool removed = false;
    for (auto const& id : m_selectedIds) {
        if (m_liveShapes.erase(id) > 0) {
            m_shapeOrder.erase(std::remove(m_shapeOrder.begin(), m_shapeOrder.end(), id), m_shapeOrder.end());
            removed = true;
        }
    }
    if (!removed) return;

    MainMenuLayoutManager::get().syncShapes(root, this->collectSceneShapesVector());
    this->rebuildShapeElements();
    this->clearSelection();
    this->pushHistory();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::pickSelectedColor(bool strokeColor) {
    auto* el = this->primaryElement();
    if (!el || this->isLocked(*el)) return;

    ccColor3B initial = { 255, 255, 255 };
    if (el->isShape) {
        initial = strokeColor ? m_liveShapes[el->id].strokeColor : m_liveShapes[el->id].color;
    } else {
        if (strokeColor) return;
        initial = m_liveButtons[el->id].color;
    }

    auto* picker = ColorPickPopup::create(ccc4(initial.r, initial.g, initial.b, 255));
    if (!picker) return;
    WeakRef<MainMenuLayoutEditor> self = this;
    std::string targetId = el->id;
    picker->setCallback([self, targetId, strokeColor](ccColor4B const& picked) {
        auto* ed = self.lock().data();
        if (!ed || !ed->getParent() || ed->m_closing) return;
        auto* elem = ed->findElement(targetId);
        if (!elem) return;
        ccColor3B c{ picked.r, picked.g, picked.b };
        if (elem->isShape) {
            if (strokeColor) ed->m_liveShapes[elem->id].strokeColor = c;
            else ed->m_liveShapes[elem->id].color = c;
        } else {
            ed->m_liveButtons[elem->id].hasColor = true;
            ed->m_liveButtons[elem->id].color = c;
        }
        ed->applyElementToScene(*elem);
        ed->pushHistory();
        ed->refreshInspector();
        ed->redraw();
    });
    picker->show();
}

void MainMenuLayoutEditor::resetSelectedColor() {
    auto* el = this->primaryElement();
    if (!el || this->isLocked(*el)) return;
    if (el->isShape) {
        m_liveShapes[el->id].color = { 90, 220, 255 };
        m_liveShapes[el->id].strokeColor = { 255, 255, 255 };
    } else {
        auto& mgr = MainMenuLayoutManager::get();
        auto def = mgr.getSessionDefaultLayout(el->id);
        if (!def) def = mgr.getDefaultLayout(el->id);
        m_liveButtons[el->id].color = def ? def->color : ccColor3B{ 255, 255, 255 };
        m_liveButtons[el->id].hasColor = def ? def->hasColor : true;
    }
    this->applyElementToScene(*el);
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::pickSelectedFont() {
    auto* el = this->primaryElement();
    if (!el || el->isShape || this->isLocked(*el)) return;
    if (!findEditableLabel(el->target.node.data())) return;

    WeakRef<MainMenuLayoutEditor> self = this;
    std::string targetId = el->id;
    auto* picker = paimon::fonts::FontPickerPopup::create([self, targetId](std::string const& fntFile) {
        auto* ed = self.lock().data();
        if (!ed || !ed->getParent() || ed->m_closing) return;
        auto* elem = ed->findElement(targetId);
        if (!elem) return;
        if (fntFile.empty()) {
            ed->resetSelectedFont();
            return;
        }
        ed->m_liveButtons[elem->id].fontFile = fntFile;
        ed->applyElementToScene(*elem);
        ed->pushHistory();
        ed->refreshInspector();
        ed->redraw();
    });
    if (picker) picker->show();
}

void MainMenuLayoutEditor::resetSelectedFont() {
    auto* el = this->primaryElement();
    if (!el || el->isShape || this->isLocked(*el)) return;
    auto& mgr = MainMenuLayoutManager::get();
    auto def = mgr.getSessionDefaultLayout(el->id);
    if (!def) def = mgr.getDefaultLayout(el->id);
    m_liveButtons[el->id].fontFile = def ? def->fontFile : std::string();
    this->applyElementToScene(*el);
    this->pushHistory();
    this->refreshInspector();
    this->redraw();
}

LayoutSnapshot MainMenuLayoutEditor::buildFullSnapshot() const {
    LayoutSnapshot snap;
    snap.buttons = m_liveButtons;
    snap.shapes = this->collectSceneShapesVector();
    return snap;
}

void MainMenuLayoutEditor::pushHistory() {
    if (m_applyingHistory) return;
    auto snap = this->buildFullSnapshot();
    if (m_historyCursor + 1 < m_history.size()) {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCursor + 1), m_history.end());
    }
    m_history.push_back(std::move(snap));
    if (m_history.size() > kHistoryLimit) m_history.erase(m_history.begin());
    m_historyCursor = m_history.empty() ? 0 : m_history.size() - 1;
    this->refreshHudState();
}

void MainMenuLayoutEditor::applyHistory(LayoutSnapshot const& snapshot) {
    auto* root = this->getTargetRoot();
    if (!root) return;
    m_applyingHistory = true;
    auto& mgr = MainMenuLayoutManager::get();

    for (auto const& elem : m_elements) {
        if (elem.isShape) continue;
        auto it = snapshot.buttons.find(elem.id);
        if (it != snapshot.buttons.end()) m_liveButtons[elem.id] = it->second;
        else if (auto sess = mgr.getSessionDefaultLayout(elem.id)) m_liveButtons[elem.id] = *sess;
        else if (auto def = mgr.getDefaultLayout(elem.id)) m_liveButtons[elem.id] = *def;
        this->animateButtonToLive(elem);
    }

    m_liveShapes.clear();
    m_shapeOrder.clear();
    for (auto s : snapshot.shapes) {
        std::string const sc = s.scene.empty() ? "MenuLayer" : s.scene;
        if (sc != m_sceneName || s.id.empty()) continue;
        s.scene = m_sceneName;
        m_liveShapes[s.id] = s;
        m_shapeOrder.push_back(s.id);
    }
    mgr.syncShapes(root, this->collectSceneShapesVector());
    this->rebuildShapeElements();

    m_selectedIds.erase(
        std::remove_if(m_selectedIds.begin(), m_selectedIds.end(), [this](std::string const& id) {
            return !this->findElement(id);
        }),
        m_selectedIds.end());

    m_applyingHistory = false;
    this->syncHudVisibility();
    this->refreshHudState();
    this->refreshInspector();
    this->rebuildLayersList();
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

bool MainMenuLayoutEditor::isDirty() const {
    for (auto const& [key, layout] : m_liveButtons) {
        auto it = m_initialButtons.find(key);
        if (it == m_initialButtons.end() || !buttonLayoutEqual(layout, it->second)) {
            return true;
        }
    }
    auto curShapes = this->collectSceneShapesVector();
    if (curShapes.size() != m_initialShapes.size()) return true;
    for (std::size_t i = 0; i < curShapes.size(); ++i) {
        if (!shapeLayoutEqual(curShapes[i], m_initialShapes[i])) return true;
    }
    return false;
}

void MainMenuLayoutEditor::redraw() {
    if (!m_highlights || !m_outline || !m_grip || !m_marqueeDraw) return;
    m_highlights->clear();
    m_outline->clear();
    m_grip->clear();
    m_marqueeDraw->clear();
    if (m_previewMode) return;

    float const op = m_interfaceOpacity;
    for (auto const& elem : m_elements) {
        if (!elem.target.node || !elem.target.node->getParent()) continue;
        if (this->isBackgroundElement(elem)) continue;
        if (this->isSelected(elem.id)) continue;

        bool const hid = this->isHidden(elem);
        if (hid && !m_settings.showHiddenOutlines) continue;

        auto rect = this->elementRect(elem);
        if (hid) {
            drawDashedRect(m_highlights, rect, { 1.f, 0.65f, 0.25f, 0.42f * op }, 0.85f, 4.f, 4.f);
        } else {
            ccColor4F col = elem.isShape
                ? ccColor4F{ 0.78f, 0.56f, 1.f, 0.45f * op }
                : ccColor4F{ 0.38f, 0.76f, 1.f, 0.35f * op };
            strokeRect(m_highlights, rect, col, 0.85f);
        }
    }

    std::unordered_set<std::string> activeGroups;
    for (auto* el : this->selectedElements()) {
        auto g = this->getLinkGroup(*el);
        if (!g.empty()) activeGroups.insert(g);
    }
    if (!activeGroups.empty()) {
        for (auto const& elem : m_elements) {
            if (this->isSelected(elem.id)) continue;
            if (activeGroups.contains(this->getLinkGroup(elem))) {
                drawDashedRect(m_outline, this->outlineRect(elem), { 1.f, 0.82f, 0.3f, 0.75f * op }, 1.1f, 4.f, 3.f);
            }
        }
    }

    for (auto* el : this->selectedElements()) {
        if (!el->target.node || !el->target.node->getParent()) continue;
        bool const locked = this->isLocked(*el);
        bool const primary = (el == this->primaryElement());
        ccColor4F col = locked
            ? ccColor4F{ 1.f, 0.65f, 0.25f, 0.95f * op }
            : (el->isShape ? ccColor4F{ 0.78f, 0.58f, 1.f, 0.98f * op }
                           : ccColor4F{ 0.38f, 0.96f, 0.64f, 0.98f * op });

        auto outline = this->outlineRect(*el);
        strokeRect(m_outline, outline, col, primary ? 1.7f : 1.2f);

        auto glow = outline;
        glow.origin.x -= 2.5f; glow.origin.y -= 2.5f;
        glow.size.width += 5.f; glow.size.height += 5.f;
        strokeRect(m_outline, glow, { col.r, col.g, col.b, 0.24f * op }, 0.9f);

        if (primary && m_selectedIds.size() == 1 && !locked) {
            ccColor4F edge{ 1.f, 1.f, 1.f, 0.95f * op };
            ccColor4F cornerFill{ 0.36f, 0.96f, 0.62f, op };
            ccColor4F sideFill{ 0.42f, 0.84f, 1.f, op };

            CCPoint corners[4] = {
                { outline.getMinX(), outline.getMinY() },
                { outline.getMaxX(), outline.getMinY() },
                { outline.getMaxX(), outline.getMaxY() },
                { outline.getMinX(), outline.getMaxY() },
            };
            for (auto const& c : corners) {
                drawSquareHandle(m_grip, c, kHandleDrawR, cornerFill, edge);
            }

            CCPoint sides[4] = {
                { outline.getMinX(), outline.getMidY() },
                { outline.getMaxX(), outline.getMidY() },
                { outline.getMidX(), outline.getMaxY() },
                { outline.getMidX(), outline.getMinY() },
            };
            for (auto const& s : sides) {
                drawSquareHandle(m_grip, s, kHandleDrawR * 0.85f, sideFill, edge);
            }

            CCPoint rotPos{ outline.getMidX(), outline.getMaxY() + kRotateArm };
            m_grip->drawSegment({ outline.getMidX(), outline.getMaxY() }, rotPos, 1.1f, { 0.48f, 0.82f, 1.f, 0.78f * op });
            m_grip->drawDot(rotPos, 5.2f, { 0.42f, 0.82f, 1.f, op });
        }
    }

    if (m_drag == DragMode::Marquee) {
        CCRect mq(
            std::min(m_touchStart.x, m_touchCur.x),
            std::min(m_touchStart.y, m_touchCur.y),
            std::abs(m_touchCur.x - m_touchStart.x),
            std::abs(m_touchCur.y - m_touchStart.y));
        CCPoint pts[4] = {
            { mq.getMinX(), mq.getMinY() },
            { mq.getMaxX(), mq.getMinY() },
            { mq.getMaxX(), mq.getMaxY() },
            { mq.getMinX(), mq.getMaxY() },
        };
        m_marqueeDraw->drawPolygon(pts, 4, { 0.35f, 0.78f, 1.f, 0.16f }, 1.2f, { 0.45f, 0.85f, 1.f, 0.85f });
    }

    this->updateHint();
    this->refreshHudState();
}

void MainMenuLayoutEditor::refreshHudState() {
    bool const canUndo = !m_history.empty() && m_historyCursor > 0;
    bool const canRedo = !m_history.empty() && m_historyCursor + 1 < m_history.size();
    studio_ui::setDockToolState(m_undoBtn, false, canUndo);
    studio_ui::setDockToolState(m_redoBtn, false, canRedo);
    studio_ui::setDockToolState(m_gridBtn, m_settings.gridOn, true);
    studio_ui::setDockToolState(m_snapBtn, m_settings.snapEnabled, true);
    studio_ui::setDockToolState(m_multiBtn, m_multiSelectMode, true);
    studio_ui::setDockToolState(m_shapeToolBtn, m_shapeBarOpen, true);
    studio_ui::setDockToolState(m_layersBtn, m_layersOpen, true);
    if (m_dirtyDot) m_dirtyDot->setVisible(this->isDirty());
}

void MainMenuLayoutEditor::refreshInspector() {
    if (!m_inspector) return;
    bool const multi = m_selectedIds.size() > 1;
    if (m_inspSingleRoot) m_inspSingleRoot->setVisible(!multi);
    if (m_inspMultiRoot) m_inspMultiRoot->setVisible(multi);

    if (m_selectedIds.empty()) return;

    if (multi) {
        setLabelIfChanged(m_inspName, fmt::format(fmt::runtime(loc("{} Selected", "{} Selec.")), m_selectedIds.size()));
        m_inspName->limitLabelWidth(108.f, 0.42f, 0.16f);
        setLabelIfChanged(m_inspSub, loc("Multi-selection actions", "Acciones multiples"));
        studio_ui::setGdButtonState(
            m_alignTargetBtn,
            m_alignToCanvas ? paimon::ui::Btn::Blue : paimon::ui::Btn::Cyan,
            m_alignToCanvas ? loc("To Screen", "A Pantalla") : loc("To Selection", "A Seleccion"));
        if (m_distHBtn) m_distHBtn->setEnabled(m_selectedIds.size() >= 3);
        if (m_distVBtn) m_distVBtn->setEnabled(m_selectedIds.size() >= 3);
        return;
    }

    auto* el = this->primaryElement();
    if (!el || !el->target.node || !el->target.node->getParent()) {
        return;
    }

    m_inspName->setString(el->info.displayName.c_str());
    m_inspName->limitLabelWidth(108.f, 0.42f, 0.16f);

    std::string grp = this->getLinkGroup(*el);
    std::string sub = grp.empty() ? el->info.groupTag : fmt::format("{} | {}", el->info.groupTag, grp);
    if (this->isLocked(*el)) sub += loc(" [LOCKED]", " [BLOQ]");
    setLabelIfChanged(m_inspSub, sub);
    m_inspSub->limitLabelWidth(182.f, 0.40f, 0.16f);

    auto w = worldPos(el->target.node);
    setLabelIfChanged(m_valX, fmt::format("{:.0f}", w.x));
    setLabelIfChanged(m_valY, fmt::format("{:.0f}", w.y));
    setLabelIfChanged(m_valRot, fmt::format("{:.0f}", this->getRotation(*el)));
    setLabelIfChanged(m_valLayer, fmt::format("{}", this->getLayerZ(*el)));

    if (el->isShape) {
        auto const& sh = m_liveShapes[el->id];
        setLabelIfChanged(m_lblScaleA, tr("menu_layout.draw_shape_width"));
        setLabelIfChanged(m_valScaleA, fmt::format("{:.0f}", sh.width));
        setLabelIfChanged(m_lblScaleB, tr("menu_layout.draw_shape_height"));
        setLabelIfChanged(m_valScaleB, fmt::format("{:.0f}", sh.height));
        if (m_aspectLockBtn) m_aspectLockBtn->setVisible(false);
        if (m_rowScaleB) m_rowScaleB->setVisible(true);
    } else {
        auto const& b = m_liveButtons[el->id];
        if (m_aspectLockBtn) {
            m_aspectLockBtn->setVisible(true);
            studio_ui::setGdButtonState(
                m_aspectLockBtn,
                m_uniformScale ? paimon::ui::Btn::Green : paimon::ui::Btn::Blue,
                m_uniformScale ? loc("Lock", "Fijo") : loc("Free", "Libre"));
        }
        if (m_uniformScale) {
            setLabelIfChanged(m_lblScaleA, tr("menu_layout.field_scale"));
            setLabelIfChanged(m_valScaleA, fmt::format("{:.0f}%", b.scaleX * 100.f));
            if (m_rowScaleB) m_rowScaleB->setVisible(false);
        } else {
            setLabelIfChanged(m_lblScaleA, loc("Scale X", "Esc X"));
            setLabelIfChanged(m_valScaleA, fmt::format("{:.0f}%", b.scaleX * 100.f));
            setLabelIfChanged(m_lblScaleB, loc("Scale Y", "Esc Y"));
            setLabelIfChanged(m_valScaleB, fmt::format("{:.0f}%", b.scaleY * 100.f));
            if (m_rowScaleB) m_rowScaleB->setVisible(true);
        }
    }

    float op = this->getOpacity(*el);
    setLabelIfChanged(m_valOpacity, fmt::format("{:.0f}%", std::clamp(op, 0.f, 1.f) * 100.f));
    if (m_opacitySlider && m_drag == DragMode::None) {
        m_opacitySlider->setValue(std::clamp(op, 0.f, 1.f));
    }
    if (m_colorSwatch) {
        m_colorSwatch->setColor(this->getColor(*el));
    }

    bool const hasFont = !el->isShape && findEditableLabel(el->target.node.data()) != nullptr;
    if (m_fontRow) m_fontRow->setVisible(hasFont);
    if (hasFont) {
        setLabelIfChanged(m_valFont, shortFontName(m_liveButtons[el->id].fontFile));
        m_valFont->limitLabelWidth(54.f, 0.44f, 0.16f);
    }

    if (m_shapeStyleRoot) m_shapeStyleRoot->setVisible(el->isShape);
    if (el->isShape) {
        auto const& sh = m_liveShapes[el->id];
        int kindIdx = sh.kind == DrawShapeKind::Rectangle ? 0 : (sh.kind == DrawShapeKind::Circle ? 2 : 1);
        for (int i = 0; i < 3; ++i) {
            bool active = (i == kindIdx);
            studio_ui::setGdButtonState(m_shapeKindBtns[i], active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
        }
        setLabelIfChanged(m_valRadius, sh.kind == DrawShapeKind::RoundedRect ? fmt::format("{:.0f}", sh.cornerRadius) : "N/A");
        setLabelIfChanged(m_valStroke, fmt::format("{:.1f}", sh.strokeWidth));
        if (m_strokeSwatch) m_strokeSwatch->setColor(sh.strokeColor);
    }

    if (m_pasteStyleBtn) {
        m_pasteStyleBtn->setEnabled(m_styleClipboard.valid);
        studio_ui::setGdButtonState(
            m_pasteStyleBtn,
            m_styleClipboard.valid ? paimon::ui::Btn::Cyan : paimon::ui::Btn::Gray);
    }

    bool const hidden = this->isHidden(*el);
    bool const locked = this->isLocked(*el);
    studio_ui::setGdButtonState(
        m_hideBtn,
        hidden ? paimon::ui::Btn::Red : paimon::ui::Btn::Gray,
        hidden ? tr("menu_layout.action_show") : tr("menu_layout.action_hide"));
    studio_ui::setGdButtonState(
        m_lockBtn,
        locked ? paimon::ui::Btn::Pink : paimon::ui::Btn::Gray,
        locked ? tr("menu_layout.action_unlock") : tr("menu_layout.action_lock"));

    for (auto const& [gid, btn] : m_groupBtns) {
        bool active = (gid == grp);
        studio_ui::setGdButtonState(btn, active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
}

void MainMenuLayoutEditor::updateLiveBadge(std::string const& text, bool visible) {
    if (!m_liveBadge || !m_liveBadgeLbl) return;
    m_liveBadge->setVisible(visible && !m_previewMode);
    if (visible) {
        setLabelIfChanged(m_liveBadgeLbl, text);
        m_liveBadgeLbl->limitLabelWidth(142.f, 0.30f, 0.14f);
    }
}

void MainMenuLayoutEditor::updateHint() {
    if (!m_hintLbl) return;
    std::string text;
    if (m_selectedIds.empty()) {
        text = loc("Tap an element or drag a box to select", "Toca un elemento o arrastra para seleccionar");
    } else if (m_selectedIds.size() > 1) {
        text = fmt::format(fmt::runtime(loc("{} selected - Drag to move or align", "{} selec. - Arrastra o alinea")), m_selectedIds.size());
    } else if (auto* el = this->primaryElement()) {
        if (this->isLocked(*el)) {
            text = tr("menu_layout.hint_locked");
        } else {
            auto w = worldPos(el->target.node);
            text = fmt::format("{}  X:{:.0f} Y:{:.0f}  {:.0f}%",
                el->info.displayName, w.x, w.y,
                el->isShape ? 100.f : (m_liveButtons[el->id].scaleX * 100.f));
        }
    }
    if (text != m_hintLbl->getString()) {
        m_hintLbl->setString(text.c_str());
        m_hintLbl->limitLabelWidth(m_hintMaxWidth, 0.40f, 0.12f);
    }
}

void MainMenuLayoutEditor::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -125, true);
}

bool MainMenuLayoutEditor::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_closing || m_interfaceElapsed < kIntroDuration) return true;
    auto wp = touch->getLocation();

    if (this->isPointInHud(wp)) return true;
    if (m_previewMode) return true;
    if (!m_transitions.empty()) return true;

    if (m_selectedIds.size() == 1) {
        if (auto* sel = this->primaryElement(); sel && sel->target.node && sel->target.node->getParent() && !this->isLocked(*sel)) {
            auto outline = this->outlineRect(*sel);
            auto r = this->elementRect(*sel);
            CCPoint center{ r.getMidX(), r.getMidY() };

            CCPoint rotPos{ outline.getMidX(), outline.getMaxY() + kRotateArm };
            if (ccpDistanceSQ(wp, rotPos) <= kRotateHitR * kRotateHitR) {
                m_drag = DragMode::Rotate;
                m_dragChanged = false;
                m_pivotWorld = center;
                m_dragStartAngle = CC_RADIANS_TO_DEGREES(std::atan2(wp.y - center.y, wp.x - center.x));
                m_dragStartRot = this->getRotation(*sel);
                this->syncHudVisibility();
                return true;
            }

            struct HandleDef { CCPoint pt; DragMode mode; };
            HandleDef handles[8] = {
                { { outline.getMinX(), outline.getMaxY() }, DragMode::ScaleTL },
                { { outline.getMaxX(), outline.getMaxY() }, DragMode::ScaleTR },
                { { outline.getMinX(), outline.getMinY() }, DragMode::ScaleBL },
                { { outline.getMaxX(), outline.getMinY() }, DragMode::ScaleBR },
                { { outline.getMinX(), outline.getMidY() }, DragMode::ScaleL },
                { { outline.getMaxX(), outline.getMidY() }, DragMode::ScaleR },
                { { outline.getMidX(), outline.getMaxY() }, DragMode::ScaleT },
                { { outline.getMidX(), outline.getMinY() }, DragMode::ScaleB },
            };
            for (auto const& h : handles) {
                if (ccpDistanceSQ(wp, h.pt) <= kHandleHitR * kHandleHitR) {
                    m_drag = h.mode;
                    m_dragChanged = false;
                    m_pivotWorld = center;
                    m_touchStart = wp;
                    m_dragStartDist = std::max(8.f, ccpDistance(wp, center));
                    if (sel->isShape) {
                        auto const& sh = m_liveShapes[sel->id];
                        m_dragStartWidth = sh.width;
                        m_dragStartHeight = sh.height;
                    } else {
                        auto const& b = m_liveButtons[sel->id];
                        m_dragStartScaleX = b.scaleX;
                        m_dragStartScaleY = b.scaleY;
                    }
                    this->syncHudVisibility();
                    return true;
                }
            }
        }
    }

    auto* hit = this->findElementAt(wp);
    if (!hit) {
        auto* kd = CCKeyboardDispatcher::get();
        m_marqueeAdd = m_multiSelectMode || (kd && kd->getShiftKeyPressed());
        if (!m_marqueeAdd) {
            this->clearSelection();
        }
        m_drag = DragMode::Marquee;
        m_touchStart = wp;
        m_touchCur = wp;
        m_dragChanged = false;
        this->redraw();
        return true;
    }

    auto* kd = CCKeyboardDispatcher::get();
    bool const multiKey = m_multiSelectMode || (kd && kd->getShiftKeyPressed());
    if (multiKey) {
        this->toggleSelection(hit->id);
    } else if (!this->isSelected(hit->id)) {
        this->selectSingle(hit->id);
    }

    if (this->isSelected(hit->id)) {
        m_drag = DragMode::Move;
        m_dragChanged = false;
        m_touchStart = wp;
        m_touchCur = wp;
        m_dragStartWorldPos.clear();
        std::unordered_set<std::string> groupsToMove;
        for (auto* el : this->selectedElements()) {
            if (el->target.node) {
                m_dragStartWorldPos[el->id] = worldPos(el->target.node);
            }
            auto g = this->getLinkGroup(*el);
            if (!g.empty()) groupsToMove.insert(g);
        }
        for (auto const& elem : m_elements) {
            if (!elem.target.node || m_dragStartWorldPos.contains(elem.id)) continue;
            if (groupsToMove.contains(this->getLinkGroup(elem))) {
                m_dragStartWorldPos[elem.id] = worldPos(elem.target.node);
            }
        }
        this->syncHudVisibility();
    }

    this->rebuildLayersList();
    this->redraw();
    return true;
}

void MainMenuLayoutEditor::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_closing || !m_transitions.empty()) return;
    auto wp = touch->getLocation();
    m_touchCur = wp;

    if (m_drag == DragMode::Marquee) {
        this->redraw();
        return;
    }

    auto* sel = this->primaryElement();
    if (!sel || !sel->target.node || !sel->target.node->getParent()) return;

    if (m_drag == DragMode::Move) {
        CCPoint rawDelta = wp - m_touchStart;
        if (m_selectedIds.size() == 1 && m_dragStartWorldPos.contains(sel->id) && !this->isLocked(*sel)) {
            CCPoint proposed = m_dragStartWorldPos[sel->id] + rawDelta;
            CCPoint snapped = this->snapWorld(*sel, proposed);
            rawDelta = snapped - m_dragStartWorldPos[sel->id];
        }
        for (auto const& [id, startW] : m_dragStartWorldPos) {
            auto* el = this->findElement(id);
            if (!el || this->isLocked(*el) || !el->target.node || !el->target.node->getParent()) continue;
            CCPoint targetW = startW + rawDelta;
            CCPoint local = el->target.node->getParent()->convertToNodeSpace(targetW);
            if (el->isShape) m_liveShapes[el->id].position = local;
            else m_liveButtons[el->id].position = local;
            this->applyElementToScene(*el);
        }
        auto w = worldPos(sel->target.node);
        this->updateLiveBadge(fmt::format("X: {:.0f}   Y: {:.0f}", w.x, w.y), true);
        m_dragChanged = true;
    } else if (m_drag == DragMode::Rotate) {
        float now = CC_RADIANS_TO_DEGREES(std::atan2(wp.y - m_pivotWorld.y, wp.x - m_pivotWorld.x));
        float rot = m_dragStartRot + (m_dragStartAngle - now);
        auto* kd = CCKeyboardDispatcher::get();
        if (kd && kd->getShiftKeyPressed()) rot = std::round(rot / 15.f) * 15.f;
        rot = std::fmod(rot, 360.f);
        if (sel->isShape) m_liveShapes[sel->id].rotation = rot;
        else m_liveButtons[sel->id].rotation = rot;
        this->applyElementToScene(*sel);
        this->updateLiveBadge(fmt::format("{}: {:.0f}", tr("menu_layout.field_rotation"), rot), true);
        m_dragChanged = true;
    } else if (m_drag == DragMode::ScaleTL || m_drag == DragMode::ScaleTR ||
               m_drag == DragMode::ScaleBL || m_drag == DragMode::ScaleBR) {
        float factor = ccpDistance(wp, m_pivotWorld) / m_dragStartDist;
        if (sel->isShape) {
            auto& sh = m_liveShapes[sel->id];
            sh.width = std::clamp(m_dragStartWidth * factor, kMinShapeSize, kMaxShapeSize);
            sh.height = std::clamp(m_dragStartHeight * factor, kMinShapeSize, kMaxShapeSize);
            this->applyElementToScene(*sel);
            this->updateLiveBadge(fmt::format("{:.0f} x {:.0f}", sh.width, sh.height), true);
        } else {
            auto& b = m_liveButtons[sel->id];
            b.scaleX = std::clamp(m_dragStartScaleX * factor, kMinScale, kMaxScale);
            b.scaleY = std::clamp(m_dragStartScaleY * factor, kMinScale, kMaxScale);
            b.scale = (b.scaleX + b.scaleY) * 0.5f;
            this->applyElementToScene(*sel);
            this->updateLiveBadge(fmt::format("{:.0f}%", b.scale * 100.f), true);
        }
        m_dragChanged = true;
    } else if (m_drag == DragMode::ScaleL || m_drag == DragMode::ScaleR) {
        float baseDist = std::max(8.f, std::abs(m_touchStart.x - m_pivotWorld.x));
        float factor = std::max(0.1f, std::abs(wp.x - m_pivotWorld.x) / baseDist);
        if (sel->isShape) {
            auto& sh = m_liveShapes[sel->id];
            sh.width = std::clamp(m_dragStartWidth * factor, kMinShapeSize, kMaxShapeSize);
            this->applyElementToScene(*sel);
            this->updateLiveBadge(fmt::format("W: {:.0f}", sh.width), true);
        } else {
            m_uniformScale = false;
            auto& b = m_liveButtons[sel->id];
            b.scaleX = std::clamp(m_dragStartScaleX * factor, kMinScale, kMaxScale);
            b.scale = (b.scaleX + b.scaleY) * 0.5f;
            this->applyElementToScene(*sel);
            this->updateLiveBadge(fmt::format("X: {:.0f}%", b.scaleX * 100.f), true);
        }
        m_dragChanged = true;
    } else if (m_drag == DragMode::ScaleT || m_drag == DragMode::ScaleB) {
        float baseDist = std::max(8.f, std::abs(m_touchStart.y - m_pivotWorld.y));
        float factor = std::max(0.1f, std::abs(wp.y - m_pivotWorld.y) / baseDist);
        if (sel->isShape) {
            auto& sh = m_liveShapes[sel->id];
            sh.height = std::clamp(m_dragStartHeight * factor, kMinShapeSize, kMaxShapeSize);
            this->applyElementToScene(*sel);
            this->updateLiveBadge(fmt::format("H: {:.0f}", sh.height), true);
        } else {
            m_uniformScale = false;
            auto& b = m_liveButtons[sel->id];
            b.scaleY = std::clamp(m_dragStartScaleY * factor, kMinScale, kMaxScale);
            b.scale = (b.scaleX + b.scaleY) * 0.5f;
            this->applyElementToScene(*sel);
            this->updateLiveBadge(fmt::format("Y: {:.0f}%", b.scaleY * 100.f), true);
        }
        m_dragChanged = true;
    }

    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_guideX) m_guideX->setVisible(false);
    if (m_guideY) m_guideY->setVisible(false);
    this->updateLiveBadge("", false);

    if (m_drag == DragMode::Marquee) {
        CCPoint endPt = touch ? touch->getLocation() : m_touchCur;
        if (ccpDistance(m_touchStart, endPt) > 6.f) {
            CCRect mq(
                std::min(m_touchStart.x, endPt.x),
                std::min(m_touchStart.y, endPt.y),
                std::abs(endPt.x - m_touchStart.x),
                std::abs(endPt.y - m_touchStart.y));
            if (!m_marqueeAdd) m_selectedIds.clear();
            for (auto const& elem : m_elements) {
                if (!elem.target.node || !elem.target.node->getParent()) continue;
                if (this->isBackgroundElement(elem)) continue;
                if (this->isHidden(elem) && !m_settings.showHiddenOutlines) continue;
                if (rectsIntersect(mq, this->elementRect(elem)) && !this->isSelected(elem.id)) {
                    m_selectedIds.push_back(elem.id);
                }
            }
            this->placeInspector();
            this->refreshInspector();
            this->rebuildLayersList();
        }
        m_drag = DragMode::None;
        this->syncHudVisibility();
        this->redraw();
        return;
    }

    bool changed = m_dragChanged && m_drag != DragMode::None;
    m_drag = DragMode::None;
    m_dragChanged = false;
    m_dragStartWorldPos.clear();
    this->syncHudVisibility();
    if (changed) this->pushHistory();
    this->redraw();
}

void MainMenuLayoutEditor::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    this->ccTouchEnded(touch, event);
}

void MainMenuLayoutEditor::keyBackClicked() {
    if (this->hasModalPopupOnTop()) return;
    if (m_previewMode) { this->setPreviewMode(false); return; }
    this->cancelAndClose();
}

void MainMenuLayoutEditor::keyDown(enumKeyCodes key, double) {
    if (m_closing || this->hasModalPopupOnTop()) return;

    auto* kd = CCKeyboardDispatcher::get();
    bool const ctrl = kd && kd->getControlKeyPressed();
    bool const shift = kd && kd->getShiftKeyPressed();

    if (key == enumKeyCodes::KEY_Escape) {
        if (m_previewMode) { this->setPreviewMode(false); return; }
        this->cancelAndClose();
        return;
    }
    if (ctrl && key == enumKeyCodes::KEY_S) { this->saveAndClose(); return; }
    if (ctrl && key == enumKeyCodes::KEY_Z) { if (shift) this->redo(); else this->undo(); return; }
    if (ctrl && key == enumKeyCodes::KEY_Y) { this->redo(); return; }
    if (ctrl && key == enumKeyCodes::KEY_A) { this->selectAllElements(); return; }
    if (!ctrl && key == enumKeyCodes::KEY_P) { this->setPreviewMode(!m_previewMode); return; }
    if (!ctrl && key == enumKeyCodes::KEY_Tab) { this->setLayersOpen(!m_layersOpen); return; }
    if (!ctrl && key == enumKeyCodes::KEY_G) { this->onToggleGrid(nullptr); return; }
    if (!ctrl && key == enumKeyCodes::KEY_S) { this->onToggleSnap(nullptr); return; }

    if (!m_transitions.empty() || m_selectedIds.empty()) return;

    if (ctrl && key == enumKeyCodes::KEY_C) { this->copySelectedStyle(); return; }
    if (ctrl && key == enumKeyCodes::KEY_V) { this->pasteStyleToSelected(); return; }
    if (ctrl && key == enumKeyCodes::KEY_D) { this->duplicateSelectedShape(); return; }
    if (!ctrl && key == enumKeyCodes::KEY_L) { this->toggleSelectionLock(); return; }
    if (!ctrl && key == enumKeyCodes::KEY_H) { this->toggleSelectionHidden(); return; }
    if (key == enumKeyCodes::KEY_Delete || key == enumKeyCodes::KEY_Backspace) {
        if (shift) this->deleteSelectedShapes();
        else this->toggleSelectionHidden();
        return;
    }
    if (!ctrl && key == enumKeyCodes::KEY_C) { this->centerSelection(true, true); return; }

    if (key == enumKeyCodes::KEY_Add || key == enumKeyCodes::KEY_OEMPlus) {
        this->scaleSelection(1.05f);
        this->pushHistory(); this->refreshInspector(); this->redraw();
        return;
    }
    if (key == enumKeyCodes::KEY_Subtract || key == enumKeyCodes::KEY_OEMMinus) {
        this->scaleSelection(1.f / 1.05f);
        this->pushHistory(); this->refreshInspector(); this->redraw();
        return;
    }
    if (key == enumKeyCodes::KEY_LeftBracket || key == enumKeyCodes::KEY_RightBracket) {
        float deg = (key == enumKeyCodes::KEY_LeftBracket ? -1.f : 1.f) * (shift ? 15.f : 5.f);
        this->rotateSelection(deg);
        this->pushHistory(); this->refreshInspector(); this->redraw();
        return;
    }

    float const step = shift ? 10.f : 1.f;
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

    auto* scene = CCDirector::get()->getRunningScene();
    bool attached = false;
    for (CCNode* p = this->getParent(); p; p = p->getParent()) {
        if (p == scene) { attached = true; break; }
    }
    if (!attached) { this->removeFromParent(); return; }

    this->updateAnimations(dt);
    if (m_opacityDirty) {
        m_opacitySettleTimer -= dt;
        if (m_opacitySettleTimer <= 0.f) {
            m_opacityDirty = false;
            this->pushHistory();
        }
    }
    if (m_closing && m_interfaceElapsed >= kCloseDuration && m_transitions.empty()) {
        this->removeFromParent();
        return;
    }
    this->redraw();
}

void MainMenuLayoutEditor::onSave(CCObject*) {
    if (m_closing) return;
    if (this->getTargetRoot()) {
        MainMenuLayoutManager::get().mergeCustomAndShapes(
            m_liveButtons, m_sceneName, this->collectSceneShapesVector());
        PaimonNotify::show(tr("menu_layout.saved"), NotificationIcon::Success);
    }
    this->clearSelection();
    this->beginClose(true);
}

void MainMenuLayoutEditor::onCancel(CCObject*) {
    if (m_closing) return;
    auto doCancel = [this] {
        for (auto const& elem : m_elements) {
            if (elem.isShape) continue;
            auto it = m_initialButtons.find(elem.id);
            if (it == m_initialButtons.end()) continue;
            m_liveButtons[elem.id] = it->second;
            this->applyElementToScene(elem);
        }
        if (auto* root = this->getTargetRoot()) {
            MainMenuLayoutManager::get().syncShapes(root, m_initialShapes);
        }
        this->beginClose(false);
    };

    if (!this->isDirty()) { doCancel(); return; }

    WeakRef<MainMenuLayoutEditor> self = this;
    geode::createQuickPopup(
        tr("menu_layout.cancel").c_str(),
        tr("menu_layout.discard_body").c_str(),
        tr("menu_layout.no").c_str(),
        tr("menu_layout.yes").c_str(),
        [self, doCancel](auto*, bool yes) {
            auto* ed = self.lock().data();
            if (!ed || !ed->getParent() || ed->m_closing || !yes) return;
            doCancel();
        });
}

void MainMenuLayoutEditor::onUndo(CCObject*) { if (!m_closing) this->undo(); }
void MainMenuLayoutEditor::onRedo(CCObject*) { if (!m_closing) this->redo(); }

void MainMenuLayoutEditor::onToggleGrid(CCObject*) {
    if (m_closing) return;
    m_settings.gridOn = !m_settings.gridOn;
    m_settings.save();
    this->rebuildGrid();
    this->refreshHudState();
}

void MainMenuLayoutEditor::onToggleSnap(CCObject*) {
    if (m_closing) return;
    m_settings.snapEnabled = !m_settings.snapEnabled;
    m_settings.save();
    this->refreshHudState();
}

void MainMenuLayoutEditor::onOpenCanvasSettings(CCObject*) {
    if (m_closing) return;
    WeakRef<MainMenuLayoutEditor> self = this;
    studio_ui::openCanvasSettingsPopup(m_settings, [self](CanvasSettings const& next) {
        auto* ed = self.lock().data();
        if (!ed || ed->m_closing) return;
        ed->m_settings = next;
        ed->rebuildGrid();
        ed->redraw();
    });
}

void MainMenuLayoutEditor::onToggleMultiSelect(CCObject*) {
    if (m_closing) return;
    m_multiSelectMode = !m_multiSelectMode;
    this->refreshHudState();
}

void MainMenuLayoutEditor::onToggleShapeBar(CCObject*) {
    if (m_closing) return;
    this->setShapeBarOpen(!m_shapeBarOpen);
}

void MainMenuLayoutEditor::onToggleLayers(CCObject*) {
    if (m_closing) return;
    this->setLayersOpen(!m_layersOpen);
}

void MainMenuLayoutEditor::onOpenPresets(CCObject*) {
    if (m_closing) return;
    WeakRef<MainMenuLayoutEditor> self = this;

    geode::createQuickPopup(
        loc("Layout Slots", "Slots de Layout"),
        loc("Do you want to load a saved slot or save the current layout into a slot?",
            "Quieres cargar un slot guardado o guardar el layout actual en un slot?"),
        tr("menu_layout.load_preset").c_str(),
        tr("menu_layout.save_preset").c_str(),
        [self](auto*, bool saveMode) {
            auto* ed = self.lock().data();
            if (!ed || !ed->getParent() || ed->m_closing) return;

            auto* popup = MainMenuLayoutPresetPopup::create(
                saveMode ? MainMenuLayoutPresetPopup::Mode::Save : MainMenuLayoutPresetPopup::Mode::Load,
                [self, saveMode](int slot) {
                    auto* editor = self.lock().data();
                    if (!editor || !editor->getParent() || editor->m_closing) return;

                    if (saveMode) {
                        MainMenuLayoutPresetManager::get().setPreset(
                            slot, editor->buildFullSnapshot(), "", editor->m_sceneName);
                        PaimonNotify::show(
                            fmt::format(fmt::runtime(tr("menu_layout.preset_saved")), slot + 1),
                            NotificationIcon::Success);
                        return;
                    }

                    auto preset = MainMenuLayoutPresetManager::get().getPreset(slot);
                    if (!preset) {
                        PaimonNotify::show(tr("menu_layout.presets_empty_slot"), NotificationIcon::Warning);
                        return;
                    }
                    editor->applyHistory(preset->snapshot);
                    editor->pushHistory();
                    editor->clearSelection();
                    editor->redraw();
                    PaimonNotify::show(
                        fmt::format(fmt::runtime(tr("menu_layout.preset_loaded")), slot + 1),
                        NotificationIcon::Success);
                },
                ed->m_sceneName);
            if (popup) popup->show();
        });
}

void MainMenuLayoutEditor::onResetAll(CCObject*) {
    if (m_closing) return;
    WeakRef<MainMenuLayoutEditor> self = this;
    geode::createQuickPopup(
        tr("menu_layout.reset_all").c_str(),
        tr("menu_layout.reset_all_body").c_str(),
        tr("menu_layout.no").c_str(),
        tr("menu_layout.yes").c_str(),
        [self](auto*, bool yes) {
            auto* ed = self.lock().data();
            if (!ed || !ed->getParent() || ed->m_closing || !yes) return;
            for (auto& elem : ed->m_elements) {
                if (!elem.isShape) ed->resetElementToDefault(elem);
            }
            ed->m_liveShapes.clear();
            ed->m_shapeOrder.clear();
            if (auto* root = ed->getTargetRoot()) {
                MainMenuLayoutManager::get().syncShapes(root, {});
            }
            ed->rebuildShapeElements();
            ed->clearSelection();
            ed->pushHistory();
            ed->rebuildLayersList();
            ed->redraw();
            PaimonNotify::show(tr("menu_layout.reset_done"), NotificationIcon::Info);
        });
}

void MainMenuLayoutEditor::onTogglePreview(CCObject*) {
    if (!m_closing) this->setPreviewMode(!m_previewMode);
}

void MainMenuLayoutEditor::onToggleDock(CCObject*) {
    if (!m_closing) this->setCollapsed(!m_collapsed);
}

void MainMenuLayoutEditor::onDeselect(CCObject*) {
    if (m_closing) return;
    this->clearSelection();
    this->rebuildLayersList();
    this->redraw();
}

void MainMenuLayoutEditor::onOpacityChanged(CCObject*) {
    if (m_closing || !m_transitions.empty() || !m_opacitySlider) return;
    auto* el = this->primaryElement();
    if (!el || this->isLocked(*el)) return;
    this->setSelectionOpacity(m_opacitySlider->getValue());
    m_opacityDirty = true;
    m_opacitySettleTimer = 0.35f;
    this->refreshInspector();
    this->redraw();
}

void MainMenuLayoutEditor::saveAndClose() { this->onSave(nullptr); }
void MainMenuLayoutEditor::cancelAndClose() { this->onCancel(nullptr); }

} // namespace paimon::menu_layout
