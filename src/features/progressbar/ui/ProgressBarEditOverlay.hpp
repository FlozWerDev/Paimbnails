#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

// ─────────────────────────────────────────────────────────────
// ProgressBarEditOverlay: full-screen CCLayer that hides the
// PauseLayer + its owner popup and exposes handles the user can
// drag to move/resize/rotate the progress bar, the percentage
// label and any user-added image decorations.
// ─────────────────────────────────────────────────────────────

class ProgressBarEditOverlay : public cocos2d::CCLayer {
public:
    static ProgressBarEditOverlay* create();

    // Activates edit mode. Detaches every non-gameplay node from
    // the scene so only PlayLayer stays visible, then installs
    // this overlay as a direct child.
    static void enterEditMode();

    // Deactivates edit mode, restoring previously-detached nodes.
    static void exitEditMode();

    static bool isActive();

    // Which element the user is currently interacting with.
    enum class Target {
        None,
        Bar,
        Label,
        Decoration,   // uses m_dragDecoIndex
    };

    // What action is the current drag performing?
    enum class Action {
        None,
        Move,
        ResizeLen,    // bar-only (axis length)
        ResizeThick,  // bar-only (axis thickness)
        ResizeUniform,// label or decoration uniform scale
        Rotate,
        Delete,       // decoration remove (click, not drag)
    };

protected:
    bool init() override;

    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyBackClicked() override;

    void update(float dt) override;

    void buildToolbar();
    void rebuildSelectionUI();     // called by update() each frame
    void clearSelectionUI();

    void onDone(cocos2d::CCObject*);
    void onFont(cocos2d::CCObject*);
    void onResetPosition(cocos2d::CCObject*);
    void onAddImage(cocos2d::CCObject*);

    // Capture current values of the selected element so drags are additive.
    void storeOrigValues();

    // Which element is currently selected (shows native GD buttons around it)
    Target m_selectedTarget = Target::None;
    int    m_selectedDecoIndex = -1;

    // Container for selection outline + native GD button handles.
    cocos2d::CCNode* m_selContainer = nullptr;
    cocos2d::CCRect  m_selRect;         // world-space AABB of selected element

    // Active drag state (body or a handle was grabbed).
    Target m_dragTarget = Target::None;
    Action m_dragAction = Action::None;
    int    m_dragDecoIndex = -1;
    cocos2d::CCPoint m_touchStart{};
    cocos2d::CCPoint m_anchorWorld{};  // pivot / world pos at drag start

    // snapshot of value being modified so drags are additive
    cocos2d::CCPoint m_origPos{};
    float m_origScaleLen   = 1.f;
    float m_origScaleThick = 1.f;
    float m_origUniformSc  = 1.f;
    float m_origRotation   = 0.f;

    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
};
