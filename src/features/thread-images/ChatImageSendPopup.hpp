#pragma once

#include "ChatImageService.hpp"
#include "../../ui/PaimonPopup.hpp"
#include "../../utils/ClipboardImage.hpp"

#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <optional>
#include <string>

namespace paimon::chatimg {

class ChatImageSendPopup : public PaimonPopup {
public:
    using SendCallback = geode::CopyableFunction<void(std::string const& token, std::string const& caption)>;

    static ChatImageSendPopup* create(
        int threadID,
        std::string initialCaption,
        SendCallback onUploaded
    );

    static ChatImageSendPopup* createWithClipboard(
        int threadID,
        std::string initialCaption,
        paimon::ClipboardImageData clipData,
        SendCallback onUploaded
    );

    ~ChatImageSendPopup() override;

protected:
    bool initWithData(
        int threadID,
        std::string initialCaption,
        std::optional<paimon::ClipboardImageData> initialClip,
        SendCallback onUploaded
    );

    void onClose(cocos2d::CCObject* sender) override;
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;

    void onPickImage(cocos2d::CCObject* sender);
    void onPasteImage(cocos2d::CCObject* sender);
    void onSend(cocos2d::CCObject* sender);

    void startPrepareClipboard(paimon::ClipboardImageData data);
    void onPrepared(PreparedUpload result);
    void renderPreview();
    void updateMetaText(std::string const& text);
    void setBusy(bool busy);
    void updateSendButtonState();

    int m_threadID = 0;
    std::string m_initialCaption;
    SendCallback m_onUploaded;
    PreparedUpload m_prepared;
    bool m_busy = false;
    bool m_closed = false;

    cocos2d::CCNode* m_previewFrame = nullptr;
    cocos2d::CCNode* m_previewContent = nullptr;
    cocos2d::CCLabelBMFont* m_metaLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    LoadingCircle* m_spinner = nullptr;
    geode::TextInput* m_captionInput = nullptr;
    CCMenuItemSpriteExtra* m_sendBtn = nullptr;
    CCMenuItemSpriteExtra* m_pickBtn = nullptr;
    CCMenuItemSpriteExtra* m_pasteBtn = nullptr;
};

} // namespace paimon::chatimg
