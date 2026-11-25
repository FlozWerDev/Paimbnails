#pragma once

#include "LayoutEditorWidgets.hpp"
#include "../services/MainMenuLayoutManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace paimon::menu_layout {

class MainMenuLayoutEditor : public cocos2d::CCLayer {
public:
    using cocos2d::CCLayer::init;
    static MainMenuLayoutEditor* create(cocos2d::CCNode* root);
    static MainMenuLayoutEditor* getActive();
    static bool isActive();
    static void open(cocos2d::CCNode* root);

    void saveAndClose();
    void cancelAndClose();
    cocos2d::CCNode* getTargetRoot() const;

    ~MainMenuLayoutEditor() override;

protected:
    bool init(cocos2d::CCNode* root);
    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double p1) override;
    void update(float dt) override;

private:
    struct EditorElement {
        bool isShape = false;
        std::string id;
        EditableMenuButton target;
        FriendlyElementInfo info;
    };

    enum class DragMode {
        None,
        Move,
        Rotate,
        ScaleTL,
        ScaleTR,
        ScaleBL,
        ScaleBR,
        ScaleL,
        ScaleR,
        ScaleT,
        ScaleB,
        Marquee,
    };

    enum class InspectorTab {
        Transform = 0,
        Style = 1,
        Group = 2,
    };

    enum class LayerFilter {
        All = 0,
        Buttons = 1,
        Art = 2,
        Shapes = 3,
        Hidden = 4,
    };

    struct StyleClipboard {
        bool valid = false;
        float scaleX = 1.f;
        float scaleY = 1.f;
        float rotation = 0.f;
        float opacity = 1.f;
        bool hasColor = false;
        cocos2d::ccColor3B color = { 255, 255, 255 };
        std::string fontFile;
        float strokeWidth = 0.f;
        cocos2d::ccColor3B strokeColor = { 255, 255, 255 };
        float cornerRadius = 18.f;
    };

    // Initialization & scene collection
    void collectElements();
    void rebuildShapeElements();
    void disableTargetMenus();
    void buildUI();
    void buildTopBar();
    void buildDock();
    void buildShapeBar();
    void buildLayersPanel();
    void buildInspector();
    void buildPreviewBadge();

    // Layout & HUD management
    void placeInspector();
    void captureInterfaceNodes(cocos2d::CCNode* node);
    void applyInterfaceOpacity();
    void syncHudVisibility();
    void beginClose(bool saved);
    void setPreviewMode(bool on);
    void setLayersOpen(bool open);
    void setShapeBarOpen(bool open);
    void setCollapsed(bool collapsed);
    void setInspectorTab(InspectorTab tab);
    void setLayerFilter(LayerFilter filter);
    void rebuildLayersList();
    void rebuildGrid();
    void updateAnimations(float dt);
    void animateButtonToLive(EditorElement const& elem);

    // Element & selection queries
    EditorElement* findElement(std::string const& id);
    EditorElement const* findElement(std::string const& id) const;
    EditorElement* primaryElement();
    EditorElement const* primaryElement() const;
    std::vector<EditorElement*> selectedElements();
    bool isSelected(std::string const& id) const;
    void selectSingle(std::string const& id);
    void toggleSelection(std::string const& id);
    void clearSelection();
    void selectAllElements();
    void selectLinkGroup(std::string const& group);

    // Geometry & hit testing
    cocos2d::CCRect elementRect(EditorElement const& elem) const;
    cocos2d::CCRect outlineRect(EditorElement const& elem) const;
    bool isBackgroundElement(EditorElement const& elem) const;
    EditorElement* findElementAt(cocos2d::CCPoint worldPos);
    bool isPointInHud(cocos2d::CCPoint worldPos) const;
    bool hasModalPopupOnTop();

    // Live property accessors (unified across Buttons & Shapes)
    bool isLocked(EditorElement const& elem) const;
    bool isHidden(EditorElement const& elem) const;
    float getOpacity(EditorElement const& elem) const;
    float getRotation(EditorElement const& elem) const;
    int getLayerZ(EditorElement const& elem) const;
    std::string getLinkGroup(EditorElement const& elem) const;
    cocos2d::ccColor3B getColor(EditorElement const& elem, bool* hasColorOut = nullptr) const;
    void applyElementToScene(EditorElement const& elem);
    std::vector<DrawShapeLayout> collectSceneShapesVector() const;

    // Transform & editing operations
    cocos2d::CCPoint snapWorld(EditorElement const& elem, cocos2d::CCPoint proposedWorld);
    float snapToGrid(float value) const;
    void moveElementAndPeers(EditorElement& elem, cocos2d::CCPoint deltaWorld, bool includeGroupPeers);
    void nudgeSelection(cocos2d::CCPoint deltaWorld);
    void scaleSelection(float factor);
    void setSelectionScale(float sx, float sy);
    void rotateSelection(float deltaDegrees);
    void setSelectionOpacity(float opacity);
    void setSelectionLayer(int deltaZ);
    void toggleSelectionLock();
    void toggleSelectionHidden();
    void centerSelection(bool horizontal, bool vertical);
    void alignSelection(int mode); // 0=L,1=CX,2=R,3=T,4=CY,5=B
    void distributeSelection(bool horizontal);
    void matchSelectionScale();
    void matchSelectionOpacity();
    void linkSelectedTogether();
    void unlinkSelected();
    void copySelectedStyle();
    void pasteStyleToSelected();
    void resetElementToDefault(EditorElement& elem);
    void addDrawShape(DrawShapeKind kind);
    void duplicateSelectedShape();
    void deleteSelectedShapes();
    void pickSelectedColor(bool strokeColor = false);
    void resetSelectedColor();
    void pickSelectedFont();
    void resetSelectedFont();

    // History & snapshots
    LayoutSnapshot buildFullSnapshot() const;
    void pushHistory();
    void applyHistory(LayoutSnapshot const& snapshot);
    void undo();
    void redo();
    bool isDirty() const;

    // Drawing & UI refresh
    void redraw();
    void refreshHudState();
    void refreshInspector();
    void updateLiveBadge(std::string const& text, bool visible);
    void updateHint();

    // Menu callbacks
    void onSave(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onUndo(cocos2d::CCObject*);
    void onRedo(cocos2d::CCObject*);
    void onToggleGrid(cocos2d::CCObject*);
    void onToggleSnap(cocos2d::CCObject*);
    void onOpenCanvasSettings(cocos2d::CCObject*);
    void onToggleMultiSelect(cocos2d::CCObject*);
    void onToggleShapeBar(cocos2d::CCObject*);
    void onToggleLayers(cocos2d::CCObject*);
    void onOpenPresets(cocos2d::CCObject*);
    void onResetAll(cocos2d::CCObject*);
    void onTogglePreview(cocos2d::CCObject*);
    void onToggleDock(cocos2d::CCObject*);
    void onDeselect(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);

    // Target state
    geode::WeakRef<cocos2d::CCNode> m_root;
    std::string m_sceneName;
    std::vector<EditorElement> m_elements;
    std::unordered_map<std::string, MenuButtonLayout> m_liveButtons;
    std::unordered_map<std::string, MenuButtonLayout> m_initialButtons;
    std::unordered_map<std::string, DrawShapeLayout> m_liveShapes;
    std::vector<std::string> m_shapeOrder;
    std::vector<DrawShapeLayout> m_initialShapes;
    std::vector<geode::Ref<cocos2d::CCMenu>> m_disabledMenus;

    struct LayoutTransition {
        MenuButtonLayout from;
        float elapsed = 0.f;
        float duration = 0.4f;
    };
    std::unordered_map<std::string, LayoutTransition> m_transitions;

    // Selection & mode state
    std::vector<std::string> m_selectedIds;
    std::string m_hoveredId;
    bool m_multiSelectMode = false;
    bool m_uniformScale = true;
    bool m_alignToCanvas = false;
    bool m_previewMode = false;
    bool m_layersOpen = false;
    bool m_shapeBarOpen = false;
    bool m_collapsed = false;
    InspectorTab m_inspTab = InspectorTab::Transform;
    LayerFilter m_layerFilter = LayerFilter::All;
    CanvasSettings m_settings;
    StyleClipboard m_styleClipboard;
    int m_nextShapeCounter = 1;

    // Lifecycle & animation state
    bool m_closing = false;
    bool m_saved = false;
    float m_interfaceElapsed = 0.f;
    float m_interfaceOpacity = 0.f;
    float m_closeOpacity = 1.f;
    float m_hudGhostFactor = 1.f;
    std::vector<std::pair<geode::WeakRef<cocos2d::CCNodeRGBA>, uint8_t>> m_interfaceNodes;

    // Canvas draw nodes
    cocos2d::CCLayerColor* m_dark = nullptr;
    cocos2d::CCNode* m_hudRoot = nullptr;
    cocos2d::CCDrawNode* m_grid = nullptr;
    cocos2d::CCDrawNode* m_highlights = nullptr;
    cocos2d::CCDrawNode* m_guideX = nullptr;
    cocos2d::CCDrawNode* m_guideY = nullptr;
    cocos2d::CCDrawNode* m_outline = nullptr;
    cocos2d::CCDrawNode* m_grip = nullptr;
    cocos2d::CCDrawNode* m_marqueeDraw = nullptr;

    // Top bar
    cocos2d::CCNode* m_topBar = nullptr;
    cocos2d::CCMenu* m_topMenu = nullptr;
    cocos2d::CCLabelBMFont* m_titleLbl = nullptr;
    cocos2d::CCLabelBMFont* m_dirtyDot = nullptr;
    cocos2d::CCLabelBMFont* m_hintLbl = nullptr;
    float m_hintMaxWidth = 220.f;
    CCMenuItemSpriteExtra* m_previewBtn = nullptr;

    // Preview restore badge
    cocos2d::CCNode* m_previewPill = nullptr;
    cocos2d::CCMenu* m_previewPillMenu = nullptr;

    // Live transform readout badge
    cocos2d::CCNode* m_liveBadge = nullptr;
    cocos2d::CCLabelBMFont* m_liveBadgeLbl = nullptr;

    // Bottom dock
    cocos2d::CCNode* m_dock = nullptr;
    cocos2d::CCMenu* m_dockMenu = nullptr;
    CCMenuItemSpriteExtra* m_undoBtn = nullptr;
    CCMenuItemSpriteExtra* m_redoBtn = nullptr;
    CCMenuItemSpriteExtra* m_gridBtn = nullptr;
    CCMenuItemSpriteExtra* m_snapBtn = nullptr;
    CCMenuItemSpriteExtra* m_multiBtn = nullptr;
    CCMenuItemSpriteExtra* m_shapeToolBtn = nullptr;
    CCMenuItemSpriteExtra* m_layersBtn = nullptr;
    CCMenuItemSpriteExtra* m_collapseBtn = nullptr;
    cocos2d::CCSprite* m_collapseArrow = nullptr;

    // Quick Shape-add floating bar
    cocos2d::CCNode* m_shapeBar = nullptr;
    cocos2d::CCMenu* m_shapeBarMenu = nullptr;

    // Left Layers drawer
    cocos2d::CCNode* m_layersPanel = nullptr;
    cocos2d::CCMenu* m_layersMenu = nullptr;
    geode::ScrollLayer* m_layersScroll = nullptr;
    cocos2d::CCLabelBMFont* m_layersCountLbl = nullptr;
    std::vector<std::pair<LayerFilter, CCMenuItemSpriteExtra*>> m_layerFilterBtns;

    // Right Inspector card
    cocos2d::CCNode* m_inspector = nullptr;
    cocos2d::CCMenu* m_inspMenu = nullptr;
    cocos2d::CCLabelBMFont* m_inspName = nullptr;
    cocos2d::CCLabelBMFont* m_inspSub = nullptr;
    cocos2d::CCNode* m_inspSingleRoot = nullptr;
    cocos2d::CCNode* m_inspMultiRoot = nullptr;
    cocos2d::CCNode* m_tabTransformRoot = nullptr;
    cocos2d::CCNode* m_tabStyleRoot = nullptr;
    cocos2d::CCNode* m_tabGroupRoot = nullptr;
    CCMenuItemSpriteExtra* m_tabBtns[3] = { nullptr, nullptr, nullptr };

    // Transform tab controls
    cocos2d::CCLabelBMFont* m_valX = nullptr;
    cocos2d::CCLabelBMFont* m_valY = nullptr;
    cocos2d::CCLabelBMFont* m_lblScaleA = nullptr;
    cocos2d::CCLabelBMFont* m_valScaleA = nullptr;
    cocos2d::CCLabelBMFont* m_lblScaleB = nullptr;
    cocos2d::CCLabelBMFont* m_valScaleB = nullptr;
    cocos2d::CCNode* m_rowScaleB = nullptr;
    CCMenuItemSpriteExtra* m_aspectLockBtn = nullptr;
    cocos2d::CCLabelBMFont* m_valRot = nullptr;
    cocos2d::CCLabelBMFont* m_valLayer = nullptr;

    // Style tab controls
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_valOpacity = nullptr;
    cocos2d::CCNodeRGBA* m_colorSwatch = nullptr;
    cocos2d::CCNode* m_fontRow = nullptr;
    cocos2d::CCLabelBMFont* m_valFont = nullptr;
    cocos2d::CCNode* m_shapeStyleRoot = nullptr;
    CCMenuItemSpriteExtra* m_shapeKindBtns[3] = { nullptr, nullptr, nullptr };
    cocos2d::CCLabelBMFont* m_valRadius = nullptr;
    cocos2d::CCLabelBMFont* m_valStroke = nullptr;
    cocos2d::CCNodeRGBA* m_strokeSwatch = nullptr;
    CCMenuItemSpriteExtra* m_pasteStyleBtn = nullptr;

    // Group tab controls
    CCMenuItemSpriteExtra* m_hideBtn = nullptr;
    CCMenuItemSpriteExtra* m_lockBtn = nullptr;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_groupBtns;

    // Multi-selection controls
    CCMenuItemSpriteExtra* m_alignTargetBtn = nullptr;
    CCMenuItemSpriteExtra* m_distHBtn = nullptr;
    CCMenuItemSpriteExtra* m_distVBtn = nullptr;

    // Drag / interaction state
    DragMode m_drag = DragMode::None;
    cocos2d::CCPoint m_touchStart = { 0.f, 0.f };
    cocos2d::CCPoint m_touchCur = { 0.f, 0.f };
    cocos2d::CCPoint m_pivotWorld = { 0.f, 0.f };
    std::unordered_map<std::string, cocos2d::CCPoint> m_dragStartWorldPos;
    float m_dragStartDist = 1.f;
    float m_dragStartScaleX = 1.f;
    float m_dragStartScaleY = 1.f;
    float m_dragStartWidth = 100.f;
    float m_dragStartHeight = 60.f;
    float m_dragStartAngle = 0.f;
    float m_dragStartRot = 0.f;
    bool m_dragChanged = false;
    bool m_marqueeAdd = false;
    bool m_opacityDirty = false;
    float m_opacitySettleTimer = 0.f;

    // Undo / Redo history
    std::vector<LayoutSnapshot> m_history;
    std::size_t m_historyCursor = 0;
    bool m_applyingHistory = false;
};

} // namespace paimon::menu_layout
