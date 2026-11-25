#include "ChatImageViewerPopup.hpp"

#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/AnimatedGIFSprite.hpp"
#include "../../utils/ClipboardImage.hpp"
#include "../../utils/FileDialog.hpp"
#include "../../utils/HttpClient.hpp"
#include "../../utils/Localization.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../utils/SpriteHelper.hpp"
#include "../community-base/services/CommunityClient.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::chatimg {

ChatImageViewerPopup* ChatImageViewerPopup::create(
    ChatImageRef ref,
    int threadID,
    int senderAccountID,
    DeletedCallback onDeleted
) {
    auto* ret = new ChatImageViewerPopup();
    if (ret && ret->initViewer(std::move(ref), threadID, senderAccountID, std::move(onDeleted))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ChatImageViewerPopup::initViewer(
    ChatImageRef ref,
    int threadID,
    int senderAccountID,
    DeletedCallback onDeleted
) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float const width = std::min(440.f, winSize.width - 40.f);
    float const height = std::min(284.f, winSize.height - 30.f);
    if (!PaimonPopup::init(width, height)) return false;

    m_ref = std::move(ref);
    m_threadID = threadID;
    m_senderAccountID = senderAccountID;
    m_onDeleted = std::move(onDeleted);

    auto& loc = Localization::get();
    setTitle(loc.getString("chatimg.viewer.title"));

    auto const size = m_mainLayer->getContentSize();
    float const cx = size.width * 0.5f;
    m_clipW = size.width - 28.f;
    m_clipH = size.height - 88.f;
    float const viewY = size.height * 0.5f + 4.f;

    auto* bg = paimon::SpriteHelper::createDarkPanel(m_clipW, m_clipH, 130, 6.f);
    if (bg) {
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({cx, viewY});
        m_mainLayer->addChild(bg, 1);
    }

    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(m_clipW, m_clipH, 8.f);
    if (stencil) {
        m_clipNode = CCClippingNode::create(stencil);
        m_clipNode->setContentSize({m_clipW, m_clipH});
        m_clipNode->setAnchorPoint({0.5f, 0.5f});
        m_clipNode->setPosition({cx, viewY});
        m_mainLayer->addChild(m_clipNode, 2);
        m_lastStencilW = m_clipW;
        m_lastStencilH = m_clipH;

        m_contentNode = CCNode::create();
        m_contentNode->setPosition({m_clipW * 0.5f, m_clipH * 0.5f});
        m_clipNode->addChild(m_contentNode, 1);
    }

    m_spinner = LoadingCircle::create();
    if (m_spinner) {
        m_spinner->setScale(0.65f);
        m_spinner->setPosition({cx, viewY});
        m_spinner->show();
        m_mainLayer->addChild(m_spinner, 5);
    }

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.6f);
        m_statusLabel->setColor({235, 160, 160});
        m_statusLabel->setPosition({cx, viewY});
        m_statusLabel->setVisible(false);
        m_mainLayer->addChild(m_statusLabel, 5);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 6);

    float const topBarY = size.height - 20.f;
    if (auto* zoomOutSpr = paimon::SpriteHelper::createWithFallback("GJ_zoomOutBtn_001.png", "paim_ui_zoomOut.png"_spr)) {
        zoomOutSpr->setScale(0.58f);
        auto* btn = CCMenuItemSpriteExtra::create(zoomOutSpr, this, menu_selector(ChatImageViewerPopup::onZoomOut));
        btn->setPosition({size.width - 84.f, topBarY});
        menu->addChild(btn);
    }
    if (auto* fitSpr = ButtonSprite::create("1:1", "goldFont.fnt", "GJ_button_04.png", 0.65f)) {
        fitSpr->setScale(0.45f);
        auto* btn = CCMenuItemSpriteExtra::create(fitSpr, this, menu_selector(ChatImageViewerPopup::onResetZoom));
        btn->setPosition({size.width - 54.f, topBarY});
        menu->addChild(btn);
    }
    if (auto* zoomInSpr = paimon::SpriteHelper::createWithFallback("GJ_zoomInBtn_001.png", "paim_ui_zoomIn.png"_spr)) {
        zoomInSpr->setScale(0.58f);
        auto* btn = CCMenuItemSpriteExtra::create(zoomInSpr, this, menu_selector(ChatImageViewerPopup::onZoomIn));
        btn->setPosition({size.width - 24.f, topBarY});
        menu->addChild(btn);
    }

    // Bottom action buttons
    auto idOpt = paimon::community::currentIdentity();
    bool const isOwner = idOpt && m_senderAccountID > 0 && idOpt->accountID == m_senderAccountID;
    bool const isMod = !HttpClient::get().getModCode().empty();
    bool const canDelete = isOwner || isMod;

    std::vector<CCMenuItemSpriteExtra*> actions;
    if (auto* saveSpr = ButtonSprite::create(loc.getString("chatimg.viewer.save").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.72f)) {
        saveSpr->setScale(0.68f);
        auto* btn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(ChatImageViewerPopup::onSave));
        actions.push_back(btn);
        menu->addChild(btn);
    }
#ifdef GEODE_IS_WINDOWS
    if (auto* copySpr = ButtonSprite::create(loc.getString("chatimg.viewer.copy").c_str(), "goldFont.fnt", "GJ_button_04.png", 0.72f)) {
        copySpr->setScale(0.68f);
        auto* btn = CCMenuItemSpriteExtra::create(copySpr, this, menu_selector(ChatImageViewerPopup::onCopy));
        actions.push_back(btn);
        menu->addChild(btn);
    }
#endif
    if (canDelete) {
        if (auto* delSpr = ButtonSprite::create(loc.getString("chatimg.viewer.delete").c_str(), "goldFont.fnt", "GJ_button_06.png", 0.72f)) {
            delSpr->setScale(0.68f);
            auto* btn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(ChatImageViewerPopup::onDelete));
            actions.push_back(btn);
            menu->addChild(btn);
        }
    }

    float const spacing = 96.f;
    float const startX = cx - spacing * static_cast<float>(actions.size() - 1) * 0.5f;
    for (size_t i = 0; i < actions.size(); ++i) {
        actions[i]->setPosition({startX + spacing * static_cast<float>(i), 26.f});
    }

    WeakRef<ChatImageViewerPopup> weak = this;
    ChatImageService::get().loadImage(m_ref, m_threadID, [weak](LoadedChatImage img) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto self = weak.lock();
        if (!self || self->m_closed) return;
        self->applyImage(std::move(img));
    });

    return true;
}

void ChatImageViewerPopup::applyImage(LoadedChatImage img) {
    if (m_spinner) m_spinner->setVisible(false);
    m_image = std::move(img);

    if (!m_image.ok || !m_contentNode) {
        if (m_statusLabel) {
            m_statusLabel->setString(Localization::get().getString("chatimg.err.load_failed").c_str());
            m_statusLabel->setVisible(true);
        }
        return;
    }

    m_contentNode->removeAllChildren();

    auto fitSprite = [this](CCNode* spr) {
        if (!spr) return;
        auto sz = spr->getContentSize();
        m_imageW = std::max(1.f, sz.width);
        m_imageH = std::max(1.f, sz.height);
        m_baseScale = std::min((m_clipW - 8.f) / m_imageW, (m_clipH - 8.f) / m_imageH);
        m_zoom = 1.f;
        m_panOffset = ccp(0.f, 0.f);
        m_contentNode->addChild(spr);
        applyTransform();
    };

    if (m_image.isGif) {
        WeakRef<ChatImageViewerPopup> weak = this;
        AnimatedGIFSprite::createAsync(m_image.rawBytes, "chatimg:" + std::to_string(m_threadID) + ":" + m_ref.id, [weak, fitSprite](AnimatedGIFSprite* gif) mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto self = weak.lock();
            if (!self || self->m_closed || !self->m_contentNode) return;
            if (!gif) {
                if (self->m_statusLabel) {
                    self->m_statusLabel->setString(Localization::get().getString("chatimg.err.load_failed").c_str());
                    self->m_statusLabel->setVisible(true);
                }
                return;
            }
            self->m_contentNode->removeAllChildren();
            auto sz = gif->getContentSize();
            self->m_imageW = std::max(1.f, sz.width);
            self->m_imageH = std::max(1.f, sz.height);
            self->m_baseScale = std::min((self->m_clipW - 8.f) / self->m_imageW, (self->m_clipH - 8.f) / self->m_imageH);
            self->m_zoom = 1.f;
            self->m_panOffset = ccp(0.f, 0.f);
            self->m_contentNode->addChild(gif);
            self->applyTransform();
        });
        return;
    }

    if (m_image.texture) {
        auto* spr = CCSprite::createWithTexture(m_image.texture);
        fitSprite(spr);
    }
}

void ChatImageViewerPopup::applyTransform() {
    if (!m_contentNode) return;
    float const totalScale = m_baseScale * m_zoom;
    m_contentNode->setScale(totalScale);

    float const maxPanX = std::max(0.f, (m_imageW * totalScale - m_clipW) * 0.5f);
    float const maxPanY = std::max(0.f, (m_imageH * totalScale - m_clipH) * 0.5f);
    m_panOffset.x = std::clamp(m_panOffset.x, -maxPanX, maxPanX);
    m_panOffset.y = std::clamp(m_panOffset.y, -maxPanY, maxPanY);
    m_contentNode->setPosition({m_clipW * 0.5f + m_panOffset.x, m_clipH * 0.5f + m_panOffset.y});

    float const stencilW = std::clamp(m_imageW * totalScale, 1.f, m_clipW);
    float const stencilH = std::clamp(m_imageH * totalScale, 1.f, m_clipH);
    if (m_clipNode && (std::fabs(stencilW - m_lastStencilW) > 0.25f || std::fabs(stencilH - m_lastStencilH) > 0.25f)) {
        float const r = computeImageCornerRadius(stencilW, stencilH, 8.f);
        if (auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(stencilW, stencilH, r)) {
            stencil->setAnchorPoint({0.f, 0.f});
            stencil->setPosition({(m_clipW - stencilW) * 0.5f, (m_clipH - stencilH) * 0.5f});
            m_clipNode->setStencil(stencil);
            m_lastStencilW = stencilW;
            m_lastStencilH = stencilH;
        }
    }
}

void ChatImageViewerPopup::onZoomIn(CCObject*) {
    m_zoom = std::min(4.f, m_zoom * 1.35f);
    applyTransform();
}

void ChatImageViewerPopup::onZoomOut(CCObject*) {
    m_zoom = std::max(1.f, m_zoom / 1.35f);
    applyTransform();
}

void ChatImageViewerPopup::onResetZoom(CCObject*) {
    m_zoom = 1.f;
    m_panOffset = ccp(0.f, 0.f);
    applyTransform();
}

bool ChatImageViewerPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (m_clipNode && m_zoom > 1.01f && touch) {
        auto local = m_clipNode->convertTouchToNodeSpace(touch);
        CCRect bounds(0.f, 0.f, m_clipW, m_clipH);
        if (bounds.containsPoint(local)) {
            m_panning = true;
            m_lastTouch = touch->getLocation();
            return true;
        }
    }
    return PaimonPopup::ccTouchBegan(touch, event);
}

void ChatImageViewerPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (m_panning && touch) {
        auto pos = touch->getLocation();
        m_panOffset.x += pos.x - m_lastTouch.x;
        m_panOffset.y += pos.y - m_lastTouch.y;
        m_lastTouch = pos;
        applyTransform();
        return;
    }
    PaimonPopup::ccTouchMoved(touch, event);
}

void ChatImageViewerPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_panning) {
        m_panning = false;
        return;
    }
    PaimonPopup::ccTouchEnded(touch, event);
}

void ChatImageViewerPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (m_panning) {
        m_panning = false;
        return;
    }
    PaimonPopup::ccTouchCancelled(touch, event);
}

void ChatImageViewerPopup::onSave(CCObject*) {
    if (!m_image.ok || m_image.rawBytes.empty()) return;
    WeakRef<ChatImageViewerPopup> weak = this;
    std::string defaultName = "paimbnails_" + m_ref.id + "." + m_ref.ext;
    pt::saveImage(defaultName, [weak, bytes = m_image.rawBytes](Result<std::optional<std::filesystem::path>> res) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (res.isErr()) {
            PaimonNotify::show(res.unwrapErr(), NotificationIcon::Error);
            return;
        }
        auto opt = std::move(res).unwrap();
        if (!opt || opt->empty()) return;
        ChatImageService::get().saveRawBytesToPath(std::move(bytes), *opt, [weak](bool ok, std::string const& err) {
            if (paimon::isRuntimeShuttingDown()) return;
            if (ok) {
                PaimonNotify::show(Localization::get().getString("chatimg.saved"), NotificationIcon::Success);
            } else {
                PaimonNotify::show(err.empty() ? Localization::get().getString("chatimg.err.save") : err, NotificationIcon::Error);
            }
        });
    });
}

void ChatImageViewerPopup::onCopy(CCObject*) {
#ifdef GEODE_IS_WINDOWS
    if (!m_image.ok || m_image.rawBytes.empty()) return;
    WeakRef<ChatImageViewerPopup> weak = this;
    ChatImageService::get().decodeRawToRgbaAsync(m_image.rawBytes, [weak](
        bool ok, std::vector<uint8_t> rgba, int w, int h
    ) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!ok || rgba.empty() || w < 1 || h < 1) {
            PaimonNotify::show(Localization::get().getString("chatimg.err.copy"), NotificationIcon::Error);
            return;
        }
        if (paimon::copyRGBAToClipboard(rgba.data(), w, h)) {
            PaimonNotify::show(Localization::get().getString("chatimg.copied"), NotificationIcon::Success);
        } else {
            PaimonNotify::show(Localization::get().getString("chatimg.err.copy"), NotificationIcon::Error);
        }
    });
#endif
}

void ChatImageViewerPopup::onDelete(CCObject*) {
    if (m_busy || m_closed) return;
    auto& loc = Localization::get();
    WeakRef<ChatImageViewerPopup> weak = this;
    auto* popup = createQuickPopup(
        loc.getString("chatimg.viewer.title").c_str(),
        loc.getString("chatimg.delete.confirm").c_str(),
        "Cancel",
        loc.getString("chatimg.viewer.delete").c_str(),
        [weak](auto*, bool confirmed) {
            if (!confirmed || paimon::isRuntimeShuttingDown()) return;
            auto self = weak.lock();
            if (!self || self->m_closed || self->m_busy) return;
            self->m_busy = true;
            ChatImageService::get().deleteImage(self->m_ref, [weak](bool ok, std::string const& err) {
                if (paimon::isRuntimeShuttingDown()) return;
                auto self2 = weak.lock();
                if (!self2 || self2->m_closed) return;
                self2->m_busy = false;
                if (!ok) {
                    PaimonNotify::show(
                        err.empty() ? Localization::get().getString("chatimg.err.delete") : err,
                        NotificationIcon::Error
                    );
                    return;
                }
                PaimonNotify::show(Localization::get().getString("chatimg.deleted"), NotificationIcon::Success);
                auto cb = std::move(self2->m_onDeleted);
                self2->onClose(nullptr);
                if (cb) cb();
            });
        }
    );
    paimon::community::showOnTop(popup);
}

void ChatImageViewerPopup::onClose(CCObject* sender) {
    m_closed = true;
    PaimonPopup::onClose(sender);
}

void ChatImageViewerPopup::keyBackClicked() {
    m_closed = true;
    PaimonPopup::keyBackClicked();
}

} // namespace paimon::chatimg
