#pragma once

#include "ChatImageService.hpp"
#include "../../ui/PaimonPopup.hpp"

#include <string>

namespace paimon::chatimg {

class ChatImageViewerPopup : public PaimonPopup {
public:
    using DeletedCallback = geode::CopyableFunction<void()>;

    static ChatImageViewerPopup* create(
        ChatImageRef ref,
        int threadID,
        int senderAccountID,
        DeletedCallback onDeleted = nullptr
    );

protected:
    bool initViewer(
        ChatImageRef ref,
        int threadID,
        int senderAccountID,
        DeletedCallback onDeleted
    );

    void onClose(cocos2d::CCObject* sender) override;
    void keyBackClicked() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void onZoomIn(cocos2d::CCObject* sender);
    void onZoomOut(cocos2d::CCObject* sender);
    void onResetZoom(cocos2d::CCObject* sender);
    void onSave(cocos2d::CCObject* sender);
    void onCopy(cocos2d::CCObject* sender);
    void onDelete(cocos2d::CCObject* sender);

    void applyImage(LoadedChatImage img);
    void applyTransform();

    ChatImageRef m_ref;
    int m_threadID = 0;
    int m_senderAccountID = 0;
    DeletedCallback m_onDeleted;
    LoadedChatImage m_image;
    bool m_closed = false;
    bool m_busy = false;
    bool m_panning = false;
    cocos2d::CCPoint m_lastTouch{};

    cocos2d::CCClippingNode* m_clipNode = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    LoadingCircle* m_spinner = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    float m_clipW = 400.f;
    float m_clipH = 190.f;
    float m_lastStencilW = 0.f;
    float m_lastStencilH = 0.f;
    float m_imageW = 1.f;
    float m_imageH = 1.f;
    float m_baseScale = 1.f;
    float m_zoom = 1.f;
    cocos2d::CCPoint m_panOffset{};
};

} // namespace paimon::chatimg
