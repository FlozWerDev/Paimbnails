#pragma once
#include <Geode/Geode.hpp>

class ProgressBarEditOverlay : public cocos2d::CCLayer {
public:
    static ProgressBarEditOverlay* create();

    static void enterEditMode();

    // restores previously-detached nodes.
    static void exitEditMode();

    enum class Target {
        None,
        Bar,
        Label,
        Decoration,   // uses m_selecteddecoindex
    };

    enum class Action {
        None,
        Move,
        ResizeUniform, // uniform scale (bar = both axes, label/deco = scale)
        Rotate,
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

    // capture current values of the selected element so drags are
    // additive (delta-based) rather than absolute.
    void storeOrigValues();

    void validateSelection();

    // selection shows native gd buttons.
    Target m_selectedTarget = Target::None;
    int    m_selectedDecoIndex = -1;

    cocos2d::CCNode* m_selContainer = nullptr;

    Target m_dragTarget = Target::None;
    Action m_dragAction = Action::None;
    int    m_dragDecoIndex = -1;
    cocos2d::CCPoint m_touchStart{};
    cocos2d::CCPoint m_anchorWorld{};  // pivot / world pos at drag start

    cocos2d::CCPoint m_origPos{};
    float m_origScaleLen   = 1.f;
    float m_origScaleThick = 1.f;
    float m_origUniformSc  = 1.f;
    float m_origRotation   = 0.f;

    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
};
