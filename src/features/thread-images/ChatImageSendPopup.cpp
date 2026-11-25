#include "ChatImageSendPopup.hpp"

#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/AnimatedGIFSprite.hpp"
#include "../../utils/FileDialog.hpp"
#include "../../utils/GeodeTextInputSafe.hpp"
#include "../../utils/ImageLoadHelper.hpp"
#include "../../utils/Localization.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../utils/SpriteHelper.hpp"
#include "../community-base/services/CommunityClient.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::chatimg {

namespace {
constexpr float kPopupW = 360.f;
constexpr float kPopupH = 264.f;
constexpr float kPreviewW = 256.f;
constexpr float kPreviewH = 112.f;

void mountRoundedPreview(CCNode* contentNode, CCNode* imgNode, float fitW, float fitH) {
    if (!contentNode || !imgNode) return;
    contentNode->removeAllChildren();

    auto const sz = imgNode->getContentSize();
    float const s = std::min(2.f, std::min(fitW / std::max(1.f, sz.width), fitH / std::max(1.f, sz.height)));
    imgNode->setScale(s);

    float const dispW = std::max(1.f, sz.width * s);
    float const dispH = std::max(1.f, sz.height * s);
    float const r = computeImageCornerRadius(dispW, dispH, 8.f);

    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(dispW, dispH, r);
    auto* clip = stencil ? CCClippingNode::create(stencil) : nullptr;
    if (clip) {
        clip->setContentSize({dispW, dispH});
        clip->setAnchorPoint({0.5f, 0.5f});
        clip->setPosition({0.f, 0.f});
        imgNode->setAnchorPoint({0.5f, 0.5f});
        imgNode->setPosition({dispW * 0.5f, dispH * 0.5f});
        clip->addChild(imgNode, 0);
        contentNode->addChild(clip, 1);
    } else {
        imgNode->setAnchorPoint({0.5f, 0.5f});
        imgNode->setPosition({0.f, 0.f});
        contentNode->addChild(imgNode, 1);
    }

    if (auto* border = paimon::SpriteHelper::createRoundedRectOutline(
            dispW, dispH, r, {1.f, 1.f, 1.f, 0.14f}, 1.f)) {
        border->setAnchorPoint({0.f, 0.f});
        border->setPosition({-dispW * 0.5f, -dispH * 0.5f});
        contentNode->addChild(border, 2);
    }
}
} // namespace

ChatImageSendPopup* ChatImageSendPopup::create(
    int threadID,
    std::string initialCaption,
    SendCallback onUploaded
) {
    auto* ret = new ChatImageSendPopup();
    if (ret && ret->initWithData(threadID, std::move(initialCaption), std::nullopt, std::move(onUploaded))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ChatImageSendPopup* ChatImageSendPopup::createWithClipboard(
    int threadID,
    std::string initialCaption,
    paimon::ClipboardImageData clipData,
    SendCallback onUploaded
) {
    auto* ret = new ChatImageSendPopup();
    if (ret && ret->initWithData(threadID, std::move(initialCaption), std::move(clipData), std::move(onUploaded))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ChatImageSendPopup::~ChatImageSendPopup() {
    paimon::ui::detachGeodeTextInput(m_captionInput);
}

bool ChatImageSendPopup::initWithData(
    int threadID,
    std::string initialCaption,
    std::optional<paimon::ClipboardImageData> initialClip,
    SendCallback onUploaded
) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    m_threadID = threadID;
    m_initialCaption = std::move(initialCaption);
    m_onUploaded = std::move(onUploaded);

    auto& loc = Localization::get();
    setTitle(loc.getString("chatimg.send.title"));

    auto const size = m_mainLayer->getContentSize();
    float const cx = size.width * 0.5f;

    auto* bg = paimon::SpriteHelper::createDarkPanel(kPreviewW, kPreviewH, 120, 6.f);
    if (bg) {
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({cx, 166.f});
        m_mainLayer->addChild(bg, 1);
    }

    auto* frame = CCNode::create();
    frame->setContentSize({kPreviewW, kPreviewH});
    frame->setAnchorPoint({0.5f, 0.5f});
    frame->setPosition({cx, 166.f});
    m_mainLayer->addChild(frame, 2);
    m_previewFrame = frame;

    m_previewContent = CCNode::create();
    m_previewContent->setPosition({kPreviewW * 0.5f, kPreviewH * 0.5f});
    m_previewFrame->addChild(m_previewContent, 1);

    m_hintLabel = CCLabelBMFont::create(loc.getString("chatimg.send.hint").c_str(), "chatFont.fnt");
    if (m_hintLabel) {
        m_hintLabel->setScale(0.62f);
        m_hintLabel->setColor({195, 195, 205});
        m_hintLabel->setPosition({kPreviewW * 0.5f, kPreviewH * 0.5f});
        m_previewFrame->addChild(m_hintLabel, 2);
    }

    m_spinner = LoadingCircle::create();
    if (m_spinner) {
        m_spinner->setScale(0.55f);
        m_spinner->setPosition({kPreviewW * 0.5f, kPreviewH * 0.5f});
        m_spinner->setVisible(false);
        m_spinner->show();
        m_previewFrame->addChild(m_spinner, 5);
    }

    m_metaLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_metaLabel) {
        m_metaLabel->setScale(0.52f);
        m_metaLabel->setColor({180, 225, 255});
        m_metaLabel->setPosition({cx, 100.f});
        m_mainLayer->addChild(m_metaLabel, 2);
    }

    m_captionInput = TextInput::create(kPreviewW, loc.getString("chatimg.send.caption").c_str(), "chatFont.fnt");
    if (m_captionInput) {
        m_captionInput->setCommonFilter(CommonFilter::TextEmojis);
        m_captionInput->setMaxCharCount(390);
        m_captionInput->setPosition({cx, 72.f});
        if (!m_initialCaption.empty()) {
            m_captionInput->setString(m_initialCaption);
        }
        m_mainLayer->addChild(m_captionInput, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 4);

#ifdef GEODE_IS_WINDOWS
    auto* pickSpr = ButtonSprite::create(loc.getString("chatimg.send.pick").c_str(), "goldFont.fnt", "GJ_button_04.png", 0.72f);
    if (pickSpr) pickSpr->setScale(0.68f);
    m_pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ChatImageSendPopup::onPickImage));
    m_pickBtn->setPosition({cx - 102.f, 30.f});
    menu->addChild(m_pickBtn);

    auto* pasteSpr = ButtonSprite::create(loc.getString("chatimg.send.paste").c_str(), "goldFont.fnt", "GJ_button_04.png", 0.72f);
    if (pasteSpr) pasteSpr->setScale(0.68f);
    m_pasteBtn = CCMenuItemSpriteExtra::create(pasteSpr, this, menu_selector(ChatImageSendPopup::onPasteImage));
    m_pasteBtn->setPosition({cx, 30.f});
    menu->addChild(m_pasteBtn);

    auto* sendSpr = ButtonSprite::create(loc.getString("chatimg.send.submit").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.72f);
    if (sendSpr) sendSpr->setScale(0.68f);
    m_sendBtn = CCMenuItemSpriteExtra::create(sendSpr, this, menu_selector(ChatImageSendPopup::onSend));
    m_sendBtn->setPosition({cx + 102.f, 30.f});
    menu->addChild(m_sendBtn);
#else
    auto* pickSpr = ButtonSprite::create(loc.getString("chatimg.send.pick").c_str(), "goldFont.fnt", "GJ_button_04.png", 0.72f);
    if (pickSpr) pickSpr->setScale(0.68f);
    m_pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ChatImageSendPopup::onPickImage));
    m_pickBtn->setPosition({cx - 72.f, 30.f});
    menu->addChild(m_pickBtn);

    auto* sendSpr = ButtonSprite::create(loc.getString("chatimg.send.submit").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.72f);
    if (sendSpr) sendSpr->setScale(0.68f);
    m_sendBtn = CCMenuItemSpriteExtra::create(sendSpr, this, menu_selector(ChatImageSendPopup::onSend));
    m_sendBtn->setPosition({cx + 72.f, 30.f});
    menu->addChild(m_sendBtn);
#endif

    updateSendButtonState();

    if (initialClip) {
        startPrepareClipboard(std::move(*initialClip));
    }

    return true;
}

void ChatImageSendPopup::onClose(CCObject* sender) {
    m_closed = true;
    paimon::ui::detachGeodeTextInput(m_captionInput);
    PaimonPopup::onClose(sender);
}

void ChatImageSendPopup::keyBackClicked() {
    m_closed = true;
    paimon::ui::detachGeodeTextInput(m_captionInput);
    PaimonPopup::keyBackClicked();
}

void ChatImageSendPopup::keyDown(enumKeyCodes key, double timestamp) {
    if (key == KEY_V && CCKeyboardDispatcher::get() && CCKeyboardDispatcher::get()->getControlKeyPressed()) {
        onPasteImage(nullptr);
        return;
    }
    PaimonPopup::keyDown(key, timestamp);
}

void ChatImageSendPopup::onPickImage(CCObject*) {
    if (m_busy || m_closed) return;
    WeakRef<ChatImageSendPopup> weak = this;
    pt::pickImage([weak](Result<std::optional<std::filesystem::path>> res) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = weak.lock();
        if (!self || self->m_closed || self->m_busy) return;
        if (res.isErr()) {
            PaimonNotify::show(res.unwrapErr(), NotificationIcon::Error);
            return;
        }
        auto opt = std::move(res).unwrap();
        if (!opt || opt->empty()) return;

        self->setBusy(true);
        self->updateMetaText(Localization::get().getString("chatimg.send.preparing"));
        ChatImageService::get().prepareFromFile(*opt, [weak](PreparedUpload result) {
            if (paimon::isRuntimeShuttingDown()) return;
            auto self2 = weak.lock();
            if (!self2 || self2->m_closed) return;
            self2->onPrepared(std::move(result));
        });
    });
}

void ChatImageSendPopup::onPasteImage(CCObject*) {
    if (m_busy || m_closed) return;
    auto clip = paimon::readImageFromClipboard();
    if (!clip) {
        PaimonNotify::show(Localization::get().getString("chatimg.err.clipboard_empty"), NotificationIcon::Warning);
        return;
    }
    startPrepareClipboard(std::move(*clip));
}

void ChatImageSendPopup::startPrepareClipboard(paimon::ClipboardImageData data) {
    setBusy(true);
    updateMetaText(Localization::get().getString("chatimg.send.preparing"));
    WeakRef<ChatImageSendPopup> weak = this;
    ChatImageService::get().prepareFromClipboard(std::move(data), [weak](PreparedUpload result) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = weak.lock();
        if (!self || self->m_closed) return;
        self->onPrepared(std::move(result));
    });
}

void ChatImageSendPopup::onPrepared(PreparedUpload result) {
    setBusy(false);
    if (!result.ok()) {
        updateMetaText("");
        PaimonNotify::show(
            result.error.empty() ? Localization::get().getString("chatimg.err.prepare") : result.error,
            NotificationIcon::Error
        );
        return;
    }
    m_prepared = std::move(result);
    renderPreview();

    std::string extUpper = m_prepared.ext;
    for (char& ch : extUpper) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    double kb = static_cast<double>(m_prepared.bytes.size()) / 1024.0;
    updateMetaText(fmt::format("{}x{}  •  {:.0f} KB  •  {}", m_prepared.width, m_prepared.height, std::max(1.0, kb), extUpper));
    updateSendButtonState();
}

void ChatImageSendPopup::renderPreview() {
    if (!m_previewContent || !m_prepared.ok()) return;
    m_previewContent->removeAllChildren();
    if (m_hintLabel) m_hintLabel->setVisible(false);

    float const fitW = kPreviewW - 10.f;
    float const fitH = kPreviewH - 10.f;

    if (m_prepared.isGif) {
        WeakRef<ChatImageSendPopup> weak = this;
        AnimatedGIFSprite::createAsync(m_prepared.bytes, "chatimg:send-preview", [weak, fitW, fitH](AnimatedGIFSprite* gif) {
            if (paimon::isRuntimeShuttingDown()) return;
            auto self = weak.lock();
            if (!self || self->m_closed || !self->m_previewContent) return;
            if (!gif) return;
            mountRoundedPreview(self->m_previewContent, gif, fitW, fitH);
        });
        return;
    }

    WeakRef<ChatImageSendPopup> weak = this;
    ChatImageService::get().decodeRawToRgbaAsync(m_prepared.bytes, [weak, fitW, fitH](
        bool ok, std::vector<uint8_t> rgba, int w, int h
    ) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = weak.lock();
        if (!self || self->m_closed || !self->m_previewContent) return;
        if (!ok || w < 1 || h < 1) return;
        auto loaded = ImageLoadHelper::createFromRGBA(rgba.data(), w, h, true);
        if (!loaded.success || !loaded.texture) return;
        auto texRef = Ref<CCTexture2D>::adopt(loaded.texture);
        auto* spr = CCSprite::createWithTexture(texRef);
        if (!spr) return;
        mountRoundedPreview(self->m_previewContent, spr, fitW, fitH);
    });
}

void ChatImageSendPopup::onSend(CCObject*) {
    if (m_busy || m_closed || !m_prepared.ok()) return;

    if (!paimon::community::hasAccountProof()) {
        PaimonNotify::show(Localization::get().getString("chatimg.verify_required"), NotificationIcon::Info);
        paimon::community::promptVerification();
        return;
    }

    std::string caption = m_captionInput ? m_captionInput->getString() : std::string{};
    setBusy(true);
    updateMetaText(Localization::get().getString("chatimg.send.uploading"));

    WeakRef<ChatImageSendPopup> weak = this;
    ChatImageService::get().uploadPrepared(m_prepared, m_threadID, [weak, caption = std::move(caption)](UploadResult res) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = weak.lock();
        if (!self || self->m_closed) return;
        self->setBusy(false);

        if (!res.ok) {
            if (res.needsVerification) {
                PaimonNotify::show(Localization::get().getString("chatimg.verify_required"), NotificationIcon::Info);
                paimon::community::promptVerification();
            } else {
                PaimonNotify::show(
                    res.error.empty() ? Localization::get().getString("chatimg.err.upload") : res.error,
                    NotificationIcon::Error
                );
            }
            self->updateMetaText("");
            return;
        }

        auto cb = std::move(self->m_onUploaded);
        auto token = std::move(res.token);
        self->onClose(nullptr);
        if (cb) cb(token, caption);
    });
}

void ChatImageSendPopup::updateMetaText(std::string const& text) {
    if (m_metaLabel) m_metaLabel->setString(text.c_str());
}

void ChatImageSendPopup::setBusy(bool busy) {
    m_busy = busy;
    if (m_spinner) m_spinner->setVisible(busy);
    if (m_pickBtn) m_pickBtn->setEnabled(!busy);
    if (m_pasteBtn) m_pasteBtn->setEnabled(!busy);
    updateSendButtonState();
}

void ChatImageSendPopup::updateSendButtonState() {
    if (!m_sendBtn) return;
    bool enabled = !m_busy && m_prepared.ok();
    m_sendBtn->setEnabled(enabled);
    if (auto* spr = m_sendBtn->getNormalImage()) {
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(spr)) {
            rgba->setOpacity(enabled ? 255 : 110);
        }
    }
}

} // namespace paimon::chatimg
