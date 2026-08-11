#include "CollabInviteBanner.hpp"

#include "../../utils/SpriteHelper.hpp"
#include "CollabManager.hpp"
#include "CollabPopups.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <algorithm>
#include <functional>

using namespace geode::prelude;

namespace paimon::collab {
namespace {

constexpr float kCardW = 360.f;
constexpr float kCardH = 62.f;
constexpr float kTopGap = 8.f;
constexpr float kIconX = 32.f;
constexpr float kTextX = 54.f;
constexpr float kBarInset = 14.f;

constexpr float kEnterDur = 0.5f;
constexpr float kHoldDur = 15.f;
constexpr float kAcceptDur = 0.65f;
constexpr float kExitDur = 0.34f;

// Above the capture preview (999000), below the color picker HUD (999500).
constexpr int kZOrder = 999100;
// Beats popups and text inputs (~-500): an invite that lands while an alert is
// open still has working buttons. CCMenu only claims touches that hit a button,
// so the popup underneath keeps working.
constexpr int kTouchPriority = -1000;

float easeOutCubic(float t) {
    float u = 1.f - t;
    return 1.f - u * u * u;
}

float easeInCubic(float t) {
    return t * t * t;
}

// Slight overshoot: the panel drops past its slot and settles back up.
float easeOutBack(float t) {
    constexpr float s = 1.45f;
    float u = t - 1.f;
    return 1.f + u * u * ((s + 1.f) * u + s);
}

CCMenuItemSpriteExtra* makeButton(CCObject* target, SEL_MenuHandler selector,
                                  char const* text, char const* bg, float height) {
    auto* spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.6f);
    auto* item = CCMenuItemSpriteExtra::create(spr, target, selector);
    if (item && item->getContentSize().height > 0.f) {
        item->setScale(height / item->getContentSize().height);
    }
    return item;
}

float scaledWidth(CCNode* node) {
    return node ? node->getContentSize().width * node->getScaleX() : 0.f;
}

} // namespace

CollabInviteBanner* CollabInviteBanner::s_instance = nullptr;

void CollabInviteBanner::present(std::string const& room, std::string const& fromName) {
    if (room.empty()) return;

    // Two stacked banners over the game read worse than losing the older one.
    if (s_instance) {
        if (s_instance->m_menu) s_instance->m_menu->setEnabled(false);
        s_instance->removeFromParent();
        s_instance = nullptr;
    }

    auto* banner = new CollabInviteBanner();
    if (!banner->init(room, fromName)) {
        CC_SAFE_DELETE(banner);
        return;
    }
    banner->autorelease();

    if (auto* host = OverlayManager::get()) {
        host->addChild(banner, kZOrder);
    } else if (auto* scene = CCDirector::get()->getRunningScene()) {
        scene->addChild(banner, 99999);
    }
}

bool CollabInviteBanner::init(std::string const& room, std::string const& fromName) {
    if (!CCNode::init()) return false;
    s_instance = this;
    m_room = room;

    auto win = CCDirector::get()->getWinSize();
    this->setID("collab-invite-banner"_spr);
    this->setContentSize({kCardW, kCardH});
    this->setAnchorPoint({0.5f, 0.5f});

    m_showY = win.height - kCardH / 2.f - kTopGap;
    m_hideY = win.height + kCardH;
    this->setPosition({win.width / 2.f, m_hideY});
    this->setScale(0.92f);

    // Vanilla GD popup panel, same frame every alert in the game uses.
    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
        panel->setContentSize({kCardW, kCardH});
        panel->setPosition({kCardW / 2.f, kCardH / 2.f});
        this->addChild(panel, 0);
    }

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 3);

    auto* accept = makeButton(this, menu_selector(CollabInviteBanner::onAccept),
        "Aceptar", "GJ_button_01.png", 26.f);
    auto* reject = makeButton(this, menu_selector(CollabInviteBanner::onReject),
        "Rechazar", "GJ_button_06.png", 26.f);
    if (accept) m_menu->addChild(accept);
    if (reject) m_menu->addChild(reject);

    float acceptW = scaledWidth(accept);
    float rejectW = scaledWidth(reject);
    if (accept) accept->setPosition({kCardW - 12.f - acceptW / 2.f, 35.f});
    if (reject) reject->setPosition({kCardW - 12.f - acceptW - 7.f - rejectW / 2.f, 35.f});

    float textW = std::max(kCardW - kTextX - acceptW - rejectW - 30.f, 120.f);

    if (auto* paimon = paimon::SpriteHelper::safeCreate("paim_Paimon.png"_spr)) {
        paimon->setScale(32.f / std::max(paimon->getContentSize().height, 1.f));
        paimon->setPosition({kIconX, 35.f});
        this->addChild(paimon, 2);
        // Keeps the banner feeling alive while it waits for an answer.
        paimon->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineInOut::create(CCMoveBy::create(0.9f, {0.f, 2.5f})),
            CCEaseSineInOut::create(CCMoveBy::create(0.9f, {0.f, -2.5f})),
            nullptr)));
    }

    std::string who = fromName.empty() ? "Alguien" : fromName;
    auto* title = CCLabelBMFont::create(
        fmt::format("{} te invito a colaborar", who).c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->limitLabelWidth(textW, 0.38f, 0.2f);
    title->setPosition({kTextX, 43.f});
    this->addChild(title, 2);

    m_roomLabel = CCLabelBMFont::create(fmt::format("Sala {}", room).c_str(), "goldFont.fnt");
    m_roomLabel->setAnchorPoint({0.f, 0.5f});
    m_roomLabel->limitLabelWidth(textW, 0.32f, 0.18f);
    m_roomLabel->setPosition({kTextX, 26.f});
    this->addChild(m_roomLabel, 2);

    // Countdown along the bottom of the panel: same track + fill pair the collab
    // chat uses for its mic level, driven with scaleX so it drains smoothly.
    float trackW = kCardW - kBarInset * 2.f;
    auto* track = CCLayerColor::create({0, 0, 0, 110}, trackW, 4.f);
    track->ignoreAnchorPointForPosition(false);
    track->setAnchorPoint({0.f, 0.5f});
    track->setPosition({kBarInset, 10.f});
    this->addChild(track, 1);

    m_barFill = CCLayerColor::create({255, 214, 122, 235}, trackW, 4.f);
    m_barFill->ignoreAnchorPointForPosition(false);
    m_barFill->setAnchorPoint({0.f, 0.5f});
    m_barFill->setPosition({kBarInset, 10.f});
    this->addChild(m_barFill, 2);

    this->captureFade();
    this->applyAlpha(0.f);
    this->toPhase(Phase::Enter, kEnterDur);
    return true;
}

void CollabInviteBanner::onEnter() {
    CCNode::onEnter();
    this->schedule(schedule_selector(CollabInviteBanner::tick));

    if (m_priorityQueued) return;
    m_priorityQueued = true;
    // Re-registering the handler while the dispatcher is locked (mid-touch)
    // dereferences a handler that is still in the pending-add queue; one frame
    // later it has been committed.
    WeakRef<CollabInviteBanner> weak = this;
    Loader::get()->queueInMainThread([weak]() {
        if (auto self = weak.lock(); self && self->m_menu) {
            self->m_menu->setHandlerPriority(kTouchPriority);
        }
    });
}

void CollabInviteBanner::onExit() {
    this->unschedule(schedule_selector(CollabInviteBanner::tick));
    CCNode::onExit();
    if (s_instance == this) s_instance = nullptr;
}

void CollabInviteBanner::toPhase(Phase phase, float duration) {
    m_phase = phase;
    m_phaseTime = 0.f;
    m_phaseDuration = duration;
}

void CollabInviteBanner::tick(float dt) {
    m_phaseTime += dt;
    float t = m_phaseDuration > 0.f ? std::clamp(m_phaseTime / m_phaseDuration, 0.f, 1.f) : 1.f;
    float centerX = CCDirector::get()->getWinSize().width / 2.f;

    switch (m_phase) {
        case Phase::Enter: {
            float slide = easeOutBack(t);
            this->setPosition({centerX, m_hideY + (m_showY - m_hideY) * slide});
            this->setScale(0.92f + 0.08f * easeOutCubic(t));
            this->applyAlpha(easeOutCubic(std::min(t * 1.7f, 1.f)));
            if (t >= 1.f) this->toPhase(Phase::Hold, kHoldDur);
            break;
        }
        case Phase::Hold: {
            if (m_barFill) m_barFill->setScaleX(1.f - t);
            if (t >= 1.f) this->toPhase(Phase::Exit, kExitDur);
            break;
        }
        case Phase::Accepted: {
            if (t >= 1.f) this->toPhase(Phase::Exit, kExitDur);
            break;
        }
        case Phase::Exit: {
            float slide = easeInCubic(t);
            this->setPosition({centerX, m_showY + (m_hideY - m_showY) * slide});
            this->setScale(1.f - 0.1f * slide);
            this->applyAlpha(1.f - easeInCubic(std::min(t * 1.3f, 1.f)));
            if (t >= 1.f) this->removeFromParent();
            break;
        }
    }
}

void CollabInviteBanner::captureFade() {
    m_fade.clear();
    // Read every opacity before touching any of them: containers push their own
    // onto their children, so a half-faded parent would poison the targets.
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) return;
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
            m_fade.emplace_back(node, rgba->getOpacity());
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
        }
    };
    if (auto* children = this->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
    }
}

void CollabInviteBanner::applyAlpha(float alpha) {
    float a = std::clamp(alpha, 0.f, 1.f);
    for (auto& [node, base] : m_fade) {
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node.data())) {
            rgba->setOpacity(static_cast<GLubyte>(base * a));
        }
    }
}

void CollabInviteBanner::onAccept(CCObject*) {
    if (m_phase != Phase::Enter && m_phase != Phase::Hold) return;
    if (m_menu) {
        m_menu->setEnabled(false);
        m_menu->setVisible(false);
    }
    // Joining is the only feedback the banner can give before the editor opens.
    this->applyAlpha(1.f);
    if (m_roomLabel) {
        m_roomLabel->setString("Uniendote a la sala...");
        m_roomLabel->setColor({150, 255, 170});
        float scale = m_roomLabel->getScale();
        m_roomLabel->setScale(scale * 0.85f);
        m_roomLabel->runAction(CCEaseSineOut::create(CCScaleTo::create(0.18f, scale)));
    }
    if (m_barFill) m_barFill->setScaleX(0.f);
    this->captureFade(); // setString rebuilt the label's glyphs
    this->toPhase(Phase::Accepted, kAcceptDur);

    CollabManager::get().connect(m_room, defaultDisplayName(), ConnectMode::Join);
}

void CollabInviteBanner::onReject(CCObject*) {
    if (m_phase != Phase::Enter && m_phase != Phase::Hold) return;
    if (m_menu) m_menu->setEnabled(false);
    this->toPhase(Phase::Exit, kExitDur);
}

} // namespace paimon::collab
