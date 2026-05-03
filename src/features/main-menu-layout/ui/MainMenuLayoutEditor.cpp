#include "MainMenuLayoutEditor.hpp"

#include "MainMenuDrawShapeNode.hpp"
#include "MainMenuLayoutPresetPopup.hpp"

#include "../services/MainMenuLayoutPresetManager.hpp"

#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cfloat>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    MainMenuLayoutEditor* s_activeEditor = nullptr;

    constexpr float kMinButtonHitSize = 26.f;
    constexpr float kOutlinePad = 6.f;
    constexpr float kGripSize = 16.f;
    constexpr float kGripHitRadius = 18.f;
    constexpr float kMinScale = 0.25f;
    constexpr float kMaxScale = 4.0f;
    constexpr float kMinAxisScale = 0.2f;
    constexpr float kScaleButtonFactor = 1.035f;
    constexpr float kOpacityStep = 0.1f;
    constexpr float kPanelAnimDuration = 0.18f;
    constexpr std::size_t kHistoryLimit = 64;
    constexpr float kHistoryPositionEpsilon = 0.05f;
    constexpr float kHistoryScaleEpsilon = 0.001f;

    CCPoint worldPosition(CCNode* node) {
        if (!node || !node->getParent()) return { 0.f, 0.f };
        return node->getParent()->convertToWorldSpace(node->getPosition());
    }

    CCRect normalizedRect(CCRect rect) {
        if (rect.size.width < 0.f) {
            rect.origin.x += rect.size.width;
            rect.size.width *= -1.f;
        }
        if (rect.size.height < 0.f) {
            rect.origin.y += rect.size.height;
            rect.size.height *= -1.f;
        }
        return rect;
    }

    void drawRect(CCDrawNode* node, CCRect rect, ccColor4F color, float thickness) {
        rect = normalizedRect(rect);
        auto minX = rect.getMinX();
        auto minY = rect.getMinY();
        auto maxX = rect.getMaxX();
        auto maxY = rect.getMaxY();

        node->drawSegment({ minX, minY }, { maxX, minY }, thickness, color);
        node->drawSegment({ maxX, minY }, { maxX, maxY }, thickness, color);
        node->drawSegment({ maxX, maxY }, { minX, maxY }, thickness, color);
        node->drawSegment({ minX, maxY }, { minX, minY }, thickness, color);
    }

    void applyButtonScale(CCMenuItem* item, float scale) {
        if (!item) return;
        item->setScale(scale);
        if (auto* spriteExtra = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
            spriteExtra->m_baseScale = scale;
        }
    }

    void applyNodeScale(CCNode* node, float scale) {
        if (!node) return;
        node->setScale(scale);
        if (auto* item = typeinfo_cast<CCMenuItem*>(node)) {
            applyButtonScale(item, scale);
        }
    }

    void scaleSpriteToFit(CCSprite* sprite, float maxWidth, float maxHeight) {
        if (!sprite) return;

        auto size = sprite->getContentSize();
        auto width = std::max(1.f, size.width);
        auto height = std::max(1.f, size.height);
        auto scale = std::min(maxWidth / width, maxHeight / height);
        sprite->setScale(std::clamp(scale, 0.1f, 1.f));
    }

    CCSprite* loadFirstFrame(std::initializer_list<char const*> frames) {
        for (auto const* frame : frames) {
            if (!frame || !*frame) continue;
            if (auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
                return sprite;
            }
        }
        return nullptr;
    }

    float nodeVisualWidth(CCNode* node) {
        if (!node) return 0.f;

        auto width = std::abs(node->boundingBox().size.width);
        if (width <= 0.f) {
            width = node->getContentSize().width * std::abs(node->getScaleX());
        }
        return width;
    }

    float nodeVisualHeight(CCNode* node) {
        if (!node) return 0.f;

        auto height = std::abs(node->boundingBox().size.height);
        if (height <= 0.f) {
            height = node->getContentSize().height * std::abs(node->getScaleY());
        }
        return height;
    }

    bool menuContainsTouch(CCMenu* menu, CCPoint worldPos) {
        if (!menu || !menu->isVisible()) return false;

        auto* children = menu->getChildren();
        if (!children) return false;

        for (auto* child : CCArrayExt<CCNode*>(children)) {
            auto* item = typeinfo_cast<CCMenuItem*>(child);
            if (!item || !item->isVisible() || !item->getParent()) continue;

            auto localPos = item->getParent()->convertToNodeSpace(worldPos);
            if (item->boundingBox().containsPoint(localPos)) {
                return true;
            }
        }

        return false;
    }

    bool nodeContainsInteractiveTouch(CCNode* node, CCPoint worldPos) {
        if (!node || !node->isVisible()) return false;

        if (auto* menu = typeinfo_cast<CCMenu*>(node)) {
            return menuContainsTouch(menu, worldPos);
        }

        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (nodeContainsInteractiveTouch(child, worldPos)) {
                    return true;
                }
            }
        }

        return false;
    }

    bool isShapeNode(CCNode* node) {
        return typeinfo_cast<MainMenuDrawShapeNode*>(node) != nullptr;
    }

    bool nearlyEqual(float a, float b, float epsilon) {
        return std::abs(a - b) <= epsilon;
    }

    bool layoutsEqual(MenuButtonLayout const& lhs, MenuButtonLayout const& rhs) {
        return nearlyEqual(lhs.position.x, rhs.position.x, kHistoryPositionEpsilon) &&
               nearlyEqual(lhs.position.y, rhs.position.y, kHistoryPositionEpsilon) &&
               nearlyEqual(lhs.scale, rhs.scale, kHistoryScaleEpsilon) &&
               nearlyEqual(lhs.scaleX, rhs.scaleX, kHistoryScaleEpsilon) &&
               nearlyEqual(lhs.scaleY, rhs.scaleY, kHistoryScaleEpsilon) &&
               nearlyEqual(lhs.opacity, rhs.opacity, kHistoryScaleEpsilon) &&
               lhs.hidden == rhs.hidden &&
               lhs.layer == rhs.layer &&
               lhs.linkGroup == rhs.linkGroup &&
               lhs.hasColor == rhs.hasColor &&
               lhs.color.r == rhs.color.r &&
               lhs.color.g == rhs.color.g &&
               lhs.color.b == rhs.color.b &&
               lhs.fontFile == rhs.fontFile;
    }

    bool shapesEqual(DrawShapeLayout const& lhs, DrawShapeLayout const& rhs) {
        return lhs.id == rhs.id &&
               lhs.kind == rhs.kind &&
               nearlyEqual(lhs.position.x, rhs.position.x, kHistoryPositionEpsilon) &&
               nearlyEqual(lhs.position.y, rhs.position.y, kHistoryPositionEpsilon) &&
               nearlyEqual(lhs.scale, rhs.scale, kHistoryScaleEpsilon) &&
               nearlyEqual(lhs.scaleX, rhs.scaleX, kHistoryScaleEpsilon) &&
               nearlyEqual(lhs.scaleY, rhs.scaleY, kHistoryScaleEpsilon) &&
               nearlyEqual(lhs.opacity, rhs.opacity, kHistoryScaleEpsilon) &&
               lhs.hidden == rhs.hidden &&
               nearlyEqual(lhs.width, rhs.width, kHistoryPositionEpsilon) &&
               nearlyEqual(lhs.height, rhs.height, kHistoryPositionEpsilon) &&
               nearlyEqual(lhs.cornerRadius, rhs.cornerRadius, kHistoryPositionEpsilon) &&
               lhs.color.r == rhs.color.r &&
               lhs.color.g == rhs.color.g &&
               lhs.color.b == rhs.color.b &&
               lhs.zOrder == rhs.zOrder &&
               lhs.layer == rhs.layer &&
               lhs.linkGroup == rhs.linkGroup;
    }

    bool snapshotsEqual(LayoutSnapshot const& lhs, LayoutSnapshot const& rhs) {
        if (lhs.buttons.size() != rhs.buttons.size() || lhs.shapes.size() != rhs.shapes.size()) {
            return false;
        }

        for (auto const& [key, layout] : lhs.buttons) {
            auto it = rhs.buttons.find(key);
            if (it == rhs.buttons.end() || !layoutsEqual(layout, it->second)) {
                return false;
            }
        }

        for (std::size_t i = 0; i < lhs.shapes.size(); ++i) {
            if (!shapesEqual(lhs.shapes[i], rhs.shapes[i])) {
                return false;
            }
        }

        return true;
    }

    void setMenuItemEnabledVisual(CCMenuItemSpriteExtra* item, bool enabled) {
        if (!item) return;
        item->setEnabled(enabled);
        item->setOpacity(enabled ? 255 : 96);
    }

    CCPoint rectAnchor(CCRect const& rect, MainMenuLayoutEditor::ResizeHandle handle) {
        switch (handle) {
            case MainMenuLayoutEditor::ResizeHandle::TopLeft: return { rect.getMinX(), rect.getMaxY() };
            case MainMenuLayoutEditor::ResizeHandle::TopRight: return { rect.getMaxX(), rect.getMaxY() };
            case MainMenuLayoutEditor::ResizeHandle::BottomLeft: return { rect.getMinX(), rect.getMinY() };
            case MainMenuLayoutEditor::ResizeHandle::BottomRight: return { rect.getMaxX(), rect.getMinY() };
            case MainMenuLayoutEditor::ResizeHandle::None: break;
        }
        return { rect.getMidX(), rect.getMidY() };
    }

    CCPoint oppositeAnchor(CCRect const& rect, MainMenuLayoutEditor::ResizeHandle handle) {
        switch (handle) {
            case MainMenuLayoutEditor::ResizeHandle::TopLeft: return { rect.getMaxX(), rect.getMinY() };
            case MainMenuLayoutEditor::ResizeHandle::TopRight: return { rect.getMinX(), rect.getMinY() };
            case MainMenuLayoutEditor::ResizeHandle::BottomLeft: return { rect.getMaxX(), rect.getMaxY() };
            case MainMenuLayoutEditor::ResizeHandle::BottomRight: return { rect.getMinX(), rect.getMaxY() };
            case MainMenuLayoutEditor::ResizeHandle::None: break;
        }
        return { rect.getMidX(), rect.getMidY() };
    }

    CCNode* makeGrip(ccColor4B color, char const* symbol) {
        auto* gripNode = CCNode::create();
        gripNode->setAnchorPoint({ 0.5f, 0.5f });
        gripNode->setContentSize({ kGripSize, kGripSize });
        gripNode->setVisible(false);

        auto* body = CCLayerColor::create(color);
        body->ignoreAnchorPointForPosition(false);
        body->setAnchorPoint({ 0.5f, 0.5f });
        body->setContentSize({ kGripSize, kGripSize });
        body->setPosition({ 0.f, 0.f });
        gripNode->addChild(body);

        auto* label = CCLabelBMFont::create(symbol, "goldFont.fnt");
        label->setScale(0.38f);
        label->setPosition({ 0.f, 0.f });
        gripNode->addChild(label);

        return gripNode;
    }
}

MainMenuLayoutEditor* MainMenuLayoutEditor::create(MenuLayer* layer) {
    auto* editor = new MainMenuLayoutEditor();
    if (editor && editor->init(layer)) {
        editor->autorelease();
        return editor;
    }
    delete editor;
    return nullptr;
}

MainMenuLayoutEditor* MainMenuLayoutEditor::getActive() {
    return s_activeEditor;
}

bool MainMenuLayoutEditor::isActive() {
    return s_activeEditor != nullptr;
}

void MainMenuLayoutEditor::open(MenuLayer* layer) {
    if (!layer || s_activeEditor) return;

    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    auto* editor = MainMenuLayoutEditor::create(layer);
    if (!editor) return;

    s_activeEditor = editor;
    scene->addChild(editor, INT_MAX - 10);
}

void MainMenuLayoutEditor::toggle(MenuLayer* layer) {
    if (!layer) return;

    if (s_activeEditor) {
        if (s_activeEditor->getTargetLayer() == layer) {
            s_activeEditor->saveAndClose();
        }
        return;
    }

    MainMenuLayoutEditor::open(layer);
}

MenuLayer* MainMenuLayoutEditor::getTargetLayer() const {
    auto ref = m_layer.lock();
    return static_cast<MenuLayer*>(ref.data());
}

MainMenuLayoutEditor::~MainMenuLayoutEditor() {
    for (auto& menu : m_disabledMenus) {
        if (menu && menu->getParent()) {
            menu->setEnabled(true);
        }
    }
    m_disabledMenus.clear();

    if (s_activeEditor == this) {
        s_activeEditor = nullptr;
    }
}

bool MainMenuLayoutEditor::init(MenuLayer* layer) {
    if (!CCLayer::init()) return false;

    m_layer = layer;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(winSize);
    this->setAnchorPoint({ 0.f, 0.f });
    this->setPosition({ 0.f, 0.f });
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setKeypadEnabled(true);
    this->scheduleUpdate();

    auto* dark = CCLayerColor::create({ 0, 0, 0, 130 });
    dark->setContentSize(winSize);
    this->addChild(dark, -1);

    m_allHighlights = CCDrawNode::create();
    this->addChild(m_allHighlights, 10);

    m_selectionOutline = CCDrawNode::create();
    this->addChild(m_selectionOutline, 20);

    m_guideX = CCDrawNode::create();
    m_guideX->setVisible(false);
    this->addChild(m_guideX, 15);

    m_guideY = CCDrawNode::create();
    m_guideY->setVisible(false);
    this->addChild(m_guideY, 15);

    auto* gripNode = CCNode::create();
    gripNode->setAnchorPoint({ 0.5f, 0.5f });
    gripNode->setContentSize({ kGripSize, kGripSize });
    gripNode->setVisible(false);

    auto* gripBody = CCLayerColor::create({ 70, 255, 130, 255 });
    gripBody->ignoreAnchorPointForPosition(false);
    gripBody->setAnchorPoint({ 0.5f, 0.5f });
    gripBody->setContentSize({ kGripSize, kGripSize });
    gripBody->setPosition({ 0.f, 0.f });
    gripNode->addChild(gripBody);

    auto* gripLines = CCDrawNode::create();
    gripLines->drawSegment({ -4.f, -1.f }, { 1.f, -6.f }, 1.2f, { 1.f, 1.f, 1.f, 0.9f });
    gripLines->drawSegment({ -1.f, 2.f }, { 4.f, -3.f }, 1.2f, { 1.f, 1.f, 1.f, 0.9f });
    gripLines->drawSegment({ 2.f, 5.f }, { 7.f, 0.f }, 1.2f, { 1.f, 1.f, 1.f, 0.9f });
    gripNode->addChild(gripLines);

    m_scaleGrip = gripNode;
    this->addChild(m_scaleGrip, 25);

    m_scaleGripTopLeft = makeGrip({ 70, 255, 130, 255 }, "\\");
    this->addChild(m_scaleGripTopLeft, 25);

    m_scaleGripTopRight = makeGrip({ 70, 255, 130, 255 }, "/");
    this->addChild(m_scaleGripTopRight, 25);

    m_scaleGripBottomLeft = makeGrip({ 70, 255, 130, 255 }, "/");
    this->addChild(m_scaleGripBottomLeft, 25);

    m_deleteGrip = makeGrip({ 255, 90, 90, 255 }, "X");
    this->addChild(m_deleteGrip, 25);

    m_opacityDownGrip = makeGrip({ 80, 220, 255, 255 }, "-");
    this->addChild(m_opacityDownGrip, 25);

    m_opacityUpGrip = makeGrip({ 80, 220, 255, 255 }, "+");
    this->addChild(m_opacityUpGrip, 25);

    this->buildUI();
    this->collectButtons();
    this->disableTargetMenus();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
    this->commitHistorySnapshot();

    return true;
}

void MainMenuLayoutEditor::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -INT_MAX + 200, true);
}

void MainMenuLayoutEditor::collectButtons() {
    auto oldLiveLayouts = m_liveLayouts;
    auto selectedKey = m_selected ? m_selected->target.key : std::string();
    m_buttons.clear();
    m_liveLayouts.clear();
    m_selected = nullptr;

    auto* layer = this->getTargetLayer();
    if (!layer) {
        this->refreshRightPanel();
        return;
    }

    auto preserveEditorShapes = !m_initialShapes.empty() || !m_shapeLayouts.empty();

    MainMenuLayoutManager::get().captureDefaultsAndApply(layer);
    if (preserveEditorShapes) {
        MainMenuLayoutManager::get().syncShapes(layer, m_shapeLayouts);
    }

    for (auto const& button : MainMenuLayoutManager::get().collectButtons(layer)) {
        if (!button.node || !button.node->getParent()) continue;

        auto layout = MainMenuLayoutManager::readLayout(button.node);
        if (auto it = oldLiveLayouts.find(button.key); it != oldLiveLayouts.end()) {
            layout = it->second;
            MainMenuLayoutManager::applyLayout(button.node, layout);
        }
        m_liveLayouts[button.key] = layout;
        if (m_initialLiveLayouts.find(button.key) == m_initialLiveLayouts.end()) {
            m_initialLiveLayouts[button.key] = layout;
        }

        m_buttons.push_back({
            button,
            layout,
            layout.opacity,
            layout.hidden,
        });

        if (button.key == selectedKey) {
            m_selected = &m_buttons.back();
        }
    }

    for (auto const& shapeNode : MainMenuLayoutManager::get().collectShapeNodes(layer)) {
        if (!shapeNode.node || !shapeNode.node->getParent()) continue;

        auto layout = MainMenuLayoutManager::readLayout(shapeNode.node);
        if (auto it = oldLiveLayouts.find(shapeNode.key); it != oldLiveLayouts.end()) {
            layout.linkGroup = it->second.linkGroup;
        }
        m_liveLayouts[shapeNode.key] = layout;
        if (m_initialLiveLayouts.find(shapeNode.key) == m_initialLiveLayouts.end()) {
            m_initialLiveLayouts[shapeNode.key] = layout;
        }
        m_buttons.push_back({
            shapeNode,
            layout,
            layout.opacity,
            layout.hidden,
        });

        if (shapeNode.key == selectedKey) {
            m_selected = &m_buttons.back();
        }
    }

    m_shapeLayouts = MainMenuLayoutManager::captureShapes(layer);
    if (m_initialShapes.empty()) {
        m_initialShapes = m_shapeLayouts;
    }

    this->refreshRightPanel();
}

void MainMenuLayoutEditor::disableTargetMenus() {
    m_disabledMenus.clear();

    std::unordered_set<CCMenu*> seen;
    for (auto const& button : m_buttons) {
        auto* menu = button.target.menu;
        if (!menu || seen.contains(menu) || !menu->isEnabled()) continue;

        seen.insert(menu);
        menu->setEnabled(false);
        m_disabledMenus.emplace_back(menu);
    }
}

void MainMenuLayoutEditor::buildUI() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    constexpr float kOuterMargin = 8.f;
    constexpr float kRightPanelVisiblePadding = 6.f;
    constexpr float kRightTabWidth = 26.f;
    constexpr float kRightTabHeight = 54.f;
    constexpr float kTopPanelRadius = 10.f;
    constexpr float kBottomPanelRadius = 8.f;

    m_rightPanelWidth = 160.f;
    float workLeft = kOuterMargin;
    float workWidth = winSize.width - m_rightPanelWidth - (kOuterMargin * 3.f);
    float workCenterX = workLeft + workWidth * 0.5f;

    m_topPanel = CCNode::create();
    m_topPanel->setPosition({ 0.f, 0.f });
    this->addChild(m_topPanel, 30);

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(0.48f);
    m_statusLabel->setPosition({ workCenterX, 24.f });
    this->addChild(m_statusLabel, 31);

    m_toolbar = CCMenu::create();
    m_toolbar->setPosition({ 0.f, 0.f });
    m_topPanel->addChild(m_toolbar, 3);

    auto makeButton = [this](char const* key, SEL_MenuHandler callback, char const* bg, int width = 60) {
        auto* sprite = ButtonSprite::create(
            Localization::get().getString(key).c_str(),
            width,
            true,
            "goldFont.fnt",
            bg,
            20.f,
            0.40f
        );
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        m_toolbar->addChild(button);
        return button;
    };

    // --- Create all top-bar buttons (compact widths) ---
    auto* cancelBtn = makeButton("menu_layout.cancel", menu_selector(MainMenuLayoutEditor::onCancel), "GJ_button_06.png", 56);
    auto* saveBtn = makeButton("menu_layout.save", menu_selector(MainMenuLayoutEditor::onSave), "GJ_button_02.png", 56);
    auto* savePresetBtn = makeButton("menu_layout.save_preset", menu_selector(MainMenuLayoutEditor::onSavePreset), "GJ_button_02.png", 68);
    auto* loadPresetBtn = makeButton("menu_layout.load_preset", menu_selector(MainMenuLayoutEditor::onLoadPreset), "GJ_button_01.png", 68);
    auto* addRectBtn = makeButton("menu_layout.draw_add_rect", menu_selector(MainMenuLayoutEditor::onAddRect), "GJ_button_05.png", 56);
    auto* addRoundBtn = makeButton("menu_layout.draw_add_round", menu_selector(MainMenuLayoutEditor::onAddRound), "GJ_button_04.png", 60);
    auto* addCircleBtn = makeButton("menu_layout.draw_add_circle", menu_selector(MainMenuLayoutEditor::onAddCircle), "GJ_button_03.png", 56);
    auto* resetSelectedBtn = makeButton("menu_layout.reset_selected", menu_selector(MainMenuLayoutEditor::onResetSelected), "GJ_button_01.png", 68);

    // --- Store all toolbar buttons for paging ---
    m_toolbarButtons = {
        cancelBtn, saveBtn, savePresetBtn, loadPresetBtn,
        addRectBtn, addRoundBtn, addCircleBtn, resetSelectedBtn
    };

    // --- Top panel dimensions ---
    float btnRowHeight = 0.f;
    for (auto* b : m_toolbarButtons)
        btnRowHeight = std::max(btnRowHeight, nodeVisualHeight(b));

    float topPanelHeight = 16.f + 16.f + 6.f + btnRowHeight + 10.f;
    float topPanelY = winSize.height - topPanelHeight - kOuterMargin;

    // --- Rounded top panel background ---
    if (auto* topBg = paimon::SpriteHelper::createDarkPanel(workWidth, topPanelHeight, 195, kTopPanelRadius)) {
        topBg->setPosition({ workLeft, topPanelY });
        m_topPanel->addChild(topBg);
    }

    // --- Rounded bottom status bar ---
    if (auto* bottomBg = paimon::SpriteHelper::createDarkPanel(workWidth, 36.f, 185, kBottomPanelRadius)) {
        bottomBg->setPosition({ workLeft, 8.f });
        this->addChild(bottomBg, 30);
    }

    // --- Title ---
    auto* title = CCLabelBMFont::create(Localization::get().getString("menu_layout.title").c_str(), "goldFont.fnt");
    title->setScale(0.50f);
    float titleY = topPanelY + topPanelHeight - 14.f;
    title->setPosition({ workCenterX, titleY });
    m_topPanel->addChild(title, 2);

    // --- Accent line under title ---
    auto* accentLine = paimon::SpriteHelper::createRoundedRect(
        workWidth * 0.55f, 1.5f, 1.f,
        { 0.45f, 0.85f, 1.f, 0.35f }
    );
    if (accentLine) {
        accentLine->setPosition({ workLeft + workWidth * 0.225f, titleY - 10.f });
        m_topPanel->addChild(accentLine, 2);
    }

    // --- Compact hint ---
    m_hintLabel = CCLabelBMFont::create("Arrastra=mover | Grip=escala | +/-=opacidad | Esc=cancelar", "chatFont.fnt");
    m_hintLabel->setScale(0.34f);
    m_hintLabel->setColor({ 180, 200, 220 });
    float hintY = titleY - 18.f;
    m_hintLabel->setPosition({ workCenterX, hintY });
    m_topPanel->addChild(m_hintLabel, 2);

    // --- Paged horizontal toolbar: show 3 buttons at a time with arrows ---
    constexpr float kArrowPad = 32.f;
    float collapseInset = 36.f;
    m_toolbarBtnRowY = hintY - 8.f - btnRowHeight * 0.5f;
    m_toolbarBtnStartX = workLeft + collapseInset + kArrowPad;
    m_toolbarBtnAreaWidth = workWidth - collapseInset - 16.f - kArrowPad * 2.f;
    m_toolbarVisibleCount = 3;
    m_toolbarPage = 0;

    // Left arrow
    if (auto* leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        leftSpr->setScale(0.55f);
        m_toolbarPrevBtn = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(MainMenuLayoutEditor::onToolbarPrev));
        m_toolbarPrevBtn->setPosition({ workLeft + collapseInset + 10.f, m_toolbarBtnRowY });
        m_toolbar->addChild(m_toolbarPrevBtn);
    }

    // Right arrow
    if (auto* rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        rightSpr->setFlipX(true);
        rightSpr->setScale(0.55f);
        m_toolbarNextBtn = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(MainMenuLayoutEditor::onToolbarNext));
        m_toolbarNextBtn->setPosition({ workLeft + workWidth - 22.f, m_toolbarBtnRowY });
        m_toolbar->addChild(m_toolbarNextBtn);
    }

    // Page indicator
    m_toolbarPageLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_toolbarPageLabel->setScale(0.28f);
    m_toolbarPageLabel->setColor({ 160, 200, 240 });
    m_toolbarPageLabel->setPosition({ workCenterX, m_toolbarBtnRowY - btnRowHeight * 0.5f - 6.f });
    m_topPanel->addChild(m_toolbarPageLabel, 4);

    // Initial page layout
    this->updateToolbarPage();

    // --- Collapse tab (rounded) ---
    if (auto* collapseTabBg = paimon::SpriteHelper::createDarkPanel(28.f, 38.f, 210, 6.f)) {
        collapseTabBg->setPosition({ workLeft + 5.f, topPanelY + topPanelHeight * 0.5f - 19.f });
        m_topPanel->addChild(collapseTabBg, 2);
    }

    if (auto* collapseSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        collapseSpr->setRotation(90.f);
        collapseSpr->setScale(0.45f);
        auto* collapseBtn = CCMenuItemSpriteExtra::create(collapseSpr, this, menu_selector(MainMenuLayoutEditor::onToggleTopPanel));
        collapseBtn->setPosition({ workLeft + 19.f, topPanelY + topPanelHeight * 0.5f });
        m_toolbar->addChild(collapseBtn);
    }

    // --- Collapsed state ---
    m_topPanelCollapsed = CCNode::create();
    m_topPanelCollapsed->setVisible(false);
    this->addChild(m_topPanelCollapsed, 31);

    if (auto* collBg = paimon::SpriteHelper::createDarkPanel(32.f, 36.f, 190, 6.f)) {
        collBg->setPosition({ workLeft + 3.f, topPanelY + topPanelHeight * 0.5f - 18.f });
        m_topPanelCollapsed->addChild(collBg);
    }

    auto* collapsedMenu = CCMenu::create();
    collapsedMenu->setPosition({ 0.f, 0.f });
    m_topPanelCollapsed->addChild(collapsedMenu, 2);
    if (auto* downSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        downSpr->setRotation(270.f);
        downSpr->setScale(0.45f);
        auto* downBtn = CCMenuItemSpriteExtra::create(downSpr, this, menu_selector(MainMenuLayoutEditor::onToggleTopPanel));
        downBtn->setPosition({ workLeft + 19.f, topPanelY + topPanelHeight * 0.5f });
        collapsedMenu->addChild(downBtn);
    }

    m_rightPanelShownX = winSize.width - m_rightPanelWidth - kOuterMargin;
    m_rightPanelHiddenX = winSize.width - kRightPanelVisiblePadding;

    m_rightPanel = CCNode::create();
    m_rightPanel->setPosition({ m_rightPanelShownX, 0.f });
    this->addChild(m_rightPanel, 32);
    if (auto* rightBg = paimon::SpriteHelper::createDarkPanel(m_rightPanelWidth, winSize.height - 16.f, 195, 10.f)) {
        rightBg->setPosition({ 0.f, 8.f });
        m_rightPanel->addChild(rightBg);
    }

    m_rightMenu = CCMenu::create();
    m_rightMenu->setPosition({ 0.f, 0.f });
    m_rightPanel->addChild(m_rightMenu, 3);

    float rightCenterX = m_rightPanelWidth * 0.5f;
    auto makeRightIconButton = [this](std::initializer_list<char const*> frames,
                                      SEL_MenuHandler callback,
                                      std::string const& caption,
                                      float iconMaxWidth,
                                      float iconMaxHeight,
                                      char const* fallbackText,
                                      bool flipX = false,
                                      bool flipY = false) {
        constexpr float kCardWidth = 38.f;
        constexpr float kCardHeight = 38.f;

        auto* container = CCNode::create();
        container->setAnchorPoint({ 0.5f, 0.5f });
        container->setContentSize({ kCardWidth, kCardHeight });

        if (auto* cardBg = paimon::SpriteHelper::createDarkPanel(kCardWidth, kCardHeight, 132, 6.f)) {
            cardBg->setPosition({ 0.f, 0.f });
            container->addChild(cardBg, 1);
        }

        float iconCap = std::min(iconMaxWidth, kCardWidth - 6.f);
        float iconCapH = std::min(iconMaxHeight, kCardHeight - (caption.empty() ? 6.f : 14.f));
        if (auto* icon = loadFirstFrame(frames)) {
            icon->setFlipX(flipX);
            icon->setFlipY(flipY);
            scaleSpriteToFit(icon, iconCap, iconCapH);
            icon->setPosition({ kCardWidth * 0.5f, caption.empty() ? kCardHeight * 0.5f : 22.f });
            container->addChild(icon, 3);
        } else {
            auto* fallback = CCLabelBMFont::create(fallbackText, "goldFont.fnt");
            fallback->setScale(caption.empty() ? 0.38f : 0.34f);
            fallback->setPosition({ kCardWidth * 0.5f, caption.empty() ? kCardHeight * 0.5f : 22.f });
            container->addChild(fallback, 3);
        }

        if (!caption.empty()) {
            auto* label = CCLabelBMFont::create(caption.c_str(), "chatFont.fnt");
            float scale = 0.26f;
            if (caption.size() >= 8) {
                scale = 0.18f;
            } else if (caption.size() >= 6) {
                scale = 0.22f;
            }
            label->setScale(scale);
            label->setColor({ 220, 220, 220 });
            label->setPosition({ kCardWidth * 0.5f, 5.f });
            container->addChild(label, 3);
        }

        auto* button = CCMenuItemSpriteExtra::create(container, this, callback);
        m_rightMenu->addChild(button);
        return button;
    };

    auto makeLayerButton = [this](std::initializer_list<char const*> frames,
                                  SEL_MenuHandler callback,
                                  char const* fallbackText,
                                  bool flipX = false,
                                  bool flipY = false) {
        constexpr float kTouchWidth = 30.f;
        constexpr float kTouchHeight = 24.f;

        auto* container = CCNode::create();
        container->setAnchorPoint({ 0.5f, 0.5f });
        container->setContentSize({ kTouchWidth, kTouchHeight });

        if (auto* cardBg = paimon::SpriteHelper::createDarkPanel(kTouchWidth, kTouchHeight, 132, 6.f)) {
            cardBg->setPosition({ 0.f, 0.f });
            container->addChild(cardBg, 1);
        }

        if (auto* icon = loadFirstFrame(frames)) {
            icon->setFlipX(flipX);
            icon->setFlipY(flipY);
            scaleSpriteToFit(icon, 17.f, 17.f);
            icon->setPosition({ kTouchWidth * 0.5f, kTouchHeight * 0.5f });
            container->addChild(icon, 3);
        } else {
            auto* fallback = CCLabelBMFont::create(fallbackText, "goldFont.fnt");
            fallback->setScale(0.437f);
            fallback->setPosition({ kTouchWidth * 0.5f, kTouchHeight * 0.5f });
            container->addChild(fallback, 3);
        }

        auto* button = CCMenuItemSpriteExtra::create(container, this, callback);
        m_rightMenu->addChild(button);
        return button;
    };

    auto* rightTitle = CCLabelBMFont::create(Localization::get().getString("menu_layout.layers_title").c_str(), "goldFont.fnt");
    rightTitle->setScale(0.36f);
    rightTitle->setPosition({ rightCenterX, winSize.height - 24.f });
    m_rightPanel->addChild(rightTitle, 4);

    m_undoButton = makeLayerButton(
        { "GJ_updateBtn_001.png", "GJ_replayBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onUndo),
        "<",
        true,
        false
    );
    m_undoButton->setPosition({ rightCenterX - 44.f, winSize.height - 24.f });

    m_redoButton = makeLayerButton(
        { "GJ_updateBtn_001.png", "GJ_replayBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onRedo),
        ">"
    );
    m_redoButton->setPosition({ rightCenterX + 44.f, winSize.height - 24.f });

    m_selectionCountLabel = CCLabelBMFont::create("0 sel", "chatFont.fnt");
    m_selectionCountLabel->setScale(0.44f);
    m_selectionCountLabel->setPosition({ rightCenterX, winSize.height - 42.f });
    m_rightPanel->addChild(m_selectionCountLabel, 4);

    auto* layerLabel = CCLabelBMFont::create(Localization::get().getString("menu_layout.layer").c_str(), "goldFont.fnt");
    layerLabel->setScale(0.34f);
    layerLabel->setPosition({ rightCenterX, winSize.height - 64.f });
    m_rightPanel->addChild(layerLabel, 4);

    auto* layerRowBg = paimon::SpriteHelper::createDarkPanel(m_rightPanelWidth - 20.f, 28.f, 95, 7.f);
    layerRowBg->setPosition({ 10.f, winSize.height - 105.f });
    m_rightPanel->addChild(layerRowBg, 2);

    m_layerValueLabel = CCLabelBMFont::create("-", "chatFont.fnt");
    m_layerValueLabel->setScale(0.44f);
    m_layerValueLabel->setPosition({ rightCenterX, winSize.height - 91.f });
    m_rightPanel->addChild(m_layerValueLabel, 4);

    auto* layerDownBtn = makeLayerButton(
        { "GJ_arrow_03_001.png" },
        menu_selector(MainMenuLayoutEditor::onLayerDown),
        "<",
        false,
        false
    );
    layerDownBtn->setPosition({ rightCenterX - 34.f, winSize.height - 91.f });

    auto* layerUpBtn = makeLayerButton(
        { "GJ_arrow_03_001.png" },
        menu_selector(MainMenuLayoutEditor::onLayerUp),
        ">",
        true,
        false
    );
    layerUpBtn->setPosition({ rightCenterX + 34.f, winSize.height - 91.f });

    // --- Section separator ---
    if (auto* sectionLine = paimon::SpriteHelper::createRoundedRect(
        m_rightPanelWidth - 24.f, 1.5f, 1.f,
        { 0.45f, 0.85f, 1.f, 0.25f }
    )) {
        sectionLine->setPosition({ 12.f, winSize.height - 118.f });
        m_rightPanel->addChild(sectionLine, 3);
    }

    auto* editBtn = makeRightIconButton(
        { "GJ_editModeBtn_001.png", "edit_eMoveBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onEditSelected),
        Localization::get().getString("menu_layout.edit_selected"),
        28.f,
        22.f,
        "Edit"
    );
    auto* hideBtn = makeRightIconButton(
        { "GJ_lock_001.png", "GJ_sMagicIcon_001.png" },
        menu_selector(MainMenuLayoutEditor::onToggleHiddenSelected),
        Localization::get().getString("menu_layout.hide_selected"),
        18.f,
        18.f,
        "Hide"
    );
    auto* linkBtn = makeRightIconButton(
        { "gj_linkBtn_001.png", "edit_eLinkBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onLinkSelected),
        Localization::get().getString("menu_layout.link_selected"),
        28.f,
        20.f,
        "Link"
    );
    auto* unlinkBtn = makeRightIconButton(
        { "gj_linkBtnOff_001.png", "edit_eUnlinkBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onUnlinkSelected),
        Localization::get().getString("menu_layout.unlink_selected"),
        28.f,
        20.f,
        "Unlink"
    );
    auto* deleteBtn = makeRightIconButton(
        { "GJ_deleteIcon_001.png", "edit_eDeleteBtn_001.png", "GJ_trashBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onDeleteSelected),
        Localization::get().getString("menu_layout.delete_selected"),
        18.f,
        18.f,
        "Del"
    );
    auto* resetAllBtn = makeRightIconButton(
        { "edit_eUndoBtn_001.png", "GJ_updateBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onResetAll),
        "Reset",
        24.f,
        22.f,
        "Reset"
    );
    auto* scaleDownBtn = makeRightIconButton(
        { "GJ_minusBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onScaleDownSelected),
        Localization::get().getString("menu_layout.scale_down"),
        17.f,
        17.f,
        "-"
    );
    auto* scaleUpBtn = makeRightIconButton(
        { "GJ_plusBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onScaleUpSelected),
        Localization::get().getString("menu_layout.scale_up"),
        17.f,
        17.f,
        "+"
    );
    auto* resizeModeBtn = makeRightIconButton(
        { "edit_eScaleBtn_001.png", "GJ_editModeBtn_001.png" },
        menu_selector(MainMenuLayoutEditor::onToggleResizeMode),
        "Escala",
        20.f,
        20.f,
        "Size"
    );

    std::vector<CCMenuItemSpriteExtra*> rightButtons {
        editBtn,
        hideBtn,
        linkBtn,
        unlinkBtn,
        deleteBtn,
        resetAllBtn,
        scaleDownBtn,
        scaleUpBtn,
        resizeModeBtn,
    };

    float leftColX = rightCenterX - 30.f;
    float rightColX = rightCenterX + 30.f;
    float gridStartY = winSize.height - 140.f;
    float rowGap = 42.f;
    for (size_t i = 0; i < rightButtons.size(); ++i) {
        float x = (i % 2 == 0) ? leftColX : rightColX;
        if ((rightButtons.size() % 2 == 1) && i == rightButtons.size() - 1) {
            x = rightCenterX;
        }
        float y = gridStartY - static_cast<float>(i / 2) * rowGap;
        rightButtons[i]->setPosition({ x, y });
    }

    m_resizeModeLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_resizeModeLabel->setScale(0.28f);
    m_resizeModeLabel->setPosition({ rightCenterX, gridStartY - 4.f * rowGap - 20.f });
    m_rightPanel->addChild(m_resizeModeLabel, 4);
    this->refreshResizeModeButton();

    if (auto* rightToggleTab = paimon::SpriteHelper::createDarkPanel(kRightTabWidth, kRightTabHeight, 210, 6.f)) {
        rightToggleTab->setPosition({ -kRightTabWidth + 4.f, winSize.height * 0.5f - kRightTabHeight * 0.5f });
        m_rightPanel->addChild(rightToggleTab, 2);
    }

    if (auto* sideArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        sideArrow->setRotation(180.f);
        sideArrow->setScale(0.5f);
        m_rightToggleArrow = sideArrow;
        auto* sideBtn = CCMenuItemSpriteExtra::create(sideArrow, this, menu_selector(MainMenuLayoutEditor::onToggleRightPanel));
        sideBtn->setPosition({ -8.f, winSize.height * 0.5f });
        m_rightMenu->addChild(sideBtn);
    }
}

std::vector<EditableMenuButton> MainMenuLayoutEditor::currentButtons() const {
    std::vector<EditableMenuButton> buttons;
    buttons.reserve(m_buttons.size());

    for (auto const& button : m_buttons) {
        if (isShapeNode(button.target.node)) continue;
        buttons.push_back(button.target);
    }

    return buttons;
}

std::vector<DrawShapeLayout> MainMenuLayoutEditor::currentShapes() const {
    return m_shapeLayouts;
}

LayoutSnapshot MainMenuLayoutEditor::buildSnapshot() const {
    LayoutSnapshot snapshot;
    for (auto const& button : m_buttons) {
        if (!button.target.node) continue;
        if (isShapeNode(button.target.node)) continue;

        auto layout = MainMenuLayoutManager::readLayout(button.target.node);
        if (auto it = m_liveLayouts.find(button.target.key); it != m_liveLayouts.end()) {
            layout.linkGroup = it->second.linkGroup;
            layout.hasColor = it->second.hasColor;
            layout.color = it->second.color;
            layout.fontFile = it->second.fontFile;
        }
        snapshot.buttons[button.target.key] = layout;
    }
    snapshot.shapes = m_shapeLayouts;
    return snapshot;
}

void MainMenuLayoutEditor::applyEditorSnapshot(LayoutSnapshot const& snapshot) {
    m_isApplyingHistory = true;
    MainMenuLayoutManager::get().applySnapshot(this->currentButtons(), snapshot, this->getTargetLayer());
    m_shapeLayouts = snapshot.shapes;
    m_liveLayouts = snapshot.buttons;
    this->collectButtons();
    this->syncStateFromNodes();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
    m_isApplyingHistory = false;
}

void MainMenuLayoutEditor::commitHistorySnapshot() {
    if (m_isApplyingHistory) return;

    auto snapshot = this->buildSnapshot();
    if (!m_history.empty() && snapshotsEqual(m_history[m_historyCursor], snapshot)) {
        this->refreshRightPanel();
        return;
    }

    if (m_historyCursor + 1 < m_history.size()) {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCursor + 1), m_history.end());
    }

    m_history.push_back(std::move(snapshot));
    if (m_history.size() > kHistoryLimit) {
        m_history.erase(m_history.begin());
    }
    m_historyCursor = m_history.empty() ? 0 : (m_history.size() - 1);
    this->refreshRightPanel();
}

bool MainMenuLayoutEditor::canUndo() const {
    return !m_history.empty() && m_historyCursor > 0;
}

bool MainMenuLayoutEditor::canRedo() const {
    return !m_history.empty() && (m_historyCursor + 1) < m_history.size();
}

void MainMenuLayoutEditor::undoHistory() {
    if (!this->canUndo()) return;
    --m_historyCursor;
    this->applyEditorSnapshot(m_history[m_historyCursor]);
    this->refreshRightPanel();
}

void MainMenuLayoutEditor::redoHistory() {
    if (!this->canRedo()) return;
    ++m_historyCursor;
    this->applyEditorSnapshot(m_history[m_historyCursor]);
    this->refreshRightPanel();
}

void MainMenuLayoutEditor::setSelected(ButtonState* state) {
    m_selected = state;
    this->refreshRightPanel();
}

std::string MainMenuLayoutEditor::selectionLinkGroup() const {
    if (!m_selected) return "-";
    auto group = this->linkGroupFor(*m_selected);
    return group.empty() ? "-" : group;
}

std::vector<MainMenuLayoutEditor::ButtonState*> MainMenuLayoutEditor::selectionStates() {
    std::vector<ButtonState*> out;
    if (!m_selected) return out;

    auto group = this->linkGroupFor(*m_selected);
    if (group.empty()) {
        out.push_back(m_selected);
        return out;
    }

    for (auto& button : m_buttons) {
        if (this->linkGroupFor(button) == group) {
            out.push_back(&button);
        }
    }

    if (out.empty()) {
        out.push_back(m_selected);
    }
    return out;
}

std::vector<MainMenuLayoutEditor::ButtonState const*> MainMenuLayoutEditor::selectionStates() const {
    std::vector<ButtonState const*> out;
    if (!m_selected) return out;

    auto group = this->linkGroupFor(*m_selected);
    if (group.empty()) {
        out.push_back(m_selected);
        return out;
    }

    for (auto const& button : m_buttons) {
        if (this->linkGroupFor(button) == group) {
            out.push_back(&button);
        }
    }

    if (out.empty()) {
        out.push_back(m_selected);
    }
    return out;
}

CCRect MainMenuLayoutEditor::selectionBounds() const {
    auto selection = this->selectionStates();
    if (selection.empty()) {
        return { 0.f, 0.f, 0.f, 0.f };
    }

    bool first = true;
    CCRect out;
    for (auto const* state : selection) {
        if (!state || !state->target.node || !state->target.node->getParent()) continue;
        auto rect = this->buttonRect(*state);
        if (first) {
            out = rect;
            first = false;
            continue;
        }

        auto minX = std::min(out.getMinX(), rect.getMinX());
        auto minY = std::min(out.getMinY(), rect.getMinY());
        auto maxX = std::max(out.getMaxX(), rect.getMaxX());
        auto maxY = std::max(out.getMaxY(), rect.getMaxY());
        out = { minX, minY, maxX - minX, maxY - minY };
    }
    return first ? CCRect{ 0.f, 0.f, 0.f, 0.f } : out;
}

std::string MainMenuLayoutEditor::linkGroupFor(ButtonState const& state) const {
    if (isShapeNode(state.target.node)) {
        constexpr char const* prefix = "MenuLayer/shapes/";
        auto key = state.target.key;
        if (key.rfind(prefix, 0) == 0) {
            auto id = key.substr(std::char_traits<char>::length(prefix));
            for (auto const& shape : m_shapeLayouts) {
                if (shape.id == id) return shape.linkGroup;
            }
        }
        return "";
    }

    auto it = m_liveLayouts.find(state.target.key);
    return it != m_liveLayouts.end() ? it->second.linkGroup : std::string();
}

std::string MainMenuLayoutEditor::selectionLayerLabel() const {
    if (!m_selected) return "-";
    if (isShapeNode(m_selected->target.node)) {
        auto key = m_selected->target.key;
        constexpr char const* prefix = "MenuLayer/shapes/";
        if (key.rfind(prefix, 0) == 0) {
            auto id = key.substr(std::char_traits<char>::length(prefix));
            for (auto const& shape : m_shapeLayouts) {
                if (shape.id == id) return fmt::format("{}", shape.layer);
            }
        }
    }
    if (auto it = m_liveLayouts.find(m_selected->target.key); it != m_liveLayouts.end()) {
        return fmt::format("{}", it->second.layer);
    }
    if (m_selected->target.node) {
        return fmt::format("{}", m_selected->target.node->getZOrder());
    }
    return "-";
}

void MainMenuLayoutEditor::refreshRightPanel() {
    if (m_selectionCountLabel) {
        auto count = static_cast<int>(this->selectionStates().size());
        auto group = this->selectionLinkGroup();
        if (count <= 0) {
            m_selectionCountLabel->setString("0 sel");
        } else if (group == "-") {
            m_selectionCountLabel->setString(fmt::format("{} sel", count).c_str());
        } else {
            m_selectionCountLabel->setString(fmt::format("{} / {}", count, group).c_str());
        }
    }
    if (m_layerValueLabel) {
        m_layerValueLabel->setString(this->selectionLayerLabel().c_str());
    }

    if (m_rightToggleArrow) {
        m_rightToggleArrow->setRotation(m_rightPanelHidden ? 0.f : 180.f);
    }

    setMenuItemEnabledVisual(m_undoButton, this->canUndo());
    setMenuItemEnabledVisual(m_redoButton, this->canRedo());

    this->refreshResizeModeButton();
}

void MainMenuLayoutEditor::refreshResizeModeButton() {
    if (!m_resizeModeLabel) return;

    m_resizeModeLabel->setString(m_freeResizeMode ? "Modo libre" : "Escala fija");
    m_resizeModeLabel->setColor(m_freeResizeMode ? ccColor3B{255, 220, 120} : ccColor3B{170, 255, 170});
}

void MainMenuLayoutEditor::syncStateFromNodes() {
    for (auto& button : m_buttons) {
        if (!button.target.node) continue;

        auto layout = MainMenuLayoutManager::readLayout(button.target.node);
        if (auto it = m_liveLayouts.find(button.target.key); it != m_liveLayouts.end()) {
            layout.linkGroup = it->second.linkGroup;
        }
        m_liveLayouts[button.target.key] = layout;
        button.opacity = layout.opacity;
        button.hidden = layout.hidden;
    }

    auto current = MainMenuLayoutManager::captureShapes(this->getTargetLayer());
    if (current.size() == m_shapeLayouts.size()) {
        m_shapeLayouts = std::move(current);
    }

    this->refreshRightPanel();
}

void MainMenuLayoutEditor::openPresetPicker(bool saveMode) {
    WeakRef<MainMenuLayoutEditor> self = this;
    auto* popup = MainMenuLayoutPresetPopup::create(
        saveMode ? MainMenuLayoutPresetPopup::Mode::Save : MainMenuLayoutPresetPopup::Mode::Load,
        [self, saveMode](int slotIndex) {
            auto editorRef = self.lock();
            auto* editor = static_cast<MainMenuLayoutEditor*>(editorRef.data());
            if (!editor || !editor->getParent()) return;

            if (saveMode) {
                auto snapshot = editor->buildSnapshot();
                MainMenuLayoutPresetManager::get().setPreset(slotIndex, snapshot);

                auto text = fmt::format(
                    fmt::runtime(Localization::get().getString("menu_layout.preset_saved")),
                    slotIndex + 1
                );
                PaimonNotify::show(text, NotificationIcon::Success);
                return;
            }

            auto preset = MainMenuLayoutPresetManager::get().getPreset(slotIndex);
            if (!preset) {
                PaimonNotify::show(Localization::get().getString("menu_layout.presets_empty_slot"), NotificationIcon::Warning);
                return;
            }

            editor->applyEditorSnapshot(preset->snapshot);
            editor->commitHistorySnapshot();

            auto text = fmt::format(
                fmt::runtime(Localization::get().getString("menu_layout.preset_loaded")),
                slotIndex + 1
            );
            PaimonNotify::show(text, NotificationIcon::Success);
        }
    );

    if (popup) {
        popup->show();
    }
}

CCRect MainMenuLayoutEditor::buttonRect(ButtonState const& state) const {
    if (!state.target.node || !state.target.node->getParent()) {
        return { 0.f, 0.f, 0.f, 0.f };
    }

    auto* node = state.target.node;
    if (!node || !node->getParent()) {
        return { 0.f, 0.f, 0.f, 0.f };
    }

    auto bb = node->boundingBox();
    auto* parent = node->getParent();
    auto bl = parent->convertToWorldSpace({ bb.getMinX(), bb.getMinY() });
    auto tr = parent->convertToWorldSpace({ bb.getMaxX(), bb.getMaxY() });

    CCRect rect(
        std::min(bl.x, tr.x),
        std::min(bl.y, tr.y),
        std::abs(tr.x - bl.x),
        std::abs(tr.y - bl.y)
    );

    if (rect.size.width < kMinButtonHitSize) {
        auto centerX = rect.getMidX();
        rect.origin.x = centerX - kMinButtonHitSize / 2.f;
        rect.size.width = kMinButtonHitSize;
    }
    if (rect.size.height < kMinButtonHitSize) {
        auto centerY = rect.getMidY();
        rect.origin.y = centerY - kMinButtonHitSize / 2.f;
        rect.size.height = kMinButtonHitSize;
    }

    return rect;
}

void MainMenuLayoutEditor::updateHighlights() {
    if (!m_allHighlights) return;

    m_allHighlights->clear();
    ccColor4F outline = { 0.35f, 0.65f, 1.f, 0.55f };
    auto selectedStates = this->selectionStates();

    for (auto const& button : m_buttons) {
        if (!button.target.node || !button.target.node->getParent()) continue;
        auto color = outline;
        if (std::find(selectedStates.begin(), selectedStates.end(), &button) != selectedStates.end()) {
            color = { 0.55f, 1.f, 0.62f, 0.8f };
        }
        drawRect(m_allHighlights, this->buttonRect(button), color, 1.2f);
    }
}

void MainMenuLayoutEditor::updateSelectionUI() {
    if (!m_selectionOutline || !m_scaleGrip || !m_scaleGripTopLeft || !m_scaleGripTopRight || !m_scaleGripBottomLeft || !m_deleteGrip || !m_opacityDownGrip || !m_opacityUpGrip) return;

    m_selectionOutline->clear();
    m_scaleGrip->setVisible(false);
    m_scaleGripTopLeft->setVisible(false);
    m_scaleGripTopRight->setVisible(false);
    m_scaleGripBottomLeft->setVisible(false);
    m_deleteGrip->setVisible(false);
    m_opacityDownGrip->setVisible(false);
    m_opacityUpGrip->setVisible(false);

    auto selection = this->selectionStates();
    if (selection.empty()) return;

    auto rect = this->selectionBounds();
    rect.origin.x -= kOutlinePad;
    rect.origin.y -= kOutlinePad;
    rect.size.width += kOutlinePad * 2.f;
    rect.size.height += kOutlinePad * 2.f;

    drawRect(m_selectionOutline, rect, { 1.f, 1.f, 1.f, 0.92f }, 1.9f);
    drawRect(m_selectionOutline, rect, { 0.35f, 1.f, 0.55f, 0.35f }, 3.4f);

    m_scaleGripTopLeft->setPosition({ rect.getMinX() - 7.f, rect.getMaxY() + 7.f });
    m_scaleGripTopLeft->setVisible(true);

    m_scaleGripTopRight->setPosition({ rect.getMaxX() + 7.f, rect.getMaxY() + 7.f });
    m_scaleGripTopRight->setVisible(true);

    m_scaleGripBottomLeft->setPosition({ rect.getMinX() - 7.f, rect.getMinY() - 7.f });
    m_scaleGripBottomLeft->setVisible(true);

    m_scaleGrip->setPosition({ rect.getMaxX() + 7.f, rect.getMinY() - 7.f });
    m_scaleGrip->setVisible(true);

    m_deleteGrip->setPosition({ rect.getMaxX() + 7.f, rect.getMaxY() + 7.f });
    m_deleteGrip->setVisible(true);

    m_opacityUpGrip->setPosition({ rect.getMinX() - 10.f, rect.getMaxY() + 7.f });
    m_opacityUpGrip->setVisible(true);

    m_opacityDownGrip->setPosition({ rect.getMinX() - 10.f, rect.getMinY() - 7.f });
    m_opacityDownGrip->setVisible(true);
}

void MainMenuLayoutEditor::setLinkGroup(ButtonState& state, std::string const& group) {
    if (isShapeNode(state.target.node)) {
        if (auto* shape = this->shapeLayout(state)) {
            shape->linkGroup = group;
            MainMenuLayoutManager::applyShapeLayout(state.target.node, *shape);
        }
        return;
    }

    if (auto* layout = this->currentLayoutFor(state)) {
        layout->linkGroup = group;
        MainMenuLayoutManager::applyLayout(state.target.node, *layout);
    }
}

std::string MainMenuLayoutEditor::nextLinkGroup() const {
    int maxIndex = 0;
    auto consider = [&maxIndex](std::string const& group) {
        constexpr char const* prefix = "group-";
        if (group.rfind(prefix, 0) != 0) return;
        auto suffix = group.substr(std::char_traits<char>::length(prefix));
        try {
            maxIndex = std::max(maxIndex, std::stoi(suffix));
        } catch (...) {
        }
    };

    for (auto const& [_, layout] : m_liveLayouts) {
        consider(layout.linkGroup);
    }
    for (auto const& shape : m_shapeLayouts) {
        consider(shape.linkGroup);
    }
    return fmt::format("group-{}", maxIndex + 1);
}

void MainMenuLayoutEditor::clearLinkArm() {
    m_linkArmedGroup.clear();
    m_linkSourceKey.clear();
}

void MainMenuLayoutEditor::linkSelection() {
    if (!m_selected) return;

    auto currentGroup = this->linkGroupFor(*m_selected);
    if (!m_linkArmedGroup.empty() && m_linkSourceKey != m_selected->target.key) {
        for (auto* state : this->selectionStates()) {
            if (state) this->setLinkGroup(*state, m_linkArmedGroup);
        }
        this->clearLinkArm();
        this->syncStateFromNodes();
        return;
    }

    if (currentGroup.empty()) {
        auto group = this->nextLinkGroup();
        for (auto* state : this->selectionStates()) {
            if (state) this->setLinkGroup(*state, group);
        }
        this->syncStateFromNodes();
        return;
    }

    m_linkArmedGroup = currentGroup;
    m_linkSourceKey = m_selected->target.key;
}

void MainMenuLayoutEditor::unlinkSelection() {
    auto selection = this->selectionStates();
    for (auto* state : selection) {
        if (state) this->setLinkGroup(*state, "");
    }
    this->clearLinkArm();
    this->syncStateFromNodes();
}

void MainMenuLayoutEditor::toggleHiddenSelection() {
    auto selection = this->selectionStates();
    if (selection.empty()) return;

    bool hide = false;
    for (auto* state : selection) {
        if (state && !state->hidden) {
            hide = true;
            break;
        }
    }

    for (auto* state : selection) {
        if (!state || !state->target.node) continue;
        state->hidden = hide;
        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                shape->hidden = hide;
                MainMenuLayoutManager::applyShapeLayout(state->target.node, *shape);
            }
        } else {
            this->applyPreviewState(*state);
        }
    }
}

void MainMenuLayoutEditor::deleteSelection() {
    auto selection = this->selectionStates();
    if (selection.empty()) return;

    bool hideButtons = false;
    for (auto* state : selection) {
        if (!state || !state->target.node || isShapeNode(state->target.node)) continue;
        if (!state->hidden) {
            hideButtons = true;
            break;
        }
    }

    for (auto* state : selection) {
        if (!state || !state->target.node) continue;
        if (isShapeNode(state->target.node)) {
            auto* shape = this->shapeLayout(*state);
            auto shapeID = shape ? shape->id : std::string();
            state->target.node->removeFromParent();
            if (!shapeID.empty()) {
                m_shapeLayouts.erase(
                    std::remove_if(m_shapeLayouts.begin(), m_shapeLayouts.end(), [&](DrawShapeLayout const& entry) {
                        return entry.id == shapeID;
                    }),
                    m_shapeLayouts.end()
                );
            }
        } else {
            state->hidden = hideButtons;
            this->applyPreviewState(*state);
        }
    }

    m_selected = nullptr;
    this->clearLinkArm();
    this->collectButtons();
}

void MainMenuLayoutEditor::applyScaleFactor(float factor) {
    auto selection = this->selectionStates();
    for (auto* state : selection) {
        if (!state || !state->target.node) continue;

        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                auto scaleX = std::clamp(shape->scaleX * factor, kMinAxisScale, kMaxScale);
                auto scaleY = std::clamp(shape->scaleY * factor, kMinAxisScale, kMaxScale);
                if (!m_freeResizeMode) {
                    auto uniformScale = std::sqrt(scaleX * scaleY);
                    scaleX = uniformScale;
                    scaleY = uniformScale;
                }
                shape->scaleX = scaleX;
                shape->scaleY = scaleY;
                shape->scale = std::sqrt(shape->scaleX * shape->scaleY);
                MainMenuLayoutManager::applyShapeLayout(state->target.node, *shape);
            }
        } else {
            auto* layout = this->currentLayoutFor(*state);
            if (!layout) continue;
            auto scaleX = std::clamp(layout->scaleX * factor, kMinAxisScale, kMaxScale);
            auto scaleY = std::clamp(layout->scaleY * factor, kMinAxisScale, kMaxScale);
            if (!m_freeResizeMode) {
                auto uniformScale = std::sqrt(scaleX * scaleY);
                scaleX = uniformScale;
                scaleY = uniformScale;
            }
            layout->scaleX = scaleX;
            layout->scaleY = scaleY;
            layout->scale = std::sqrt(layout->scaleX * layout->scaleY);
            MainMenuLayoutManager::applyLayout(state->target.node, *layout);
        }
    }

    this->syncStateFromNodes();
}

void MainMenuLayoutEditor::applyOpacityDelta(float delta) {
    auto selection = this->selectionStates();
    for (auto* state : selection) {
        if (!state || !state->target.node) continue;
        state->hidden = false;
        state->opacity = std::clamp(state->opacity + delta, 0.f, 1.f);
        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                shape->hidden = false;
                shape->opacity = state->opacity;
                MainMenuLayoutManager::applyShapeLayout(state->target.node, *shape);
            }
        } else {
            this->applyPreviewState(*state);
        }
    }

    this->syncStateFromNodes();
}

void MainMenuLayoutEditor::beginDrag(DragMode mode, CCPoint worldPos, ResizeHandle handle) {
    m_dragMode = mode;
    m_resizeHandle = handle;
    m_touchStart = worldPos;
    m_dragSnapshots.clear();
    m_dragStartBounds = this->selectionBounds();
    m_scaleCenterWorld = ccp(m_dragStartBounds.getMidX(), m_dragStartBounds.getMidY());

    for (auto* state : this->selectionStates()) {
        if (!state || !state->target.node || !state->target.node->getParent()) continue;
        m_dragSnapshots[state->target.key] = {
            worldPosition(state->target.node),
            state->target.node->getPosition(),
            state->target.node->getScale(),
            state->target.node->getScaleX(),
            state->target.node->getScaleY(),
        };
    }

    if (m_selected && m_dragSnapshots.contains(m_selected->target.key)) {
        auto const& snapshot = m_dragSnapshots.at(m_selected->target.key);
        m_itemStartWorld = snapshot.worldPosition;
        m_itemStartScale = snapshot.scale;
    }
}

void MainMenuLayoutEditor::applyMoveSelection(CCPoint worldPos) {
    if (!m_selected) return;
    auto it = m_dragSnapshots.find(m_selected->target.key);
    if (it == m_dragSnapshots.end()) return;

    auto leadWorld = CCPoint{
        it->second.worldPosition.x + (worldPos.x - m_touchStart.x),
        it->second.worldPosition.y + (worldPos.y - m_touchStart.y),
    };
    auto leadLocal = this->snappedLocalPosition(m_selected, leadWorld);
    auto actualWorld = m_selected->target.node->getParent()->convertToWorldSpace(leadLocal);
    auto deltaWorld = actualWorld - it->second.worldPosition;

    for (auto* state : this->selectionStates()) {
        if (!state || !state->target.node || !state->target.node->getParent()) continue;
        auto snapIt = m_dragSnapshots.find(state->target.key);
        if (snapIt == m_dragSnapshots.end()) continue;

        auto targetWorld = snapIt->second.worldPosition + deltaWorld;
        auto targetLocal = state->target.node->getParent()->convertToNodeSpace(targetWorld);
        state->target.node->setPosition(targetLocal);

        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                shape->position = targetLocal;
            }
        } else if (auto* layout = this->currentLayoutFor(*state)) {
            layout->position = targetLocal;
        }
    }
}

void MainMenuLayoutEditor::applyResizeSelection(CCPoint worldPos) {
    if (m_resizeHandle == ResizeHandle::None) return;
    auto bounds = m_dragStartBounds;
    if (bounds.size.width <= 0.f || bounds.size.height <= 0.f) return;

    auto fixed = oppositeAnchor(bounds, m_resizeHandle);
    auto startCorner = rectAnchor(bounds, m_resizeHandle);
    auto startDx = startCorner.x - fixed.x;
    auto startDy = startCorner.y - fixed.y;
    auto currentDx = worldPos.x - fixed.x;
    auto currentDy = worldPos.y - fixed.y;

    auto scaleX = std::clamp(std::abs(currentDx) / std::max(8.f, std::abs(startDx)), kMinAxisScale, kMaxScale);
    auto scaleY = std::clamp(std::abs(currentDy) / std::max(8.f, std::abs(startDy)), kMinAxisScale, kMaxScale);
    if (!m_freeResizeMode) {
        auto uniformScale = std::sqrt(scaleX * scaleY);
        scaleX = uniformScale;
        scaleY = uniformScale;
    }

    for (auto* state : this->selectionStates()) {
        if (!state || !state->target.node || !state->target.node->getParent()) continue;
        auto snapIt = m_dragSnapshots.find(state->target.key);
        if (snapIt == m_dragSnapshots.end()) continue;

        auto const& snap = snapIt->second;
        auto offset = snap.worldPosition - fixed;
        auto targetWorld = CCPoint{
            fixed.x + offset.x * scaleX,
            fixed.y + offset.y * scaleY,
        };
        auto targetLocal = state->target.node->getParent()->convertToNodeSpace(targetWorld);
        state->target.node->setPosition(targetLocal);

        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                shape->position = targetLocal;
                shape->scaleX = std::clamp(snap.scaleX * scaleX, kMinAxisScale, kMaxScale);
                shape->scaleY = std::clamp(snap.scaleY * scaleY, kMinAxisScale, kMaxScale);
                shape->scale = std::sqrt(shape->scaleX * shape->scaleY);
                MainMenuLayoutManager::applyShapeLayout(state->target.node, *shape);
            }
            continue;
        }

        auto* layout = this->currentLayoutFor(*state);
        if (!layout) continue;
        layout->position = targetLocal;
        layout->scaleX = std::clamp(snap.scaleX * scaleX, kMinAxisScale, kMaxScale);
        layout->scaleY = std::clamp(snap.scaleY * scaleY, kMinAxisScale, kMaxScale);
        layout->scale = std::sqrt(layout->scaleX * layout->scaleY);
        MainMenuLayoutManager::applyLayout(state->target.node, *layout);
    }
}

void MainMenuLayoutEditor::animateRightPanel(bool hidden) {
    if (!m_rightPanel) return;
    m_rightPanelHidden = hidden;
    m_rightPanel->stopAllActions();

    auto targetX = hidden ? m_rightPanelHiddenX : m_rightPanelShownX;
    auto action = CCEaseSineOut::create(CCMoveTo::create(kPanelAnimDuration, { targetX, 0.f }));
    m_rightPanel->runAction(action);
    this->refreshRightPanel();
}

MenuButtonLayout* MainMenuLayoutEditor::currentLayoutFor(ButtonState& state) {
    if (isShapeNode(state.target.node)) return nullptr;
    auto it = m_liveLayouts.find(state.target.key);
    if (it == m_liveLayouts.end()) return nullptr;
    return &it->second;
}

void MainMenuLayoutEditor::updateNodeLayer(ButtonState& state, int layer) {
    if (isShapeNode(state.target.node)) {
        if (auto* shape = this->shapeLayout(state)) {
            shape->layer = layer;
            shape->zOrder = layer;
            MainMenuLayoutManager::applyShapeLayout(state.target.node, *shape);
        }
        return;
    }

    if (auto* layout = this->currentLayoutFor(state)) {
        layout->layer = layer;
        layout->scale = state.target.node->getScale();
        layout->scaleX = state.target.node->getScaleX();
        layout->scaleY = state.target.node->getScaleY();
        MainMenuLayoutManager::applyLayout(state.target.node, *layout);
    }
}

void MainMenuLayoutEditor::updateStatusText() {
    if (!m_statusLabel) return;

    auto selection = this->selectionStates();
    if (!m_selected || !m_selected->target.node || selection.empty()) {
        m_statusLabel->setString(fmt::format("{} [{}]", Localization::get().getString("menu_layout.none_selected"), m_buttons.size()).c_str());
        return;
    }

    auto rect = this->selectionBounds();
    auto label = m_selected->target.label;
    auto group = this->selectionLinkGroup();
    if (selection.size() > 1) {
        label = group == "-"
            ? fmt::format("{} sel", selection.size())
            : fmt::format("{} / {}", selection.size(), group);
    } else if (isShapeNode(m_selected->target.node)) {
        auto shapeLabel = Localization::get().getString("menu_layout.draw_shape_label");
        if (label.rfind(shapeLabel, 0) != 0) {
            label = fmt::format("{} / {}", shapeLabel, label);
        }
    }

    float avgScale = 0.f;
    float avgOpacity = 0.f;
    int count = 0;
    for (auto const* state : selection) {
        if (!state || !state->target.node) continue;
        avgScale += state->target.node->getScale();
        avgOpacity += std::clamp(state->opacity, 0.f, 1.f);
        ++count;
    }
    if (count > 0) {
        avgScale /= static_cast<float>(count);
        avgOpacity /= static_cast<float>(count);
    }

    auto formatString = Localization::get().getString("menu_layout.status");
    m_statusLabel->setString(
        fmt::format(
            fmt::runtime(formatString),
            label,
            rect.getMidX(),
            rect.getMidY(),
            avgScale,
            std::round(avgOpacity * 100.f)
        ).c_str()
    );
}

void MainMenuLayoutEditor::applyPreviewState(ButtonState& state) {
    if (!state.target.node) return;

    if (auto* layout = this->currentLayoutFor(state)) {
        *layout = this->buildLayout(state);
        MainMenuLayoutManager::applyLayout(state.target.node, *layout);
        return;
    }

    auto layout = this->buildLayout(state);
    MainMenuLayoutManager::applyLayout(state.target.node, layout);
}

MenuButtonLayout MainMenuLayoutEditor::buildLayout(ButtonState const& state) const {
    MenuButtonLayout layout;
    if (!state.target.node) return layout;

    layout.position = state.target.node->getPosition();
    layout.scale = state.target.node->getScale();
    layout.scaleX = state.target.node->getScaleX();
    layout.scaleY = state.target.node->getScaleY();
    layout.opacity = state.opacity;
    layout.hidden = state.hidden;
    if (auto it = m_liveLayouts.find(state.target.key); it != m_liveLayouts.end()) {
        layout.layer = it->second.layer;
        layout.linkGroup = it->second.linkGroup;
        layout.hasColor = it->second.hasColor;
        layout.color = it->second.color;
        layout.fontFile = it->second.fontFile;
    } else {
        layout.layer = state.target.node->getZOrder();
    }
    return layout;
}

void MainMenuLayoutEditor::hideGuides() {
    if (m_guideX) {
        m_guideX->clear();
        m_guideX->setVisible(false);
    }
    if (m_guideY) {
        m_guideY->clear();
        m_guideY->setVisible(false);
    }
}

void MainMenuLayoutEditor::updateGuides(bool showX, bool showY, float x, float y) {
    auto showGuides = Mod::get()->getSettingValue<bool>("main-menu-layout-show-guides");
    if (!showGuides) {
        this->hideGuides();
        return;
    }

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    ccColor4F color = { 0.15f, 1.f, 0.55f, 0.85f };

    if (m_guideX) {
        m_guideX->clear();
        if (showX) {
            m_guideX->drawSegment({ x, 0.f }, { x, winSize.height }, 1.2f, color);
            m_guideX->setVisible(true);
        } else {
            m_guideX->setVisible(false);
        }
    }

    if (m_guideY) {
        m_guideY->clear();
        if (showY) {
            m_guideY->drawSegment({ 0.f, y }, { winSize.width, y }, 1.2f, color);
            m_guideY->setVisible(true);
        } else {
            m_guideY->setVisible(false);
        }
    }
}

void MainMenuLayoutEditor::restoreInitialLayouts() {
    for (auto const& button : m_buttons) {
        if (!button.target.node || !button.target.node->getParent()) continue;
        if (isShapeNode(button.target.node)) continue;
        auto it = m_initialLiveLayouts.find(button.target.key);
        MainMenuLayoutManager::applyLayout(button.target.node, it != m_initialLiveLayouts.end() ? it->second : button.initialLayout);
    }

    MainMenuLayoutManager::get().syncShapes(this->getTargetLayer(), m_initialShapes);
    m_shapeLayouts = m_initialShapes;
    m_liveLayouts = m_initialLiveLayouts;
    this->clearLinkArm();
}

void MainMenuLayoutEditor::resetSelectedToDefault() {
    if (!m_selected || !m_selected->target.node) return;

    if (isShapeNode(m_selected->target.node)) {
        if (auto* shape = this->shapeLayout(*m_selected)) {
            auto initial = std::find_if(m_initialShapes.begin(), m_initialShapes.end(), [&](DrawShapeLayout const& entry) {
                return entry.id == shape->id;
            });

            if (initial != m_initialShapes.end()) {
                *shape = *initial;
            } else {
                shape->position = ccp(CCDirector::sharedDirector()->getWinSize().width * 0.5f, CCDirector::sharedDirector()->getWinSize().height * 0.5f);
                shape->scale = 1.f;
                shape->scaleX = 1.f;
                shape->scaleY = 1.f;
                shape->opacity = 0.75f;
                shape->hidden = false;
                shape->zOrder = shape->layer;
            }

            MainMenuLayoutManager::applyShapeLayout(m_selected->target.node, *shape);
            this->syncStateFromNodes();
        }
        return;
    }

    auto layout = MainMenuLayoutManager::get().getDefaultLayout(m_selected->target.key);
    if (!layout) {
        auto it = m_initialLiveLayouts.find(m_selected->target.key);
        if (it != m_initialLiveLayouts.end()) {
            layout = it->second;
        } else {
            layout = m_selected->initialLayout;
        }
    }

    m_selected->opacity = layout->opacity;
    m_selected->hidden = layout->hidden;
    m_liveLayouts[m_selected->target.key] = *layout;
    MainMenuLayoutManager::applyLayout(m_selected->target.node, *layout);
}

void MainMenuLayoutEditor::resetAllToDefault() {
    for (auto& button : m_buttons) {
        if (!button.target.node || !button.target.node->getParent()) continue;

        if (isShapeNode(button.target.node)) continue;

        auto layout = MainMenuLayoutManager::get().getDefaultLayout(button.target.key);
        if (!layout) {
            auto it = m_initialLiveLayouts.find(button.target.key);
            if (it != m_initialLiveLayouts.end()) {
                layout = it->second;
            } else {
                layout = button.initialLayout;
            }
        }

        button.opacity = layout->opacity;
        button.hidden = layout->hidden;
        m_liveLayouts[button.target.key] = *layout;
        MainMenuLayoutManager::applyLayout(button.target.node, *layout);
    }

    m_shapeLayouts.clear();
    MainMenuLayoutManager::get().syncShapes(this->getTargetLayer(), {});
    this->clearLinkArm();
    this->collectButtons();
}

void MainMenuLayoutEditor::close(bool save) {
    if (save) {
        MainMenuLayoutManager::get().setCustomFromSnapshot(this->buildSnapshot());
        PaimonNotify::show(Localization::get().getString("menu_layout.saved"), NotificationIcon::Success);
    } else {
        this->restoreInitialLayouts();
    }

    this->clearLinkArm();
    this->removeFromParent();
}

MainMenuLayoutEditor::ButtonState* MainMenuLayoutEditor::findButtonAt(CCPoint worldPos) {
    ButtonState* fallback = nullptr;
    ButtonState* hiddenHit = nullptr;
    float bestDistSq = FLT_MAX;

    for (auto it = m_buttons.rbegin(); it != m_buttons.rend(); ++it) {
        if (!it->target.node || !it->target.node->getParent()) continue;

        bool hidden = it->hidden || !it->target.node->isVisible();

        auto* parent = it->target.node->getParent();
        auto localPos = parent->convertToNodeSpace(worldPos);
        auto bbox = it->target.node->boundingBox();

        float x0 = bbox.getMinX();
        float y0 = bbox.getMinY();
        float x1 = bbox.getMaxX();
        float y1 = bbox.getMaxY();
        float cx = (x0 + x1) * 0.5f;
        float cy = (y0 + y1) * 0.5f;

        if (x1 - x0 < kMinButtonHitSize) {
            x0 = cx - kMinButtonHitSize * 0.5f;
            x1 = cx + kMinButtonHitSize * 0.5f;
        }
        if (y1 - y0 < kMinButtonHitSize) {
            y0 = cy - kMinButtonHitSize * 0.5f;
            y1 = cy + kMinButtonHitSize * 0.5f;
        }

        CCRect localRect(x0, y0, x1 - x0, y1 - y0);
        bool directHit = localRect.containsPoint(localPos) || this->buttonRect(*it).containsPoint(worldPos);
        if (directHit && !hidden) {
            return &(*it);
        }

        if (directHit && hidden && !hiddenHit) {
            hiddenHit = &(*it);
            continue;
        }

        if (hidden) {
            continue;
        }

        auto worldCenter = worldPosition(it->target.node);
        auto dx = worldPos.x - worldCenter.x;
        auto dy = worldPos.y - worldCenter.y;
        auto distSq = dx * dx + dy * dy;
        auto hitRadius = std::max({ 90.f, kMinButtonHitSize, x1 - x0, y1 - y0 }) * 0.75f;
        if (distSq <= hitRadius * hitRadius && distSq < bestDistSq) {
            bestDistSq = distSq;
            fallback = &(*it);
        }
    }

    if (hiddenHit) {
        return hiddenHit;
    }

    return fallback;
}

bool MainMenuLayoutEditor::isTouchOnToolbar(CCPoint worldPos) const {
    if (!m_topPanelHidden && nodeContainsInteractiveTouch(m_topPanel, worldPos)) {
        return true;
    }
    if (m_topPanelCollapsed && m_topPanelCollapsed->isVisible() && nodeContainsInteractiveTouch(m_topPanelCollapsed, worldPos)) {
        return true;
    }
    return false;
}

bool MainMenuLayoutEditor::isTouchOnRightPanel(CCPoint worldPos) const {
    return nodeContainsInteractiveTouch(m_rightPanel, worldPos);
}

bool MainMenuLayoutEditor::isTouchOnScaleGrip(CCPoint worldPos) const {
    if (!m_scaleGrip || !m_scaleGrip->isVisible()) return false;

    auto gripPos = m_scaleGrip->getPosition();
    auto dx = worldPos.x - gripPos.x;
    auto dy = worldPos.y - gripPos.y;
    return (dx * dx + dy * dy) <= (kGripHitRadius * kGripHitRadius);
}

MainMenuLayoutEditor::ResizeHandle MainMenuLayoutEditor::resizeHandleAt(CCPoint worldPos) const {
    auto hit = [worldPos](CCNode* grip) {
        if (!grip || !grip->isVisible()) return false;
        auto gripPos = grip->getPosition();
        auto dx = worldPos.x - gripPos.x;
        auto dy = worldPos.y - gripPos.y;
        return (dx * dx + dy * dy) <= (kGripHitRadius * kGripHitRadius);
    };

    if (hit(m_scaleGripTopLeft)) return ResizeHandle::TopLeft;
    if (hit(m_scaleGripTopRight)) return ResizeHandle::TopRight;
    if (hit(m_scaleGripBottomLeft)) return ResizeHandle::BottomLeft;
    if (hit(m_scaleGrip)) return ResizeHandle::BottomRight;
    return ResizeHandle::None;
}

bool MainMenuLayoutEditor::isTouchOnDeleteGrip(CCPoint worldPos) const {
    if (!m_deleteGrip || !m_deleteGrip->isVisible()) return false;

    auto gripPos = m_deleteGrip->getPosition();
    auto dx = worldPos.x - gripPos.x;
    auto dy = worldPos.y - gripPos.y;
    return (dx * dx + dy * dy) <= (kGripHitRadius * kGripHitRadius);
}

bool MainMenuLayoutEditor::isTouchOnOpacityDownGrip(CCPoint worldPos) const {
    if (!m_opacityDownGrip || !m_opacityDownGrip->isVisible()) return false;

    auto gripPos = m_opacityDownGrip->getPosition();
    auto dx = worldPos.x - gripPos.x;
    auto dy = worldPos.y - gripPos.y;
    return (dx * dx + dy * dy) <= (kGripHitRadius * kGripHitRadius);
}

bool MainMenuLayoutEditor::isTouchOnOpacityUpGrip(CCPoint worldPos) const {
    if (!m_opacityUpGrip || !m_opacityUpGrip->isVisible()) return false;

    auto gripPos = m_opacityUpGrip->getPosition();
    auto dx = worldPos.x - gripPos.x;
    auto dy = worldPos.y - gripPos.y;
    return (dx * dx + dy * dy) <= (kGripHitRadius * kGripHitRadius);
}

CCPoint MainMenuLayoutEditor::snappedLocalPosition(ButtonState* state, CCPoint proposedWorld) const {
    if (!state || !state->target.node || !state->target.node->getParent()) {
        return { 0.f, 0.f };
    }

    auto snapDistance = std::clamp(static_cast<float>(Mod::get()->getSettingValue<int64_t>("main-menu-layout-snap-distance")), 1.f, 64.f);
    auto gridSize = std::max(2.f, static_cast<float>(Mod::get()->getSettingValue<int64_t>("main-menu-layout-grid-size")));
    auto gridSnap = Mod::get()->getSettingValue<bool>("main-menu-layout-grid-snap");
    auto edgeSnap = Mod::get()->getSettingValue<bool>("main-menu-layout-snap-to-edges");
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto currentRect = this->buttonRect(*state);
    auto halfW = currentRect.size.width / 2.f;
    auto halfH = currentRect.size.height / 2.f;

    float bestX = proposedWorld.x;
    float bestY = proposedWorld.y;
    float bestDistX = snapDistance + 1.f;
    float bestDistY = snapDistance + 1.f;
    bool snappedX = false;
    bool snappedY = false;

    auto considerX = [&](float candidate) {
        auto dist = std::abs(proposedWorld.x - candidate);
        if (dist <= snapDistance && dist < bestDistX) {
            bestDistX = dist;
            bestX = candidate;
            snappedX = true;
        }
    };
    auto considerY = [&](float candidate) {
        auto dist = std::abs(proposedWorld.y - candidate);
        if (dist <= snapDistance && dist < bestDistY) {
            bestDistY = dist;
            bestY = candidate;
            snappedY = true;
        }
    };

    considerX(winSize.width / 2.f);
    considerY(winSize.height / 2.f);

    if (edgeSnap) {
        considerX(halfW);
        considerX(winSize.width - halfW);
        considerY(halfH);
        considerY(winSize.height - halfH);
    }

    if (gridSnap) {
        considerX(std::round(proposedWorld.x / gridSize) * gridSize);
        considerY(std::round(proposedWorld.y / gridSize) * gridSize);
    }

    for (auto const& button : m_buttons) {
        if (&button == state || !button.target.node || !button.target.node->getParent()) continue;

        auto otherWorld = worldPosition(button.target.node);
        considerX(otherWorld.x);
        considerY(otherWorld.y);

        if (edgeSnap) {
            auto otherRect = this->buttonRect(button);
            considerX(otherRect.getMinX() + halfW);
            considerX(otherRect.getMaxX() - halfW);
            considerY(otherRect.getMinY() + halfH);
            considerY(otherRect.getMaxY() - halfH);
        }
    }

    const_cast<MainMenuLayoutEditor*>(this)->updateGuides(snappedX, snappedY, bestX, bestY);

    auto snappedWorld = CCPoint{
        snappedX ? bestX : proposedWorld.x,
        snappedY ? bestY : proposedWorld.y,
    };

    return state->target.node->getParent()->convertToNodeSpace(snappedWorld);
}

bool MainMenuLayoutEditor::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto worldPos = touch->getLocation();

    if (m_buttons.empty()) {
        this->collectButtons();
        this->updateHighlights();
    }

    if (this->isTouchOnToolbar(worldPos)) {
        return false;
    }

    if (this->isTouchOnRightPanel(worldPos)) {
        return false;
    }

    if (m_selected && this->isTouchOnDeleteGrip(worldPos) && m_selected->target.node) {
        this->deleteSelection();
        this->commitHistorySnapshot();
        this->updateHighlights();
        this->updateSelectionUI();
        this->updateStatusText();
        return true;
    }

    if (m_selected && this->isTouchOnOpacityDownGrip(worldPos) && m_selected->target.node) {
        this->applyOpacityDelta(-kOpacityStep);
        this->commitHistorySnapshot();
        this->updateStatusText();
        this->updateHighlights();
        this->updateSelectionUI();
        return true;
    }

    if (m_selected && this->isTouchOnOpacityUpGrip(worldPos) && m_selected->target.node) {
        this->applyOpacityDelta(kOpacityStep);
        this->commitHistorySnapshot();
        this->updateStatusText();
        this->updateHighlights();
        this->updateSelectionUI();
        return true;
    }

    if (m_selected && m_selected->target.node) {
        auto handle = this->resizeHandleAt(worldPos);
        if (handle != ResizeHandle::None) {
            this->beginDrag(DragMode::Scale, worldPos, handle);
            return true;
        }
    }

    auto* hit = this->findButtonAt(worldPos);
    if (!hit) {
        this->collectButtons();
        hit = this->findButtonAt(worldPos);
    }

    if (!hit) {
        this->setSelected(nullptr);
        this->updateSelectionUI();
        this->updateStatusText();
        this->hideGuides();
        return true;
    }

    this->setSelected(hit);
    this->updateSelectionUI();
    this->updateStatusText();

    if (!m_selected || !m_selected->target.node || !m_selected->target.node->getParent()) {
        this->hideGuides();
        return true;
    }

    this->beginDrag(DragMode::Move, worldPos);
    return true;
}

void MainMenuLayoutEditor::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_selected || !m_selected->target.node) return;

    auto worldPos = touch->getLocation();

    if (m_dragMode == DragMode::Move) {
        this->applyMoveSelection(worldPos);
    } else if (m_dragMode == DragMode::Scale) {
        this->applyResizeSelection(worldPos);
    }

    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::ccTouchEnded(CCTouch*, CCEvent*) {
    bool changed = m_dragMode != DragMode::None;
    m_dragMode = DragMode::None;
    m_resizeHandle = ResizeHandle::None;
    m_dragSnapshots.clear();
    this->hideGuides();
    if (changed) {
        this->commitHistorySnapshot();
    }
}

void MainMenuLayoutEditor::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_dragMode = DragMode::None;
    m_resizeHandle = ResizeHandle::None;
    m_dragSnapshots.clear();
    this->hideGuides();
}

void MainMenuLayoutEditor::keyBackClicked() {
    this->cancelAndClose();
}

void MainMenuLayoutEditor::update(float) {
    if (!this->getTargetLayer()) {
        this->removeFromParent();
        return;
    }

    if (m_buttons.empty()) {
        this->collectButtons();
    }

    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onSave(CCObject*) {
    this->saveAndClose();
}

void MainMenuLayoutEditor::onCancel(CCObject*) {
    this->cancelAndClose();
}

void MainMenuLayoutEditor::onSavePreset(CCObject*) {
    this->openPresetPicker(true);
}

void MainMenuLayoutEditor::onLoadPreset(CCObject*) {
    this->openPresetPicker(false);
}

void MainMenuLayoutEditor::onResetSelected(CCObject*) {
    this->resetSelectedToDefault();
    this->clearLinkArm();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onResetAll(CCObject*) {
    this->resetAllToDefault();
    this->clearLinkArm();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
    PaimonNotify::show(Localization::get().getString("menu_layout.reset_done"), NotificationIcon::Info);
}

void MainMenuLayoutEditor::onToggleTopPanel(CCObject*) {
    m_topPanelHidden = !m_topPanelHidden;
    if (m_topPanel) m_topPanel->setVisible(!m_topPanelHidden);
    if (m_topPanelCollapsed) m_topPanelCollapsed->setVisible(m_topPanelHidden);
}

void MainMenuLayoutEditor::onToggleRightPanel(CCObject*) {
    this->animateRightPanel(!m_rightPanelHidden);
}

void MainMenuLayoutEditor::onUndo(CCObject*) {
    this->undoHistory();
}

void MainMenuLayoutEditor::onRedo(CCObject*) {
    this->redoHistory();
}

void MainMenuLayoutEditor::onLayerUp(CCObject*) {
    auto selection = this->selectionStates();
    for (auto* state : selection) {
        if (!state || !state->target.node) continue;
        int current = state->target.node->getZOrder();
        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                current = shape->layer;
            }
        } else if (auto* layout = this->currentLayoutFor(*state)) {
            current = layout->layer;
        }
        this->updateNodeLayer(*state, current + 1);
    }
    this->syncStateFromNodes();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onLayerDown(CCObject*) {
    auto selection = this->selectionStates();
    for (auto* state : selection) {
        if (!state || !state->target.node) continue;
        int current = state->target.node->getZOrder();
        if (isShapeNode(state->target.node)) {
            if (auto* shape = this->shapeLayout(*state)) {
                current = shape->layer;
            }
        } else if (auto* layout = this->currentLayoutFor(*state)) {
            current = layout->layer;
        }
        this->updateNodeLayer(*state, current - 1);
    }
    this->syncStateFromNodes();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::openContextEditor(ButtonState& state) {
    if (!state.target.node) return;
    if (isShapeNode(state.target.node)) {
        this->openShapeEditor(state);
        return;
    }

    auto* layout = this->currentLayoutFor(state);
    if (!layout) return;

    bool allowFont = typeinfo_cast<CCLabelBMFont*>(state.target.node) != nullptr;
    bool allowColor = layout->hasColor || allowFont || typeinfo_cast<CCMenuItemSprite*>(state.target.node) != nullptr;

    WeakRef<MainMenuLayoutEditor> self = this;
    auto* popup = MainMenuContextEditPopup::create(state.target.label, *layout, allowColor, allowFont, [self, key = state.target.key, node = state.target.node](MenuButtonLayout const& newLayout) {
        auto ref = self.lock();
        auto* editor = static_cast<MainMenuLayoutEditor*>(ref.data());
        if (!editor || !editor->getParent() || !node) return;
        editor->m_liveLayouts[key] = newLayout;
        MainMenuLayoutManager::applyLayout(node, newLayout);
        editor->syncStateFromNodes();
        editor->commitHistorySnapshot();
        editor->updateHighlights();
        editor->updateSelectionUI();
        editor->updateStatusText();
    });
    if (popup) popup->show();
}

void MainMenuLayoutEditor::onEditSelected(CCObject*) {
    if (!m_selected) return;
    this->openContextEditor(*m_selected);
}

void MainMenuLayoutEditor::onLinkSelected(CCObject*) {
    this->linkSelection();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onUnlinkSelected(CCObject*) {
    this->unlinkSelection();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onToggleHiddenSelected(CCObject*) {
    this->toggleHiddenSelection();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onDeleteSelected(CCObject*) {
    this->deleteSelection();
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onScaleDownSelected(CCObject*) {
    this->applyScaleFactor(1.f / kScaleButtonFactor);
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onScaleUpSelected(CCObject*) {
    this->applyScaleFactor(kScaleButtonFactor);
    this->commitHistorySnapshot();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();
}

void MainMenuLayoutEditor::onToggleResizeMode(CCObject*) {
    m_freeResizeMode = !m_freeResizeMode;
    this->refreshResizeModeButton();
}

void MainMenuLayoutEditor::addShape(DrawShapeKind kind) {
    auto* layer = this->getTargetLayer();
    if (!layer) return;

    auto shapes = this->currentShapes();

    DrawShapeLayout shape;
    shape.id = MainMenuLayoutManager::createShapeID();
    shape.kind = kind;
    shape.position = ccp(CCDirector::sharedDirector()->getWinSize().width * 0.5f, CCDirector::sharedDirector()->getWinSize().height * 0.5f - 18.f);
    shape.scale = 1.f;
    shape.opacity = 0.78f;
    shape.hidden = false;
    shape.width = kind == DrawShapeKind::Circle ? 88.f : 128.f;
    shape.height = kind == DrawShapeKind::Circle ? 88.f : 72.f;
    shape.cornerRadius = 20.f;
    shape.color = kind == DrawShapeKind::Rectangle ? ccColor3B{100, 228, 255} : kind == DrawShapeKind::Circle ? ccColor3B{255, 208, 102} : ccColor3B{180, 120, 255};
    int nextLayer = 0;
    for (auto const& entry : shapes) {
        nextLayer = std::max(nextLayer, entry.layer + 1);
    }
    shape.layer = nextLayer;
    shape.zOrder = nextLayer;

    shapes.push_back(shape);
    m_shapeLayouts = shapes;
    MainMenuLayoutManager::get().syncShapes(layer, shapes);
    this->collectButtons();
    this->commitHistorySnapshot();

    auto targetKey = fmt::format("MenuLayer/shapes/{}", shape.id);
    m_selected = nullptr;
    for (auto& button : m_buttons) {
        if (button.target.key == targetKey) {
            m_selected = &button;
            break;
        }
    }

    this->syncStateFromNodes();
    this->updateHighlights();
    this->updateSelectionUI();
    this->updateStatusText();

    if (m_selected) {
        this->openShapeEditor(*m_selected);
    }
}

DrawShapeLayout* MainMenuLayoutEditor::shapeLayout(ButtonState& state) {
    if (!isShapeNode(state.target.node)) return nullptr;

    constexpr char const* prefix = "MenuLayer/shapes/";
    auto key = state.target.key;
    if (key.rfind(prefix, 0) != 0) return nullptr;
    auto id = key.substr(std::char_traits<char>::length(prefix));

    for (auto& shape : m_shapeLayouts) {
        if (shape.id == id) return &shape;
    }
    return nullptr;
}

void MainMenuLayoutEditor::openShapeEditor(ButtonState& state) {
    auto* shape = this->shapeLayout(state);
    if (!shape) return;

    WeakRef<MainMenuLayoutEditor> self = this;
    auto* popup = MainMenuDrawShapePopup::create(*shape, [self, node = state.target.node, shapeID = shape->id](DrawShapeLayout const& layout) {
        auto editorRef = self.lock();
        auto* editor = static_cast<MainMenuLayoutEditor*>(editorRef.data());
        if (!editor || !editor->getParent() || !node) return;

        for (auto& entry : editor->m_shapeLayouts) {
            if (entry.id == shapeID) {
                entry = layout;
                break;
            }
        }

        MainMenuLayoutManager::applyShapeLayout(node, layout);
        editor->syncStateFromNodes();
        editor->commitHistorySnapshot();
        editor->updateHighlights();
        editor->updateSelectionUI();
        editor->updateStatusText();
    });

    if (popup) {
        popup->show();
    }
}

void MainMenuLayoutEditor::onAddRect(CCObject*) {
    this->addShape(DrawShapeKind::Rectangle);
}

void MainMenuLayoutEditor::onAddRound(CCObject*) {
    this->addShape(DrawShapeKind::RoundedRect);
}

void MainMenuLayoutEditor::onAddCircle(CCObject*) {
    this->addShape(DrawShapeKind::Circle);
}

void MainMenuLayoutEditor::onEditShape(CCObject*) {
    if (!m_selected || !m_selected->target.node || !isShapeNode(m_selected->target.node)) return;
    this->openShapeEditor(*m_selected);
}

void MainMenuLayoutEditor::saveAndClose() {
    this->close(true);
}

void MainMenuLayoutEditor::cancelAndClose() {
    this->close(false);
}

void MainMenuLayoutEditor::updateToolbarPage() {
    int totalButtons = static_cast<int>(m_toolbarButtons.size());
    int totalPages = std::max(1, (totalButtons + m_toolbarVisibleCount - 1) / m_toolbarVisibleCount);
    m_toolbarPage = std::clamp(m_toolbarPage, 0, totalPages - 1);

    int startIdx = m_toolbarPage * m_toolbarVisibleCount;
    int endIdx = std::min(startIdx + m_toolbarVisibleCount, totalButtons);
    int visibleCount = endIdx - startIdx;

    // Measure total width of visible buttons
    constexpr float kBtnGap = 8.f;
    float totalWidth = 0.f;
    for (int i = startIdx; i < endIdx; ++i) {
        totalWidth += nodeVisualWidth(m_toolbarButtons[i]);
    }
    if (visibleCount > 1) {
        totalWidth += kBtnGap * static_cast<float>(visibleCount - 1);
    }

    // Center visible buttons in the area
    float cursorX = m_toolbarBtnStartX + std::max(0.f, (m_toolbarBtnAreaWidth - totalWidth) * 0.5f);

    for (int i = 0; i < totalButtons; ++i) {
        auto* btn = m_toolbarButtons[i];
        if (i >= startIdx && i < endIdx) {
            btn->setVisible(true);
            btn->setEnabled(true);
            float bw = nodeVisualWidth(btn);
            btn->setPosition({ cursorX + bw * 0.5f, m_toolbarBtnRowY });
            cursorX += bw + kBtnGap;
        } else {
            btn->setVisible(false);
            btn->setEnabled(false);
        }
    }

    // Update arrow visibility
    if (m_toolbarPrevBtn) {
        setMenuItemEnabledVisual(m_toolbarPrevBtn, m_toolbarPage > 0);
    }
    if (m_toolbarNextBtn) {
        setMenuItemEnabledVisual(m_toolbarNextBtn, m_toolbarPage < totalPages - 1);
    }

    // Update page indicator
    if (m_toolbarPageLabel) {
        m_toolbarPageLabel->setString(fmt::format("{}/{}", m_toolbarPage + 1, totalPages).c_str());
    }
}

void MainMenuLayoutEditor::onToolbarPrev(CCObject*) {
    if (m_toolbarPage > 0) {
        --m_toolbarPage;
        this->updateToolbarPage();
    }
}

void MainMenuLayoutEditor::onToolbarNext(CCObject*) {
    int totalButtons = static_cast<int>(m_toolbarButtons.size());
    int totalPages = std::max(1, (totalButtons + m_toolbarVisibleCount - 1) / m_toolbarVisibleCount);
    if (m_toolbarPage < totalPages - 1) {
        ++m_toolbarPage;
        this->updateToolbarPage();
    }
}

} // namespace paimon::menu_layout
