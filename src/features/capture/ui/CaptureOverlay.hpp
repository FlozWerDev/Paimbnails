#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include <memory>

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

    cocos2d::CCNode* m_centerCard = nullptr;
    cocos2d::CCMenu* m_centerMenu = nullptr;

    geode::Ref<cocos2d::CCTexture2D> m_capturedTexture = nullptr;
    std::shared_ptr<uint8_t> m_rgbaBuffer = nullptr;
    int m_captureWidth = 0;
    int m_captureHeight = 0;

    cocos2d::CCNode* m_previewContainer = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCMenu* m_previewMenu = nullptr;
    cocos2d::CCNode* m_previewCard = nullptr;
    cocos2d::CCClippingNode* m_flyClipNode = nullptr;
    cocos2d::CCNodeRGBA* m_flyCardBg = nullptr;
    bool m_isClosing = false;

    void onCapture(cocos2d::CCObject* sender);
    void onClose(cocos2d::CCObject* sender);
    void onDownload(cocos2d::CCObject* sender);
    void onOpenFolder(cocos2d::CCObject* sender);
    void onOpenShortcuts(cocos2d::CCObject* sender);

    void triggerCaptureProcess();
    void playFlyToBottomRightAnimation();
    void revealPreviewControls();
    void finishClose();
};
