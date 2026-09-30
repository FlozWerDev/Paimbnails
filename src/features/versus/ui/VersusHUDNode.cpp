#include "VersusHUDNode.hpp"
#include "../services/VersusEffects.hpp"
#include "../services/VersusGlobed.hpp"
#include "../services/VersusSession.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kBarWidth = 190.f;
constexpr float kRopeWidth = 120.f;
constexpr ccColor3B kOwnColor = {90, 180, 250};
constexpr ccColor3B kRivalColor = {240, 90, 110};

CCProgressTimer* makeFill(char const* frame, float width, ccColor3B color) {
    auto* sprite = paimon::SpriteHelper::safeCreate(frame);
    if (!sprite) return nullptr;

    auto* timer = CCProgressTimer::create(sprite);
    if (!timer) return nullptr;

    timer->setType(kCCProgressTimerTypeBar);
    timer->setMidpoint({0.f, 0.5f});
    timer->setBarChangeRate({1.f, 0.f});
    timer->setPercentage(0.f);
    timer->setScale(width / std::max(1.f, sprite->getContentSize().width));
    timer->setColor(color);
    return timer;
}

} // namespace

VersusHUDNode* VersusHUDNode::create() {
    auto ret = new VersusHUDNode();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusHUDNode::init() {
    if (!CCNode::init()) return false;

    auto const winSize = CCDirector::get()->getWinSize();
    this->setPosition({0.f, 0.f});

    m_ownRow = buildBar(true, winSize.height - 22.f);
    m_rivalRow = buildBar(false, winSize.height - 40.f);
    buildRope(winSize.height - 74.f);

    m_clock = CCLabelBMFont::create("", "bigFont.fnt");
    m_clock->setScale(0.42f);
    m_clock->setPosition({winSize.width / 2.f, winSize.height - 58.f});
    m_clock->setOpacity(210);
    m_clock->setVisible(false);
    this->addChild(m_clock, 2);

    m_ping = CCLabelBMFont::create("", "chatFont.fnt");
    m_ping->setScale(0.36f);
    m_ping->setAnchorPoint({1.f, 0.5f});
    m_ping->setPosition({winSize.width - 8.f, winSize.height - 56.f});
    m_ping->setOpacity(130);
    this->addChild(m_ping, 2);

    m_countdown = CCLabelBMFont::create("", "bigFont.fnt");
    m_countdown->setScale(1.6f);
    m_countdown->setPosition({winSize.width / 2.f, winSize.height / 2.f});
    m_countdown->setVisible(false);
    this->addChild(m_countdown, 5);

    this->scheduleUpdate();
    refresh();
    return true;
}

CCNode* VersusHUDNode::buildBar(bool own, float y) {
    auto const winSize = CCDirector::get()->getWinSize();
    float const centerX = winSize.width / 2.f;

    auto* row = CCNode::create();
    row->setPosition({centerX, y});
    this->addChild(row, 1);

    if (auto* fill = makeFill(Mod::get()->expandSpriteName("paim_vsBarFill.png").c_str(),
                              kBarWidth, own ? kOwnColor : kRivalColor)) {
        fill->setPosition({0.f, 0.f});
        row->addChild(fill, 1);
        (own ? m_ownFill : m_rivalFill) = fill;
    }

    if (auto* frame = paimon::SpriteHelper::safeCreate("paim_vsBar.png"_spr)) {
        frame->setScale(kBarWidth / std::max(1.f, frame->getContentSize().width));
        frame->setPosition({0.f, 0.f});
        frame->setColor({210, 216, 232});
        row->addChild(frame, 2);
    }

    auto* label = CCLabelBMFont::create("0%", "bigFont.fnt");
    label->setScale(0.36f);
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({kBarWidth / 2.f + 6.f, 0.f});
    label->setColor(own ? kOwnColor : kRivalColor);
    row->addChild(label, 3);
    (own ? m_ownLabel : m_rivalLabel) = label;

    if (!own) {
        m_rivalName = CCLabelBMFont::create("", "chatFont.fnt");
        m_rivalName->setScale(0.4f);
        m_rivalName->setAnchorPoint({1.f, 0.5f});
        m_rivalName->setPosition({-kBarWidth / 2.f - 6.f, 0.f});
        m_rivalName->setOpacity(190);
        row->addChild(m_rivalName, 3);
    }

    return row;
}

// tug of war is decided by a number nobody can see otherwise, so the rope gets
// its own strip. every other format leaves it hidden.
void VersusHUDNode::buildRope(float y) {
    auto const winSize = CCDirector::get()->getWinSize();

    m_ropeRow = CCNode::create();
    m_ropeRow->setPosition({winSize.width / 2.f, y});
    m_ropeRow->setVisible(false);
    this->addChild(m_ropeRow, 1);

    if (auto* frame = paimon::SpriteHelper::safeCreate("paim_vsBar.png"_spr)) {
        frame->setScale(kRopeWidth / std::max(1.f, frame->getContentSize().width));
        frame->setColor({210, 216, 232});
        m_ropeRow->addChild(frame, 1);
    }

    m_ropePip = paimon::SpriteHelper::safeCreate("paim_vsPip.png"_spr);
    if (!m_ropePip) return;
    m_ropePip->setScale(14.f / std::max(1.f, m_ropePip->getContentSize().width));
    m_ropePip->setColor(kOwnColor);
    m_ropeRow->addChild(m_ropePip, 2);
}

void VersusHUDNode::refresh() {
    auto const& session = VersusSession::get();
    auto const& own = session.own();
    auto const& rival = session.rival();

    // blackout takes both bars away; the eye is what buys the rival's back.
    auto const& effects = VersusEffects::get();
    bool const blackout = effects.barsHidden();
    if (m_ownRow) m_ownRow->setVisible(!blackout);
    if (m_rivalRow) m_rivalRow->setVisible(!blackout || effects.seesRival());

    if (m_ownFill) m_ownFill->setPercentage(std::clamp(own.percent, 0.f, 100.f));
    if (m_rivalFill) m_rivalFill->setPercentage(std::clamp(rival.percent, 0.f, 100.f));

    if (m_ownLabel) m_ownLabel->setString(fmt::format("{}%", static_cast<int>(own.percent)).c_str());
    if (m_rivalLabel) {
        m_rivalLabel->setString(fmt::format("{}%", static_cast<int>(rival.percent)).c_str());
        // dim the rival's bar while they are dead or practising, so the lead
        // reads at a glance instead of looking like they simply stopped.
        GLubyte const opacity = rival.alive && !rival.practice ? 255 : 120;
        m_rivalLabel->setOpacity(opacity);
        if (m_rivalFill) m_rivalFill->setOpacity(opacity);
    }

    if (m_rivalName) {
        auto const& name = session.match().rival.name;
        if (name != m_rivalName->getString()) m_rivalName->setString(name.c_str());
    }

    bool const tug = session.match().format == Format::TugOfWar;
    if (m_ropeRow) m_ropeRow->setVisible(tug && !blackout);
    if (tug && m_ropePip) {
        m_ropePip->setPositionX(std::clamp(session.rope(), -1.f, 1.f) * kRopeWidth / 2.f);
    }

    float const left = session.timeLeft();
    if (m_clock) {
        m_clock->setVisible(left > 0.f && !blackout);
        int const seconds = left > 0.f ? static_cast<int>(std::ceil(left)) : -1;
        if (seconds != m_shownSeconds) {
            m_shownSeconds = seconds;
            if (seconds >= 0) {
                m_clock->setString(fmt::format("{}:{:02}", seconds / 60, seconds % 60).c_str());
                m_clock->setColor(seconds <= 15 ? ccColor3B{240, 130, 140} : ccColor3B{225, 230, 240});
            }
        }
    }
}

void VersusHUDNode::update(float dt) {
    auto const& session = VersusSession::get();
    if (session.countingDown() && m_countdown) {
        int const left = static_cast<int>(std::ceil(session.countdownLeft()));
        m_countdown->setString(fmt::format("{}", std::max(1, left)).c_str());
    }

    m_pingTimer += dt;
    if (m_pingTimer < 1.f) return;
    m_pingTimer = 0.f;

    if (!m_ping) return;
    auto const ping = gl::pingMs();
    if (ping == 0) {
        m_ping->setString(Localization::get().getString("versus.hud.offline").c_str());
        m_ping->setColor({240, 160, 90});
        return;
    }
    m_ping->setString(fmt::format("{} ms", ping).c_str());
    m_ping->setColor(ping < 120 ? ccColor3B{140, 220, 160}
                                : (ping < 250 ? ccColor3B{240, 220, 140} : ccColor3B{240, 140, 140}));
}

void VersusHUDNode::playCountdown(float seconds) {
    if (!m_countdown) return;

    m_countdown->stopAllActions();
    m_countdown->setVisible(true);
    m_countdown->setOpacity(255);
    m_countdown->setColor({255, 226, 140});
    m_countdown->runAction(CCSequence::create(
        CCDelayTime::create(seconds),
        CCFadeOut::create(0.25f),
        CCHide::create(),
        nullptr));
}

void VersusHUDNode::showResult(Outcome outcome) {
    if (!m_countdown) return;

    auto& loc = Localization::get();
    m_countdown->setVisible(true);
    m_countdown->setOpacity(255);
    m_countdown->setScale(1.6f);

    switch (outcome) {
        case Outcome::Win:
            m_countdown->setString(loc.getString("versus.result.win").c_str());
            m_countdown->setColor({140, 230, 160});
            break;
        case Outcome::Loss:
            m_countdown->setString(loc.getString("versus.result.loss").c_str());
            m_countdown->setColor({240, 130, 140});
            break;
        default:
            m_countdown->setString(loc.getString("versus.result.draw").c_str());
            m_countdown->setColor({230, 220, 160});
            break;
    }

    m_countdown->setScale(0.4f);
    m_countdown->runAction(CCEaseBackOut::create(CCScaleTo::create(0.4f, 1.6f)));
}

} // namespace paimon::versus
