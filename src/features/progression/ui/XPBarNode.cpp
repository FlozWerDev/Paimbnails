#include "XPBarNode.hpp"
#include "GDProgressBar.hpp"
#include "../data/ProgressionStats.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

int64_t lerpExp(int64_t from, int64_t to, float t) {
    return from + static_cast<int64_t>(std::llround((to - from) * static_cast<double>(t)));
}

} // namespace

XPBarNode* XPBarNode::create(float width, float height) {
    auto ret = new XPBarNode();
    if (ret && ret->init(width, height)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool XPBarNode::init(float width, float height) {
    if (!CCNode::init()) return false;

    m_width = width;
    m_height = height;
    this->setContentSize({width, height});
    this->setAnchorPoint({0.5f, 0.5f});

    m_bar = GDProgressBar::create(width, height);
    if (m_bar) {
        m_bar->setPosition({width / 2.f, height / 2.f});
        this->addChild(m_bar, 0);
    }

    m_label = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_label) {
        m_label->setScale(std::clamp(height * 0.028f, 0.30f, 0.42f));
        m_label->setPosition({width / 2.f, height / 2.f});
        m_label->setColor({255, 255, 255});
        this->addChild(m_label, 2);
    }

    this->scheduleUpdate();
    return true;
}

void XPBarNode::setTier(Tier const& tier) {
    if (m_bar) m_bar->setFillColor(tier.base);
}

void XPBarNode::setLevelUpCallback(std::function<void(int)> callback) {
    m_onLevelUp = std::move(callback);
}

void XPBarNode::setExp(int64_t exp) {
    m_queue.clear();
    m_segment = 0;
    m_segmentTime = 0.f;
    m_exp = exp;
    applyExp(exp, levelForExp(exp));
}

void XPBarNode::animateTo(int64_t exp, float duration) {
    if (duration <= 0.f || exp <= m_exp) {
        setExp(exp);
        return;
    }

    int64_t const from = m_exp;
    m_queue.clear();
    m_segment = 0;
    m_segmentTime = 0.f;

    // One segment per level crossed, weighted by how much of the gain falls in
    // each so a long grind doesn't spend the whole animation on one level.
    int64_t cursor = from;
    int64_t const span = exp - from;
    while (cursor < exp) {
        int const level = levelForExp(cursor);
        int64_t const boundary = expForLevel(level + 1);
        int64_t const stop = (level >= kMaxLevel) ? exp : std::min(exp, boundary);
        float const share = span > 0 ? static_cast<float>(stop - cursor) / static_cast<float>(span) : 1.f;
        m_queue.push_back({cursor, stop, std::max(0.18f, duration * share)});
        cursor = stop;
        if (m_queue.size() > 12) break;
    }
    if (m_queue.empty()) setExp(exp);
}

void XPBarNode::applyExp(int64_t exp, int level) {
    int64_t const base = expForLevel(level);
    int64_t const span = expSpanOfLevel(level);
    float const progress = span > 0
        ? std::clamp(static_cast<float>(exp - base) / static_cast<float>(span), 0.f, 1.f)
        : 1.f;

    if (m_bar) m_bar->setProgress(progress);

    if (m_label) {
        m_label->setString(fmt::format(
            "{} / {} XP",
            formatCount(exp - base),
            span > 0 ? formatCount(span) : std::string("MAX")
        ).c_str());
    }
}

void XPBarNode::update(float dt) {
    if (m_segment >= m_queue.size()) return;

    auto const& segment = m_queue[m_segment];
    m_segmentTime = std::min(m_segmentTime + dt, segment.duration);
    float const t = segment.duration > 0.f ? m_segmentTime / segment.duration : 1.f;
    float const eased = 1.f - std::pow(1.f - t, 3.f);

    int const level = levelForExp(segment.from);
    int64_t const current = lerpExp(segment.from, segment.to, eased);
    applyExp(current, level);
    m_exp = current;

    if (m_segmentTime < segment.duration) return;

    ++m_segment;
    m_segmentTime = 0.f;
    m_exp = segment.to;

    int const reached = levelForExp(segment.to);
    if (reached > level) {
        flashLevelUp();
        if (m_onLevelUp) m_onLevelUp(reached);
    }
    if (m_segment >= m_queue.size()) applyExp(m_exp, reached);
}

void XPBarNode::flashLevelUp() {
    auto* flash = GDProgressBar::makeCapsule();
    if (!flash) return;

    flash->setContentSize({m_width, m_height});
    flash->setPosition({m_width / 2.f, m_height / 2.f});
    flash->runAction(CCSequence::create(
        CCFadeTo::create(0.35f, 0),
        CCRemoveSelf::create(),
        nullptr
    ));
    this->addChild(flash, 3);
}

} // namespace paimon::progression
