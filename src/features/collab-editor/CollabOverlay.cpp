#include "CollabOverlay.hpp"

#include "CollabEmotes.hpp"
#include "CollabManager.hpp"
#include "CollabPopups.hpp"
#include "CollabVoice.hpp"

#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

constexpr float kTagBaseScale = 0.45f;
constexpr int kMaxConcurrentFlashes = 48;

// Toast notifications (chat/system notices). They slide in from the right
// edge so they never sit on top of the canvas center while editing.
constexpr int   kMaxToasts      = 3;     // shown at once; extras evict the oldest
constexpr float kToastHold      = 4.0f;  // seconds fully visible before leaving
constexpr float kToastEnter     = 0.4f;  // slide-in duration
constexpr float kToastExit      = 0.3f;  // slide-out duration
constexpr float kToastGap       = 6.f;   // vertical gap between toasts
constexpr float kToastTopMargin = 42.f;  // clears the editor pause button
constexpr float kToastRightMargin = 8.f; // space from the right edge
constexpr int   kToastMoveTag   = 71;    // reposition action tag

// Voice chips (who's talking, top center).
constexpr float kChipHeight  = 26.f;
constexpr float kChipGap     = 8.f;
constexpr float kChipTopY    = 18.f; // distance from the top edge to chip center
constexpr int   kChipMoveTag = 72;

// Fades every RGBA-capable node in the tree; plain container nodes in this
// cocos fork don't cascade opacity, so each descendant animates itself.
void fadeOutTree(CCNode* node, float duration) {
    if (!node) return;
    if (dynamic_cast<CCRGBAProtocol*>(node)) {
        node->stopAllActions(); // a fade-in may still be running
        node->runAction(CCFadeOut::create(duration));
    }
    if (auto* kids = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(kids)) fadeOutTree(child, duration);
    }
}

// Fade-in to each node's own authored opacity (bg is translucent, text solid).
void fadeInTree(CCNode* node, float duration) {
    if (!node) return;
    if (auto* rgba = dynamic_cast<CCRGBAProtocol*>(node)) {
        GLubyte target = rgba->getOpacity();
        rgba->setOpacity(0);
        node->runAction(CCFadeTo::create(duration, target));
    }
    if (auto* kids = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(kids)) fadeInTree(child, duration);
    }
}

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

    m_voiceLayer = CCNode::create();
    m_voiceLayer->setPosition({0.f, 0.f});
    m_voiceLayer->setContentSize(winSize);
    m_voiceLayer->setZOrder(510);
    addChild(m_voiceLayer);

    CollabManager::get().setOverlay(this);
    schedule(schedule_selector(CollabEditorOverlay::refresh), 0.1f);
    // Voice bars animate every frame so levels rise and fall smoothly.
    schedule(schedule_selector(CollabEditorOverlay::updateVoice));
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

void CollabEditorOverlay::refresh(float) {
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
    bg->setOpacity(195);
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
    // Start just off the right edge at the top slot; layoutToasts eases it in.
    toast->setPosition({
        winSize.width + toast->getContentSize().width / 2.f + 12.f,
        winSize.height - kToastTopMargin - toast->getContentSize().height / 2.f,
    });
    m_toastLayer->addChild(toast);
    m_toasts.emplace_back(toast);
    fadeInTree(toast, kToastEnter * 0.75f);

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

    // Newest (back of the vector) at the top, right-aligned, stacking down by
    // each toast's real height so mixed sizes never overlap.
    float y = winSize.height - kToastTopMargin;
    for (int p = static_cast<int>(m_toasts.size()) - 1; p >= 0; --p) {
        auto* toast = m_toasts[p].data();
        if (!toast) continue;
        float w = toast->getContentSize().width;
        float h = toast->getContentSize().height;
        CCPoint target{winSize.width - kToastRightMargin - w / 2.f, y - h / 2.f};
        y -= h + kToastGap;

        toast->stopActionByTag(kToastMoveTag);
        auto* move = CCEaseOut::create(CCMoveTo::create(kToastEnter, target), 3.f);
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

    // Slide back out to the right and fade, then remove.
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    toast->stopAllActions();
    fadeOutTree(toast, kToastExit);
    float outX = winSize.width + toast->getContentSize().width / 2.f + 16.f;
    toast->runAction(CCSequence::create(
        CCEaseIn::create(CCMoveTo::create(kToastExit, {outX, toast->getPositionY()}), 2.f),
        CCCallFunc::create(toast, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));

    // Reflow the remaining toasts into their new slots.
    layoutToasts();
}

// ---------------------------------------------------------------------------
// Voice chips
// ---------------------------------------------------------------------------

void CollabEditorOverlay::buildVoiceChip(int clientId, std::string const& name, VoiceChip& chip) {
    auto* nameLabel = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
    nameLabel->setScale(0.4f);
    nameLabel->limitLabelWidth(84.f, 0.4f, 0.15f);
    float nameW = nameLabel->getContentSize().width * nameLabel->getScale();

    constexpr float kTextX = 28.f;
    float barW = std::max(nameW, 36.f);
    float w = kTextX + barW + 8.f;
    float h = kChipHeight;

    auto* root = CCNode::create();
    root->setContentSize({w, h});
    root->setAnchorPoint({0.5f, 0.5f});

    auto* bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({w, h});
    bg->setColor({10, 12, 20});
    bg->setOpacity(180);
    bg->setPosition({w / 2.f, h / 2.f});
    root->addChild(bg);

    auto color = clientId == 0 ? ccColor3B{140, 255, 140} : peerColor(clientId);

    // Avatar: colored dot with the speaker's initial.
    auto* dot = CCDrawNode::create();
    dot->drawDot({0.f, 0.f}, 8.f, ccColor4F{color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f});
    dot->setPosition({14.f, h / 2.f});
    root->addChild(dot, 1);

    char initial = '?';
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            initial = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            break;
        }
    }
    auto* letter = CCLabelBMFont::create(std::string(1, initial).c_str(), "bigFont.fnt");
    letter->setScale(0.3f);
    letter->setColor({20, 22, 30});
    letter->setPosition({14.f, h / 2.f + 0.5f});
    root->addChild(letter, 2);

    nameLabel->setAnchorPoint({0.f, 1.f});
    nameLabel->setPosition({kTextX, h - 4.f});
    root->addChild(nameLabel, 1);

    // Level bar under the name: dark track + colored fill scaled by loudness.
    auto* track = CCLayerColor::create({0, 0, 0, 130}, barW, 3.5f);
    track->ignoreAnchorPointForPosition(false);
    track->setAnchorPoint({0.f, 0.f});
    track->setPosition({kTextX, 5.f});
    root->addChild(track, 1);

    auto* fill = CCLayerColor::create({color.r, color.g, color.b, 255}, barW, 3.5f);
    fill->ignoreAnchorPointForPosition(false);
    fill->setAnchorPoint({0.f, 0.f});
    fill->setPosition({kTextX, 5.f});
    fill->setScaleX(0.f);
    root->addChild(fill, 2);

    chip.root = root;
    chip.barFill = fill;
    chip.width = w;
}

void CollabEditorOverlay::updateVoice(float dt) {
    if (!m_voiceLayer) return;
    auto& voice = CollabVoice::get();

    // Who should have a chip right now (local user included while transmitting).
    std::vector<SpeakingInfo> want;
    if (CollabManager::get().connected()) {
        want = voice.speakingNow();
        if (voice.transmitting()) want.push_back({0, "Tu", voice.localLevel()});
    }

    bool changed = false;
    std::unordered_set<int> active;
    for (auto const& s : want) {
        active.insert(s.clientId);
        auto it = m_voiceChips.find(s.clientId);
        if (it == m_voiceChips.end()) {
            VoiceChip chip;
            buildVoiceChip(s.clientId, s.name, chip);
            chip.root->setScale(0.6f);
            chip.root->setVisible(false); // until the first layout places it
            m_voiceLayer->addChild(chip.root);
            it = m_voiceChips.emplace(s.clientId, std::move(chip)).first;
            changed = true;
        }
        it->second.target = std::clamp(s.level, 0.f, 1.f);
        it->second.silent = 0.f;
    }

    for (auto it = m_voiceChips.begin(); it != m_voiceChips.end();) {
        auto& chip = it->second;
        if (!active.count(it->first)) {
            chip.target = 0.f;
            chip.silent += dt;
            // Linger briefly so pauses between words don't flicker the chip.
            if (chip.silent > 0.6f) {
                auto* node = chip.root.data();
                node->stopAllActions();
                node->runAction(CCSequence::create(
                    CCEaseIn::create(CCScaleTo::create(0.18f, 0.f), 2.f),
                    CCCallFunc::create(node, callfunc_selector(CCNode::removeFromParent)),
                    nullptr
                ));
                it = m_voiceChips.erase(it);
                changed = true;
                continue;
            }
        }
        // Fast attack, slower release: the bar jumps up with the voice and
        // drains smoothly in silences.
        float k = chip.target > chip.shown ? 16.f : 6.f;
        chip.shown += (chip.target - chip.shown) * std::min(1.f, k * dt);
        if (chip.barFill) chip.barFill->setScaleX(std::clamp(chip.shown, 0.f, 1.f));
        ++it;
    }

    if (changed) layoutVoiceChips();
}

void CollabEditorOverlay::layoutVoiceChips() {
    if (m_voiceChips.empty()) return;
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Stable order: you first (id 0), then peers by client id.
    std::vector<int> ids;
    ids.reserve(m_voiceChips.size());
    for (auto const& [id, chip] : m_voiceChips) ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    float total = -kChipGap;
    for (int id : ids) total += m_voiceChips[id].width + kChipGap;

    float x = winSize.width / 2.f - total / 2.f;
    float y = winSize.height - kChipTopY;
    for (int id : ids) {
        auto& chip = m_voiceChips[id];
        float cx = x + chip.width / 2.f;
        x += chip.width + kChipGap;

        auto* node = chip.root.data();
        if (!chip.placed) {
            chip.placed = true;
            node->setPosition({cx, y});
            node->setVisible(true);
            node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.f)));
        } else {
            node->stopActionByTag(kChipMoveTag);
            auto* move = CCEaseOut::create(CCMoveTo::create(0.2f, {cx, y}), 2.5f);
            move->setTag(kChipMoveTag);
            node->runAction(move);
        }
    }
}

} // namespace paimon::collab
