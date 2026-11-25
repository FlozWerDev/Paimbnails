// inline images in group/dm threads (ThreadViewPopup, gd 2.2091).
// messages carry a compact "[pimg:<id>.<ext>:<w>x<h>]" token; the hook hides it, reserves
// the exact row height up front and draws the picture below the text.
#include <Geode/Geode.hpp>
#include <Geode/modify/ThreadViewPopup.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/binding/GJMessageThread.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "ChatImageService.hpp"
#include "ChatImageSendPopup.hpp"
#include "ChatImageViewerPopup.hpp"
#include "ChatImageToken.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/AnimatedGIFSprite.hpp"
#include "../../utils/ClipboardImage.hpp"
#include "../../utils/Localization.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../utils/SpriteHelper.hpp"
#include "../community-base/services/CommunityClient.hpp"

using namespace geode::prelude;
using namespace paimon::chatimg;

namespace {

std::atomic<bool> s_enabled{true};

bool threadImagesEnabled() {
    return s_enabled.load(std::memory_order_relaxed);
}

#ifdef GEODE_IS_WINDOWS
// windows 2.2091 layout, verified against GeometryDash.exe (see virtualRowCount: size >> 6).
struct NativeThreadRow {
    int32_t type;
    int32_t messageIndex;
    float y;
    float height;
    uint8_t hasHeader;
    uint8_t hasGap;
    uint8_t pad12[6];
    std::string text;
    cocos2d::ccColor3B color;
    uint8_t pad3b[5];
};
static_assert(sizeof(NativeThreadRow) == 0x40, "NativeThreadRow layout changed");
static_assert(offsetof(NativeThreadRow, height) == 0x0c, "NativeThreadRow layout changed");
static_assert(offsetof(NativeThreadRow, text) == 0x18, "NativeThreadRow layout changed");

constexpr uintptr_t kMsgSenderOffset = 0x40;
constexpr uintptr_t kMsgBodyOffset = 0x48;
constexpr uintptr_t kMsgDeletedOffset = 0x70;
constexpr uintptr_t kComposeWidthOffset = 0x428;

std::string const* messageBody(GJThreadMessage* msg) {
    return reinterpret_cast<std::string const*>(reinterpret_cast<uintptr_t>(msg) + kMsgBodyOffset);
}
int messageSender(GJThreadMessage* msg) {
    return *reinterpret_cast<int32_t const*>(reinterpret_cast<uintptr_t>(msg) + kMsgSenderOffset);
}
bool messageDeleted(GJThreadMessage* msg) {
    return *reinterpret_cast<uint8_t const*>(reinterpret_cast<uintptr_t>(msg) + kMsgDeletedOffset) != 0;
}
#endif

constexpr float kCardGap = 6.f;
constexpr float kTextLeft = 32.f;
constexpr size_t kMaxMessageChars = 500;
constexpr float kAttachSlotWidth = 26.f;
constexpr float kAttachGapFromBox = 16.f;
constexpr float kInlineCornerRadius = 8.f;

struct RowImages {
    ParsedChatMessage parsed;
    int senderID = 0;
};

void mountRoundedInlineImage(CCNode* holderNode, CCNode* imgNode, float maxW, float maxH, bool showGifBadge) {
    if (!holderNode || !imgNode) return;
    holderNode->removeAllChildren();

    auto const sz = imgNode->getContentSize();
    float const scale = std::min(maxW / std::max(1.f, sz.width), maxH / std::max(1.f, sz.height));
    imgNode->setScale(scale);

    float const dispW = std::max(1.f, sz.width * scale);
    float const dispH = std::max(1.f, sz.height * scale);
    float const r = computeImageCornerRadius(dispW, dispH, kInlineCornerRadius);

    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(dispW, dispH, r);
    auto* clip = stencil ? CCClippingNode::create(stencil) : nullptr;
    if (clip) {
        clip->setContentSize({dispW, dispH});
        clip->setAnchorPoint({0.5f, 0.5f});
        clip->setPosition({0.f, 0.f});
        imgNode->setAnchorPoint({0.5f, 0.5f});
        imgNode->setPosition({dispW * 0.5f, dispH * 0.5f});
        clip->addChild(imgNode, 0);
        holderNode->addChild(clip, 1);
    } else {
        imgNode->setAnchorPoint({0.5f, 0.5f});
        imgNode->setPosition({0.f, 0.f});
        holderNode->addChild(imgNode, 1);
    }

    if (auto* border = paimon::SpriteHelper::createRoundedRectOutline(
            dispW, dispH, r, {1.f, 1.f, 1.f, 0.14f}, 1.f)) {
        border->setAnchorPoint({0.f, 0.f});
        border->setPosition({-dispW * 0.5f, -dispH * 0.5f});
        holderNode->addChild(border, 2);
    }

    if (showGifBadge) {
        if (auto* badge = CCLabelBMFont::create("GIF", "bigFont.fnt")) {
            badge->setScale(0.22f);
            badge->setPosition({0.f, -dispH * 0.5f + 8.f});
            holderNode->addChild(badge, 3);
        }
    }
}

} // namespace

$execute {
    s_enabled.store(Mod::get()->getSettingValue<bool>("thread-images-enabled"), std::memory_order_relaxed);
    listenForSettingChanges<bool>("thread-images-enabled", [](bool value) {
        s_enabled.store(value, std::memory_order_relaxed);
    });
}

#include "../../framework/HookConventions.hpp"

class $modify(PaimonThreadImages, ThreadViewPopup) {
    struct Fields {
        std::unordered_map<int, RowImages> rows;
        Ref<CCMenu> attachMenu;
        Ref<CCMenu> composeMenu;
        Ref<CCNode> composeBox;
        CCMenuItemSpriteExtra* attachButton = nullptr;
        bool composeLayoutReady = false;
        bool usingNodeIdsCompose = false;
        ComposeBarLayout composeLayout{};
    };

#ifdef GEODE_IS_WINDOWS
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ThreadViewPopup::init");
        paimon::hooks::afterNodeIdsOrLate(self, "ThreadViewPopup::textChanged");
    }
#endif

#ifdef GEODE_IS_WINDOWS
    NativeThreadRow* nativeRows(size_t& count) {
        count = 0;
        // m_rows (std::vector<NativeThreadRow>, 0x18 bytes) sits at the start of the 0x28 pad before m_unk2e0.
        auto const& rows = *reinterpret_cast<std::vector<void*> const*>(
            reinterpret_cast<uintptr_t>(&this->m_unk2e0) - 0x28);
        if (rows.empty()) return nullptr;
        size_t const bytes = rows.size() * sizeof(void*);
        if (bytes % sizeof(NativeThreadRow) != 0) return nullptr;
        count = bytes / sizeof(NativeThreadRow);
        if (count != static_cast<size_t>(this->virtualRowCount())) {
            count = 0;
            return nullptr;
        }
        return reinterpret_cast<NativeThreadRow*>(const_cast<void**>(rows.data()));
    }
#endif

    bool canAttach() {
        return threadImagesEnabled() && !m_isModView && m_thread && m_thread->m_threadID > 0
            && !m_thread->m_isLocked && m_messageInput && m_composeBg && m_composeBg->isVisible();
    }

    void applyFixedComposeNodes() {
#ifdef GEODE_IS_WINDOWS
        if (!m_fields->composeLayoutReady || !m_composeBg || !m_messageInput) return;
        auto const& lay = m_fields->composeLayout;
        *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + kComposeWidthOffset) = lay.composeWidth;

        if (m_fields->usingNodeIdsCompose) {
            auto* menu = m_fields->composeMenu.data();
            auto* box = m_fields->composeBox.data();
            if (menu && box && m_composeBg->getParent() == box && m_messageInput->getParent() == box) {
                CCSize const boxSize = m_composeBg->getContentSize();
                box->setContentSize(boxSize);
                CCPoint const localCenter = boxSize * 0.5f;
                m_composeBg->setPosition(localCenter);
                m_messageInput->setPosition(localCenter);

                if (auto* btn = m_fields->attachButton) {
                    btn->setVisible(m_composeBg->isVisible());
                }

                menu->setContentSize({menu->getContentWidth(), boxSize.height});
                menu->updateLayout();

                if (m_mainLayer) {
                    CCPoint const boxCenterInMain = m_mainLayer->convertToNodeSpace(
                        box->convertToWorldSpace(localCenter)
                    );
                    float const rightEdgeInMain = boxCenterInMain.x + boxSize.width * 0.5f;
                    if (m_charCountLabel && m_charCountLabel->getParent() == m_mainLayer) {
                        m_charCountLabel->setPositionX(rightEdgeInMain - 3.f);
                    }
                    if (m_hideKeyboardButton && m_hideKeyboardButton->getParent()) {
                        CCPoint const hidePos = m_hideKeyboardButton->getParent()->convertToNodeSpace(
                            m_mainLayer->convertToWorldSpace({rightEdgeInMain - 12.f, 0.f})
                        );
                        m_hideKeyboardButton->setPositionX(hidePos.x);
                    }
                }
                return;
            }
        }

        m_composeBg->setPositionX(lay.composeBgX);
        m_messageInput->setPositionX(lay.messageInputX);
        if (auto* btn = m_fields->attachButton) {
            CCPoint pos{lay.attachX, lay.attachY};
            if (auto* parent = btn->getParent(); parent && parent != m_composeBg->getParent()
                && parent->getParent() == m_composeBg->getParent()) {
                pos = pos - parent->getPosition();
            }
            btn->setPosition(pos);
            btn->setVisible(m_composeBg->isVisible());
        }
#endif
    }

    void syncAttachVisibility() {
#ifdef GEODE_IS_WINDOWS
        if (m_fields->attachButton && m_composeBg) {
            m_fields->attachButton->setVisible(m_composeBg->isVisible());
        }
#endif
    }

    bool init(GJMessageThread* thread) {
        if (!ThreadViewPopup::init(thread)) return false;
#ifdef GEODE_IS_WINDOWS
        if (!threadImagesEnabled() || m_isModView || !thread) return true;
        if (!m_composeBg || !m_composeBg->getParent() || !m_messageInput) return true;

        auto* nodeIdsBox = m_composeBg->getParent();
        auto* nodeIdsMenu = m_mainLayer
            ? typeinfo_cast<CCMenu*>(m_mainLayer->getChildByID("compose-menu"))
            : nullptr;
        bool const hasNodeIdsTree = nodeIdsBox
            && nodeIdsMenu
            && nodeIdsBox->getID() == "compose-box"
            && nodeIdsBox->getParent() == nodeIdsMenu
            && m_messageInput->getParent() == nodeIdsBox;

        auto* icon = CCSprite::createWithSpriteFrameName("gj_folderBtn_001.png");
        CCNode* sprite = icon;
        if (icon) {
            icon->setScale(0.6f);
            if (auto* circle = CircleButtonSprite::create(icon, CircleBaseColor::Green, CircleBaseSize::Small)) {
                circle->setScale(0.62f);
                sprite = circle;
            }
        }
        if (!sprite) return true;
        auto* btn = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(PaimonThreadImages::onAttachImage));
        btn->setID("paimon-chat-attach-image"_spr);
        m_fields->attachButton = btn;

        float const origComposeW = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + kComposeWidthOffset);

        if (hasNodeIdsTree) {
            nodeIdsMenu->addChild(btn);
            nodeIdsMenu->insertBefore(btn, nodeIdsBox);

            float gap = 4.f;
            if (auto* axis = typeinfo_cast<AxisLayout*>(nodeIdsMenu->getLayout())) {
                gap = std::max(0.f, axis->getGap());
            }
            float const slotW = btn->getScaledContentSize().width + gap;

            m_fields->composeMenu = nodeIdsMenu;
            m_fields->composeBox = nodeIdsBox;
            m_fields->usingNodeIdsCompose = true;
            m_fields->composeLayout = ComposeBarLayout{};
            m_fields->composeLayout.composeWidth = std::max(120.f, origComposeW - slotW);
            m_fields->composeLayoutReady = true;
            this->layoutComposeBar();
            return true;
        }

        CCMenu* targetMenu = nullptr;
        if (m_buttonMenu && m_buttonMenu->getParent() == m_composeBg->getParent()) {
            targetMenu = m_buttonMenu;
        } else {
            auto* menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            menu->setContentSize(m_composeBg->getParent()->getContentSize());
            m_composeBg->getParent()->addChild(menu, 10);
            m_fields->attachMenu = menu;
            targetMenu = menu;
        }
        targetMenu->addChild(btn);

        m_fields->composeLayout = computeFixedComposeBarLayout(
            origComposeW,
            m_composeBg->getPositionX(),
            m_messageInput->getPositionX(),
            m_composeBg->getPositionY(),
            m_composeBg->getScaleX(),
            m_composeBg->getAnchorPoint().x,
            kAttachSlotWidth,
            kAttachGapFromBox
        );
        m_fields->composeLayoutReady = true;
        this->layoutComposeBar();
#endif
        return true;
    }

    void layoutComposeBar() {
        applyFixedComposeNodes();
        ThreadViewPopup::layoutComposeBar();
        applyFixedComposeNodes();
    }

#ifdef GEODE_IS_WINDOWS
    void textChanged(CCTextInputNode* input) {
        ThreadViewPopup::textChanged(input);
        applyFixedComposeNodes();
    }
#endif

    void threadMessagesFinished(int p0, CCArray* p1, bool p2) {
        ThreadViewPopup::threadMessagesFinished(p0, p1, p2);
        syncAttachVisibility();
    }

    void threadSendFinished(int p0, long long p1) {
        ThreadViewPopup::threadSendFinished(p0, p1);
        syncAttachVisibility();
    }

    void threadUpdateFinished(int p0, int p1) {
        ThreadViewPopup::threadUpdateFinished(p0, p1);
        syncAttachVisibility();
    }

    void exitSearch(bool p0) {
        ThreadViewPopup::exitSearch(p0);
        syncAttachVisibility();
    }

    void onAttachImage(CCObject*) {
        if (!canAttach() || m_sendPending) return;
        WeakRef<PaimonThreadImages> weak = this;
        auto* popup = ChatImageSendPopup::create(
            m_thread->m_threadID,
            m_messageInput->getString(),
            [weak](std::string const& token, std::string const& caption) {
                if (paimon::isRuntimeShuttingDown()) return;
                if (auto self = weak.lock()) self->sendPreparedImageMessage(token, caption);
            });
        if (popup) paimon::community::showOnTop(popup);
    }

    void keyDown(enumKeyCodes key, double timestamp) {
        if (key == KEY_V && canAttach() && m_fields->attachButton && !m_sendPending
            && CCKeyboardDispatcher::get() && CCKeyboardDispatcher::get()->getControlKeyPressed()
            && paimon::clipboardHasImage()) {
            if (auto clip = paimon::readImageFromClipboard()) {
                WeakRef<PaimonThreadImages> weak = this;
                auto* popup = ChatImageSendPopup::createWithClipboard(
                    m_thread->m_threadID, m_messageInput->getString(), std::move(*clip),
                    [weak](std::string const& token, std::string const& caption) {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (auto self = weak.lock()) self->sendPreparedImageMessage(token, caption);
                    });
                if (popup) paimon::community::showOnTop(popup);
                return;
            }
        }
        ThreadViewPopup::keyDown(key, timestamp);
    }

    void sendPreparedImageMessage(std::string const& token, std::string const& caption) {
        if (!m_thread || m_isModView || m_thread->m_isLocked || m_sendPending || !m_messageInput) return;
        std::string text = caption;
        while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.pop_back();
        while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.erase(text.begin());
        size_t const room = kMaxMessageChars > token.size() + 1 ? kMaxMessageChars - token.size() - 1 : 0;
        if (text.size() > room) text.resize(room);
        std::string body = text.empty() ? token : text + " " + token;
        for (char& c : body) if (c == '\n' || c == '\r') c = ' ';

        std::string const previous = m_messageInput->getString();
        m_messageInput->setString(body);
        this->onSend(nullptr);
        if (!m_sendPending) {
            m_messageInput->setString(previous);
            PaimonNotify::show(Localization::get().getString("chatimg.send_blocked"), NotificationIcon::Warning);
        } else {
            m_messageInput->setString("");
        }
    }

    void layoutRows() {
        ThreadViewPopup::layoutRows();
        m_fields->rows.clear();
#ifdef GEODE_IS_WINDOWS
        if (!threadImagesEnabled() || !m_messages) return;
        size_t count = 0;
        auto* rows = nativeRows(count);
        if (!rows) return;

        float const maxCardW = std::min(210.f, std::max(110.f, m_listWidth - 66.f));
        float cumulative = 0.f;
        for (size_t i = 0; i < count; ++i) {
            auto& row = rows[i];
            row.y -= cumulative;
            if (row.type != 4 || row.messageIndex < 0
                || static_cast<unsigned>(row.messageIndex) >= m_messages->count()) continue;
            auto* msg = static_cast<GJThreadMessage*>(m_messages->objectAtIndex(row.messageIndex));
            if (!msg || messageDeleted(msg)) continue;
            auto parsed = parseChatImages(*messageBody(msg));
            if (parsed.images.empty()) continue;

            float blockH = 0.f;
            for (auto const& img : parsed.images) {
                blockH += computeInlineDisplaySize(img.width, img.height, maxCardW, 135.f).height;
            }
            blockH += kCardGap * static_cast<float>(parsed.images.size() - 1);
            float const extra = parsed.displayText.empty()
                ? std::max(0.f, blockH + 8.f - 14.f)
                : blockH + 8.f;
            row.height += extra;
            cumulative += extra;
            m_fields->rows[static_cast<int>(i)] = RowImages{std::move(parsed), messageSender(msg)};
        }
        m_unk2e0 += cumulative;
#endif
    }

    void mountVirtualRow(int rowIdx, CCNode* container, CCMenu* menu) {
        ThreadViewPopup::mountVirtualRow(rowIdx, container, menu);
        if (!threadImagesEnabled() || !container || !menu) return;
        auto it = m_fields->rows.find(rowIdx);
        if (it == m_fields->rows.end()) return;
        auto const& meta = it->second;

        float const rowHeight = container->getContentSize().height;
        TextArea* textArea = nullptr;
        for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
            if (auto* ta = typeinfo_cast<TextArea*>(child)) { textArea = ta; break; }
        }

        float cursorY = rowHeight - 20.f;
        if (textArea) {
            if (meta.parsed.displayText.empty()) {
                textArea->setVisible(false);
                cursorY = textArea->getPositionY() - 2.f;
            } else {
                textArea->setString(meta.parsed.displayText);
                cursorY = textArea->getPositionY() - std::max(14.f, textArea->getContentSize().height) - 4.f;
            }
        }

        int const threadID = m_thread ? m_thread->m_threadID : 0;
        if (threadID <= 0) return;

        float const maxCardW = std::min(210.f, std::max(110.f, m_listWidth - 66.f));
        WeakRef<PaimonThreadImages> weakSelf = this;
        for (auto const& ref : meta.parsed.images) {
            auto size = computeInlineDisplaySize(ref.width, ref.height, maxCardW, 135.f);
            if (cursorY - size.height < 4.f) cursorY = size.height + 4.f;

            auto* card = CCNode::create();
            card->setContentSize({size.width, size.height});
            card->setAnchorPoint({0.5f, 0.5f});
            float const cardRadius = computeImageCornerRadius(size.width, size.height, kInlineCornerRadius);
            if (auto* bg = paimon::SpriteHelper::createRoundedRect(
                    size.width, size.height, cardRadius, {0.f, 0.f, 0.f, 110.f / 255.f})) {
                bg->setAnchorPoint({0.f, 0.f});
                bg->setPosition({0.f, 0.f});
                card->addChild(bg, 0);
            }
            auto* holder = CCNode::create();
            holder->setPosition({size.width * 0.5f, size.height * 0.5f});
            card->addChild(holder, 1);

            struct CardState {
                bool needsVerification = false;
                bool deleted = false;
            };
            auto cardState = std::make_shared<CardState>();
            WeakRef<CCNode> weakHolder = holder;
            int const senderID = meta.senderID;
            auto* btn = CCMenuItemExt::createSpriteExtra(card, [weakSelf, weakHolder, cardState, ref, threadID, senderID](CCMenuItemSpriteExtra*) {
                if (paimon::isRuntimeShuttingDown() || !weakSelf.lock()) return;
                if (cardState->needsVerification) {
                    paimon::community::promptVerification();
                    return;
                }
                if (cardState->deleted) return;
                auto* popup = ChatImageViewerPopup::create(ref, threadID, senderID, [weakHolder, cardState]() {
                    cardState->deleted = true;
                    if (auto node = weakHolder.lock()) {
                        node->removeAllChildren();
                        if (auto* lbl = CCLabelBMFont::create(Localization::get().getString("chatimg.unavailable").c_str(), "chatFont.fnt")) {
                            lbl->setScale(0.55f);
                            lbl->setOpacity(170);
                            node->addChild(lbl);
                        }
                    }
                });
                if (popup) paimon::community::showOnTop(popup);
            });
            btn->setPosition({kTextLeft + size.width * 0.5f, cursorY - size.height * 0.5f});
            menu->addChild(btn);

            float const w = size.width, h = size.height;
            std::string const gifKey = "chatimg:" + std::to_string(threadID) + ":" + ref.id;
            ChatImageService::get().loadImage(ref, threadID, [weakHolder, cardState, gifKey, w, h](LoadedChatImage img) {
                if (paimon::isRuntimeShuttingDown()) return;
                auto holderNode = weakHolder.lock();
                if (!holderNode) return;
                if (img.needsVerification) {
                    cardState->needsVerification = true;
                    if (auto* label = CCLabelBMFont::create(Localization::get().getString("chatimg.verify_to_view").c_str(), "chatFont.fnt")) {
                        label->setScale(std::min(0.6f, (w - 12.f) / std::max(1.f, label->getContentSize().width)));
                        label->setColor({255, 220, 120});
                        holderNode->addChild(label);
                    }
                    return;
                }
                if (!img.ok) {
                    if (auto* label = CCLabelBMFont::create("?", "chatFont.fnt")) {
                        label->setScale(0.7f);
                        label->setOpacity(150);
                        holderNode->addChild(label);
                    }
                    return;
                }
                if (img.isGif) {
                    AnimatedGIFSprite::createAsync(img.rawBytes, gifKey,
                        [weakHolder, w, h](AnimatedGIFSprite* gif) {
                            if (paimon::isRuntimeShuttingDown() || !gif) return;
                            auto node = weakHolder.lock();
                            if (!node) return;
                            mountRoundedInlineImage(node, gif, w, h, true);
                        });
                    return;
                }
                if (img.texture) {
                    if (auto* spr = CCSprite::createWithTexture(img.texture)) {
                        mountRoundedInlineImage(holderNode, spr, w, h, false);
                    }
                }
            });
            cursorY -= size.height + kCardGap;
        }
    }
};
