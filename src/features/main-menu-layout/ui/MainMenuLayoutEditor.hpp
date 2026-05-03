#pragma once

#include "MainMenuContextEditPopup.hpp"
#include "MainMenuDrawShapePopup.hpp"
#include "../services/MainMenuLayoutManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>

#include <cstddef>
#include <vector>

namespace paimon::menu_layout {

class MainMenuLayoutEditor : public cocos2d::CCLayer {
 public:
    enum class ResizeHandle {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    static MainMenuLayoutEditor* create(MenuLayer* layer);
    static MainMenuLayoutEditor* getActive();
    static bool isActive();
    static void open(MenuLayer* layer);
    static void toggle(MenuLayer* layer);

    void saveAndClose();
    void cancelAndClose();
    MenuLayer* getTargetLayer() const;

    ~MainMenuLayoutEditor() override;

protected:
    bool init(MenuLayer* layer);
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void keyBackClicked() override;
    void update(float dt) override;

private:
    struct ButtonState {
        EditableMenuButton target;
        MenuButtonLayout initialLayout;
        float opacity = 1.f;
        bool hidden = false;
    };

    enum class DragMode {
        None,
        Move,
        Scale,
    };

    struct TransformSnapshot {
        cocos2d::CCPoint worldPosition = { 0.f, 0.f };
        cocos2d::CCPoint localPosition = { 0.f, 0.f };
        float scale = 1.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
    };

    void collectButtons();
    void disableTargetMenus();
    void buildUI();
    void updateHighlights();
    void updateSelectionUI();
    void updateStatusText();
    void refreshRightPanel();
    LayoutSnapshot buildSnapshot() const;
    void setSelected(ButtonState* state);
    std::string selectionLayerLabel() const;
    std::string selectionLinkGroup() const;
    std::vector<ButtonState*> selectionStates();
    std::vector<ButtonState const*> selectionStates() const;
    cocos2d::CCRect selectionBounds() const;
    std::string linkGroupFor(ButtonState const& state) const;
    std::vector<EditableMenuButton> currentButtons() const;
    std::vector<DrawShapeLayout> currentShapes() const;
    void syncStateFromNodes();
    void openPresetPicker(bool saveMode);
    void applyPreviewState(ButtonState& state);
    void applyEditorSnapshot(LayoutSnapshot const& snapshot);
    MenuButtonLayout buildLayout(ButtonState const& state) const;
    void setLinkGroup(ButtonState& state, std::string const& group);
    std::string nextLinkGroup() const;
    void clearLinkArm();
    void linkSelection();
    void unlinkSelection();
    void toggleHiddenSelection();
    void deleteSelection();
    void applyOpacityDelta(float delta);
    void applyScaleFactor(float factor);
    void commitHistorySnapshot();
    bool canUndo() const;
    bool canRedo() const;
    void undoHistory();
    void redoHistory();
    void beginDrag(DragMode mode, cocos2d::CCPoint worldPos, ResizeHandle handle = ResizeHandle::None);
    void applyMoveSelection(cocos2d::CCPoint worldPos);
    void applyResizeSelection(cocos2d::CCPoint worldPos);
    void animateRightPanel(bool hidden);
    void hideGuides();
    void updateGuides(bool showX, bool showY, float x, float y);
    void restoreInitialLayouts();
    void resetSelectedToDefault();
    void resetAllToDefault();
    void close(bool save);
    void addShape(DrawShapeKind kind);
    void openShapeEditor(ButtonState& state);
    void openContextEditor(ButtonState& state);
    DrawShapeLayout* shapeLayout(ButtonState& state);
    MenuButtonLayout* currentLayoutFor(ButtonState& state);
    void updateNodeLayer(ButtonState& state, int layer);

    ButtonState* findButtonAt(cocos2d::CCPoint worldPos);
    bool isTouchOnToolbar(cocos2d::CCPoint worldPos) const;
    bool isTouchOnRightPanel(cocos2d::CCPoint worldPos) const;
    bool isTouchOnScaleGrip(cocos2d::CCPoint worldPos) const;
    ResizeHandle resizeHandleAt(cocos2d::CCPoint worldPos) const;
    bool isTouchOnDeleteGrip(cocos2d::CCPoint worldPos) const;
    bool isTouchOnOpacityDownGrip(cocos2d::CCPoint worldPos) const;
    bool isTouchOnOpacityUpGrip(cocos2d::CCPoint worldPos) const;
    cocos2d::CCRect buttonRect(ButtonState const& state) const;
    cocos2d::CCPoint snappedLocalPosition(ButtonState* state, cocos2d::CCPoint proposedWorld) const;

    void onSave(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onSavePreset(cocos2d::CCObject*);
    void onLoadPreset(cocos2d::CCObject*);
    void onResetSelected(cocos2d::CCObject*);
    void onResetAll(cocos2d::CCObject*);
    void onAddRect(cocos2d::CCObject*);
    void onAddRound(cocos2d::CCObject*);
    void onAddCircle(cocos2d::CCObject*);
    void onEditShape(cocos2d::CCObject*);
    void onToggleTopPanel(cocos2d::CCObject*);
    void onToggleRightPanel(cocos2d::CCObject*);
    void onUndo(cocos2d::CCObject*);
    void onRedo(cocos2d::CCObject*);
    void onLayerUp(cocos2d::CCObject*);
    void onLayerDown(cocos2d::CCObject*);
    void onEditSelected(cocos2d::CCObject*);
    void onLinkSelected(cocos2d::CCObject*);
    void onUnlinkSelected(cocos2d::CCObject*);
    void onToggleHiddenSelected(cocos2d::CCObject*);
    void onDeleteSelected(cocos2d::CCObject*);
    void onScaleDownSelected(cocos2d::CCObject*);
    void onScaleUpSelected(cocos2d::CCObject*);
    void onToggleResizeMode(cocos2d::CCObject*);
    void onToolbarPrev(cocos2d::CCObject*);
    void onToolbarNext(cocos2d::CCObject*);
    void refreshResizeModeButton();
    void updateToolbarPage();

    geode::WeakRef<MenuLayer> m_layer;
    std::vector<ButtonState> m_buttons;
    std::vector<geode::Ref<cocos2d::CCMenu>> m_disabledMenus;
    std::vector<DrawShapeLayout> m_shapeLayouts;
    std::vector<DrawShapeLayout> m_initialShapes;
    std::unordered_map<std::string, MenuButtonLayout> m_liveLayouts;
    std::unordered_map<std::string, MenuButtonLayout> m_initialLiveLayouts;

    cocos2d::CCMenu* m_toolbar = nullptr;
    cocos2d::CCNode* m_topPanel = nullptr;
    cocos2d::CCNode* m_topPanelCollapsed = nullptr;
    cocos2d::CCNode* m_rightPanel = nullptr;
    cocos2d::CCMenu* m_rightMenu = nullptr;
    CCMenuItemSpriteExtra* m_undoButton = nullptr;
    CCMenuItemSpriteExtra* m_redoButton = nullptr;
    cocos2d::CCSprite* m_rightToggleArrow = nullptr;
    cocos2d::CCLabelBMFont* m_layerValueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_selectionCountLabel = nullptr;
    cocos2d::CCLabelBMFont* m_resizeModeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCDrawNode* m_allHighlights = nullptr;
    cocos2d::CCDrawNode* m_selectionOutline = nullptr;
    cocos2d::CCDrawNode* m_guideX = nullptr;
    cocos2d::CCDrawNode* m_guideY = nullptr;
    cocos2d::CCNode* m_scaleGrip = nullptr;
    cocos2d::CCNode* m_scaleGripTopLeft = nullptr;
    cocos2d::CCNode* m_scaleGripTopRight = nullptr;
    cocos2d::CCNode* m_scaleGripBottomLeft = nullptr;
    cocos2d::CCNode* m_deleteGrip = nullptr;
    cocos2d::CCNode* m_opacityDownGrip = nullptr;
    cocos2d::CCNode* m_opacityUpGrip = nullptr;

    ButtonState* m_selected = nullptr;
    DragMode m_dragMode = DragMode::None;
    ResizeHandle m_resizeHandle = ResizeHandle::None;
    std::unordered_map<std::string, TransformSnapshot> m_dragSnapshots;
    cocos2d::CCRect m_dragStartBounds = { 0.f, 0.f, 0.f, 0.f };
    cocos2d::CCPoint m_touchStart = { 0.f, 0.f };
    cocos2d::CCPoint m_itemStartWorld = { 0.f, 0.f };
    cocos2d::CCPoint m_scaleCenterWorld = { 0.f, 0.f };
    float m_itemStartScale = 1.f;
    std::string m_linkArmedGroup;
    std::string m_linkSourceKey;
    bool m_freeResizeMode = false;
    bool m_topPanelHidden = false;
    bool m_rightPanelHidden = false;
    bool m_isApplyingHistory = false;
    float m_rightPanelWidth = 92.f;
    float m_rightPanelShownX = 0.f;
    float m_rightPanelHiddenX = 0.f;
    std::vector<LayoutSnapshot> m_history;
    std::size_t m_historyCursor = 0;

    // Toolbar horizontal paging
    std::vector<CCMenuItemSpriteExtra*> m_toolbarButtons;
    CCMenuItemSpriteExtra* m_toolbarPrevBtn = nullptr;
    CCMenuItemSpriteExtra* m_toolbarNextBtn = nullptr;
    cocos2d::CCLabelBMFont* m_toolbarPageLabel = nullptr;
    int m_toolbarPage = 0;
    int m_toolbarVisibleCount = 3;
    float m_toolbarBtnRowY = 0.f;
    float m_toolbarBtnStartX = 0.f;
    float m_toolbarBtnAreaWidth = 0.f;
};

} // namespace paimon::menu_layout
