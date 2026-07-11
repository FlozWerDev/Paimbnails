#pragma once

#include <Geode/Geode.hpp>
#include <memory>
#include <vector>

// CCLayer (not Popup) so it can hide entirely for a clean screenshot.
// After capture, plays a fly-to-corner animation and shows download/open buttons.
class CaptureOverlay : public cocos2d::CCLayer {
public:
    static void show();
    static void hideOverlay();

    virtual bool init() override;
    virtual void onExit() override;

    virtual bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    virtual void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {}
    virtual void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {}
    virtual void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {}

    virtual void registerWithTouchDispatcher() override;

    CREATE_FUNC(CaptureOverlay);

private:
    static CaptureOverlay* s_instance;

    cocos2d::CCLayerColor* m_dimBg = nullptr;

    geode::Ref<cocos2d::CCTexture2D> m_capturedTexture = nullptr;
    std::shared_ptr<uint8_t> m_rgbaBuffer = nullptr;
    int m_captureWidth = 0;
    int m_captureHeight = 0;

    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCMenu* m_previewMenu = nullptr;
    cocos2d::CCNode* m_previewCard = nullptr;
    cocos2d::CCClippingNode* m_flyClipNode = nullptr;
    cocos2d::CCNodeRGBA* m_flyCardBg = nullptr;
    bool m_isClosing = false;
    // Identity-only snapshot of the scene the overlay was opened over; never
    // dereferenced. OverlayManager persists across scenes, so the card would
    // otherwise survive a scene transition.
    cocos2d::CCScene* m_ownerScene = nullptr;
    bool m_docked = false;
    // Alerts already visible when the card docked (identity only); a popup not
    // in this set means the user moved on -> the card dismisses itself.
    std::vector<cocos2d::CCNode*> m_alertsAtDock;

    void onClose(cocos2d::CCObject* sender);
    void onDownload(cocos2d::CCObject* sender);
    void onOpenFolder(cocos2d::CCObject* sender);

    void startAutoDismissTimer();
    void onAutoDismiss(float dt);
    void checkSceneChanged(float dt);

    void triggerCaptureProcess(float dt = 0.f);
    void playFlyToBottomRightAnimation();
    void revealPreviewControls();
    void finishClose();
};
