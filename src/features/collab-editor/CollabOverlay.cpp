#include "CollabOverlay.hpp"

#include "CollabEmotes.hpp"
#include "CollabManager.hpp"
#include "CollabPopups.hpp"
#include "CollabVoice.hpp"

#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

constexpr float kTagBaseScale = 0.45f;
constexpr int kMaxConcurrentFlashes = 48;

// Toast notifications.
constexpr int   kMaxToasts     = 2;      // shown at once; extras evict the oldest
constexpr float kToastHold     = 4.0f;   // seconds fully visible before leaving
constexpr float kToastEnter    = 0.35f;  // slide-in duration
constexpr float kToastExit     = 0.3f;   // slide-out duration
constexpr float kToastHeight   = 30.f;   // slot height used for stacking
constexpr float kToastGap      = 6.f;    // vertical gap between toasts
constexpr float kToastTopMargin= 10.f;   // space from the top edge
constexpr int   kToastMoveTag  = 71;     // reposition action tag

} // namespace

CollabEditorOverlay* CollabEditorOverlay::create(LevelEditorLayer* editor) {
    auto* ret = new CollabEditorOverlay();
    if (ret && ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabEditorOverlay::init(LevelEditorLayer* editor) {
    if (!CCNode::init()) return false;
    m_editor = editor;
    setID("collab-overlay"_spr);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    setContentSize(winSize);

    m_toastLayer = CCNode::create();
    m_toastLayer->setPosition({0.f, 0.f});
    m_toastLayer->setContentSize(winSize);
    m_toastLayer->setZOrder(500);
    addChild(m_toastLayer);

    buildButtons();

    CollabManager::get().setOverlay(this);
    schedule(schedule_selector(CollabEditorOverlay::refresh), 0.1f);
    return true;
}

CollabEditorOverlay::~CollabEditorOverlay() {
    // Unregister first: chat/system messages can arrive at any moment (join_ok,
    // poll) and the manager must never call into a freed overlay.
    CollabManager::get().clearOverlay(this);

    // Tags are children of the object layer (not ours); drop them explicitly
    // in case the overlay dies before the editor does.
    for (auto& [id, tag] : m_tags) {
        if (tag && tag->getParent()) tag->removeFromParent();
    }
    m_tags.clear();
}

void CollabEditorOverlay::buildButtons() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Chat and mic live in the Ctrl+E popup now; the overlay only shows who's
    // talking, keeping the editor UI clear.
    m_speakingLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_speakingLabel->setScale(0.45f);
    m_speakingLabel->setAnchorPoint({1.f, 1.f});
    m_speakingLabel->setPosition({winSize.width - 4.f, winSize.height * 0.5f - 30.f});
    m_speakingLabel->setColor({140, 255, 140});
    addChild(m_speakingLabel);
}

void CollabEditorOverlay::refresh(float) {
    auto& mgr = CollabManager::get();
    bool active = mgr.connected();
    m_speakingLabel->setVisible(active);
    if (!active) return;

    auto& voice = CollabVoice::get();

    std::string speaking;
    if (voice.transmitting()) speaking = "tu";
    for (auto const& name : voice.speakingPeers()) {
        if (!speaking.empty()) speaking += ", ";
        speaking += name;
    }
    m_speakingLabel->setString(speaking.empty() ? "" : fmt::format("Hablando: {}", speaking).c_str());

    // Keep attribution tags readable at any zoom level.
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (objectLayer) {
        float zoom = objectLayer->getScale();
        if (zoom > 0.f) {
            float scale = std::clamp(kTagBaseScale / zoom, 0.1f, 6.f);
            for (auto& [id, tag] : m_tags) {
                if (tag && tag->getParent() && tag->isVisible()) tag->setScale(scale);
            }
        }
    }
}

void CollabEditorOverlay::onRemoteEdit(int clientId, std::string const& name, CCPoint worldPos, bool isDelete) {
    auto* objectLayer = m_editor ? m_editor->m_objectLayer : nullptr;
    if (!objectLayer) return;

    auto color = peerColor(clientId);

    // Brief flash on the touched spot so bursts of edits are visible even when
    // the tag has already moved on. Capped to survive 2k-object pastes.
    if (m_flashCount < kMaxConcurrentFlashes) {
        if (auto* flash = CCSprite::create("square02b_001.png")) {
            ++m_flashCount;
            flash->setPosition(worldPos);
            flash->setScale(0.4f);
            flash->setColor(isDelete ? ccColor3B{255, 70, 70} : color);
            flash->setOpacity(110);
            flash->setZOrder(9000);
            objectLayer->addChild(flash);
            flash->runAction(CCSequence::create(
                CCFadeOut::create(0.45f),
                CCCallFunc::create(flash, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            // Track completion via a scheduled decrement on ourselves.
            runAction(CCSequence::create(
                CCDelayTime::create(0.5f),
                CCCallFuncO::create(this, callfuncO_selector(CollabEditorOverlay::onFlashDone), nullptr),
                nullptr
            ));
        }
    }

    // Per-peer name tag jumps to the latest edit and fades out.
    auto it = m_tags.find(clientId);
    CCLabelBMFont* tag = (it != m_tags.end()) ? it->second.data() : nullptr;
    if (!tag || !tag->getParent()) {
        tag = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        if (!tag) return;
        tag->setZOrder(9500);
        objectLayer->addChild(tag);
        m_tags[clientId] = tag;
    }
    tag->setString(name.c_str());
    tag->setColor(color);
    tag->setPosition(worldPos + CCPoint{0.f, 14.f});
    float zoom = objectLayer->getScale();
    tag->setScale(zoom > 0.f ? std::clamp(kTagBaseScale / zoom, 0.1f, 6.f) : kTagBaseScale);
    tag->setVisible(true);
    tag->stopAllActions();
    tag->setOpacity(255);
    tag->runAction(CCSequence::create(
        CCDelayTime::create(1.6f),
        CCFadeOut::create(0.8f),
        CCHide::create(),
        nullptr
    ));
}

void CollabEditorOverlay::onFlashDone(CCObject*) {
    if (m_flashCount > 0) --m_flashCount;
}

void CollabEditorOverlay::onChat(ChatMessage const& msg) {
    showToast(msg);
}

CCNode* CollabEditorOverlay::buildToast(ChatMessage const& msg) {
    auto* content = buildChatLine(msg, 0.5f);
    if (!content) return nullptr;

    float cw = content->getContentSize().width * content->getScaleX();
    float ch = content->getContentSize().height * content->getScaleY();
    cw = std::clamp(cw, 40.f, 340.f);

    float w = cw + 22.f;
    float h = std::max(ch + 12.f, 24.f);

    auto* toast = CCNode::create();
    toast->setAnchorPoint({0.5f, 0.5f});
    toast->setContentSize({w, h});

    auto* bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({w, h});
    bg->setPosition({w / 2.f, h / 2.f});
    bg->setColor({12, 14, 22});
    bg->setOpacity(205);
    toast->addChild(bg);

    content->setAnchorPoint({0.5f, 0.5f});
    content->setPosition({w / 2.f, h / 2.f});
    toast->addChild(content);

    return toast;
}

void CollabEditorOverlay::showToast(ChatMessage const& msg) {
    if (!m_toastLayer) return;

    auto* toast = buildToast(msg);
    if (!toast) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    // Start just above the screen, horizontally centered; layoutToasts slides
    // it down into its slot for the "drop from the top" feel.
    toast->setPosition({winSize.width / 2.f, winSize.height + kToastHeight});
    m_toastLayer->addChild(toast);
    m_toasts.emplace_back(toast);

    // Cap: evict the oldest so at most kMaxToasts remain, each leaving on its
    // own animation.
    while (static_cast<int>(m_toasts.size()) > kMaxToasts) {
        dismissToast(m_toasts.front());
    }

    layoutToasts();

    // Independent auto-dismiss timer for this toast.
    toast->runAction(CCSequence::create(
        CCDelayTime::create(kToastEnter + kToastHold),
        CCCallFuncO::create(this, callfuncO_selector(CollabEditorOverlay::onToastExpired), toast),
        nullptr
    ));
}

void CollabEditorOverlay::layoutToasts() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float topY = winSize.height - kToastTopMargin - kToastHeight / 2.f;
    float x = winSize.width / 2.f;

    int n = static_cast<int>(m_toasts.size());
    for (int p = 0; p < n; ++p) {
        auto* toast = m_toasts[p].data();
        if (!toast) continue;
        // Newest (back of the vector) sits at the top slot.
        int slot = (n - 1) - p;
        float targetY = topY - slot * (kToastHeight + kToastGap);
        toast->stopActionByTag(kToastMoveTag);
        auto* move = CCEaseOut::create(CCMoveTo::create(kToastEnter, {x, targetY}), 2.f);
        move->setTag(kToastMoveTag);
        toast->runAction(move);
    }
}

void CollabEditorOverlay::onToastExpired(CCObject* sender) {
    dismissToast(typeinfo_cast<CCNode*>(sender));
}

void CollabEditorOverlay::dismissToast(CCNode* toast) {
    if (!toast) return;
    auto it = std::find_if(m_toasts.begin(), m_toasts.end(),
        [toast](geode::Ref<CCNode> const& r) { return r.data() == toast; });
    if (it == m_toasts.end()) return; // already leaving
    m_toasts.erase(it);

    // Slide up and fade out, then remove. Fade each direct child since CCNode
    // doesn't cascade opacity in this cocos fork.
    toast->stopAllActions();
    if (auto* kids = toast->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(kids)) {
            if (child) child->runAction(CCFadeOut::create(kToastExit));
        }
    }
    float upY = toast->getPositionY() + 22.f;
    toast->runAction(CCSequence::create(
        CCEaseIn::create(CCMoveTo::create(kToastExit, {toast->getPositionX(), upY}), 2.f),
        CCCallFunc::create(toast, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));

    // Reflow the remaining toasts into their new slots.
    layoutToasts();
}

} // namespace paimon::collab
