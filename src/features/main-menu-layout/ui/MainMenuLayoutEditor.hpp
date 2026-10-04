#pragma once

#include "../services/MainMenuLayoutManager.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace paimon::menu_layout {

class MainMenuLayoutEditor : public cocos2d::CCLayer {
public:
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
    struct Item {
        EditableMenuButton target;
    };

    enum class DragMode { None, Move, Scale, Rotate };

    void collectItems();
    void disableTargetMenus();
    void buildUI();
    void buildTopBar();
    void buildDock();
    void buildInspector();
    void redraw();
    void refreshInspector();
    void updateHint();
    void captureInterfaceNodes(cocos2d::CCNode* node);
    void applyInterfaceOpacity();
    void beginClose(bool saved);
    void animateLive(Item const& item);
    void updateAnimations(float dt);
    void rebuildGrid();
    void setGridEnabled(bool on);
    void setSnapEnabled(bool on);

    Item* selectedItem();
    void selectIndex(int index);
    cocos2d::CCRect itemRect(Item const& item) const;
    // itemrect plus outline margin: grip draw zone.
    cocos2d::CCRect outlineRect(Item const& item) const;
    cocos2d::CCPoint gripPos(Item const& item) const;
    cocos2d::CCPoint rotateGripPos(Item const& item) const;
    Item* findItemAt(cocos2d::CCPoint worldPos);
    bool isBackgroundItem(Item const& item) const;
    bool isSelectionLocked();

    MenuButtonLayout* liveLayout(Item const& item);
    void applyLive(Item const& item);
    cocos2d::CCPoint snapWorld(Item const& item, cocos2d::CCPoint proposedWorld);
    float snapToGrid(float value) const;
    void nudgeSelection(cocos2d::CCPoint deltaWorld);
    void scaleSelection(float factor);
    void rotateSelection(float deltaDegrees);
    void setSelectionOpacity(float opacity);
    void bringSelection(int direction);
    void toggleSelectionLock();
    void centerSelection(bool horizontal, bool vertical);
    void resetItemToDefault(Item const& item);

    void pushHistory();
    void applyHistory(LayoutSnapshot const& snapshot);
    void undo();
    void redo();
    LayoutSnapshot buildSnapshot() const;

    void onSave(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onResetSelected(cocos2d::CCObject*);
    void onResetAll(cocos2d::CCObject*);
    void onToggleHidden(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onSavePreset(cocos2d::CCObject*);
    void onLoadPreset(cocos2d::CCObject*);
    void openPresetPicker(bool saveMode);
    void onToggleBar(cocos2d::CCObject*);
    void onUndo(cocos2d::CCObject*);
    void onRedo(cocos2d::CCObject*);
    void onToggleGrid(cocos2d::CCObject*);
    void onToggleSnap(cocos2d::CCObject*);
    void onToggleLock(cocos2d::CCObject*);
    void onBringFront(cocos2d::CCObject*);
    void onBringBack(cocos2d::CCObject*);
    void onInspectorStep(cocos2d::CCObject*);

    geode::WeakRef<cocos2d::CCNode> m_root;
    std::vector<Item> m_items;
    std::unordered_map<std::string, MenuButtonLayout> m_live;
    std::unordered_map<std::string, MenuButtonLayout> m_initial;
    struct LayoutTransition {
        MenuButtonLayout from;
        float elapsed = 0.f;
        float duration;
    };
    std::unordered_map<std::string, LayoutTransition> m_transitions;
    // shapes on open: cancel restores them even mid-close.
    std::vector<DrawShapeLayout> m_initialShapes;
    bool m_closing = false;
    bool m_saved = false;
    float m_interfaceElapsed = 0.f;
    float m_interfaceOpacity = 0.f;
    float m_closeOpacity = 1.f;
    std::vector<std::pair<geode::WeakRef<cocos2d::CCNodeRGBA>, uint8_t>> m_interfaceNodes;
    int m_selected = -1;

    std::vector<geode::Ref<cocos2d::CCMenu>> m_disabledMenus;

    cocos2d::CCDrawNode* m_highlights = nullptr;
    cocos2d::CCDrawNode* m_outline = nullptr;
    cocos2d::CCDrawNode* m_grip = nullptr;
    cocos2d::CCDrawNode* m_guideX = nullptr;
    cocos2d::CCDrawNode* m_guideY = nullptr;
    cocos2d::CCDrawNode* m_grid = nullptr;
    cocos2d::CCLayerColor* m_dark = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCNode* m_topBar = nullptr;
    cocos2d::CCMenu* m_bar = nullptr;
    cocos2d::CCNode* m_barContainer = nullptr;
    CCMenuItemSpriteExtra* m_collapseBtn = nullptr;
    cocos2d::CCSprite* m_collapseArrow = nullptr;
    bool m_collapsed = false;
    Slider* m_opacitySlider = nullptr;

    // floating inspector for the active node.
    cocos2d::CCNode* m_inspector = nullptr;
    cocos2d::CCLabelBMFont* m_inspX = nullptr;
    cocos2d::CCLabelBMFont* m_inspY = nullptr;
    cocos2d::CCLabelBMFont* m_inspScale = nullptr;
    cocos2d::CCLabelBMFont* m_inspRot = nullptr;
    cocos2d::CCLabelBMFont* m_inspOpacity = nullptr;
    cocos2d::CCLabelBMFont* m_inspZ = nullptr;
    cocos2d::CCSprite* m_lockIcon = nullptr;
    cocos2d::CCSprite* m_gridIcon = nullptr;
    cocos2d::CCSprite* m_snapIcon = nullptr;

    bool m_gridOn = false;
    bool m_snapGrid = false;
    float m_gridStep = 24.f;

    DragMode m_drag = DragMode::None;
    cocos2d::CCPoint m_touchStart = { 0.f, 0.f };
    cocos2d::CCPoint m_itemStartWorld = { 0.f, 0.f };
    cocos2d::CCPoint m_scaleFixedWorld = { 0.f, 0.f };
    float m_scaleStartDist = 1.f;
    float m_itemStartScale = 1.f;
    float m_rotateStartAngle = 0.f;
    float m_itemStartRotation = 0.f;
    bool m_dragChanged = false;

    std::vector<LayoutSnapshot> m_history;
    std::size_t m_historyCursor = 0;
    bool m_applyingHistory = false;
};

} // namespace paimon::menu_layout
