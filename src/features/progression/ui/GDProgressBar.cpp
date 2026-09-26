#include "GDProgressBar.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;
using namespace cocos2d::extension;

namespace paimon::progression {

namespace {

constexpr char const* kBarFile = "GJ_progressBar_001.png";
constexpr float kBarW = 340.f;
constexpr float kBarH = 20.f;
constexpr float kInset = 2.5f;

} // namespace

// 340x20 capsule, 10px caps: stretchable middle is everything but caps.
CCScale9Sprite* GDProgressBar::makeCapsule() {
    if (!CCTextureCache::sharedTextureCache()->addImage(kBarFile, false)) return nullptr;
    return CCScale9Sprite::create(
        kBarFile, CCRectMake(0.f, 0.f, kBarW, kBarH), CCRectMake(12.f, 6.f, kBarW - 24.f, 8.f));
}

GDProgressBar* GDProgressBar::create(float width, float height) {
    auto ret = new GDProgressBar();
    if (ret && ret->init(width, height)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GDProgressBar::init(float width, float height) {
    if (!CCNode::init()) return false;

    m_width = width;
    // Below natural height squash caps instead of resizing them.
    m_artHeight = std::max(height, kBarH);
    m_squash = height / m_artHeight;

    this->setContentSize({width, height});
    this->setAnchorPoint({0.5f, 0.5f});

    if (auto* groove = makeCapsule()) {
        groove->setContentSize({width / m_squash, m_artHeight});
        groove->setAnchorPoint({0.f, 0.5f});
        groove->setScale(m_squash);
        groove->setPosition({0.f, height / 2.f});
        groove->setColor({0, 0, 0});
        groove->setOpacity(130);
        this->addChild(groove, 0);
    }

    m_fill = makeCapsule();
    if (m_fill) {
        m_fill->setAnchorPoint({0.f, 0.5f});
        m_fill->setScale(m_squash);
        m_fill->setPosition({kInset * m_squash, height / 2.f});
        m_fill->setVisible(false);
        this->addChild(m_fill, 1);
    }

    this->scheduleUpdate();
    return true;
}

void GDProgressBar::setFillColor(ccColor3B color) {
    if (m_fill) m_fill->setColor(color);
}

void GDProgressBar::setText(std::string const& text, float scale, ccColor3B color) {
    if (!m_label) {
        m_label = CCLabelBMFont::create("", "chatFont.fnt");
        if (!m_label) return;
        auto const size = this->getContentSize();
        m_label->setPosition({size.width / 2.f, size.height / 2.f});
        this->addChild(m_label, 2);
    }
    m_label->setString(text.c_str());
    m_label->setScale(scale);
    m_label->setColor(color);
}

void GDProgressBar::setProgress(float progress) {
    m_duration = 0.f;
    m_delay = 0.f;
    m_target = progress;
    applyProgress(progress);
}

void GDProgressBar::animateTo(float progress, float delay, float duration) {
    if (duration <= 0.f) {
        setProgress(progress);
        return;
    }
    m_from = m_target;
    m_target = progress;
    m_delay = delay;
    m_duration = duration;
    m_elapsed = 0.f;
    applyProgress(m_from);
}

void GDProgressBar::applyProgress(float progress) {
    if (!m_fill) return;

    float const clamped = std::clamp(progress, 0.f, 1.f);
    float const track = m_width / m_squash - kInset * 2.f;
    float const inner = m_artHeight - kInset * 2.f;

    if (clamped <= 0.001f) {
        m_fill->setVisible(false);
        return;
    }
    m_fill->setVisible(true);
    m_fill->setContentSize({std::max(inner, track * clamped), inner});
}

void GDProgressBar::update(float dt) {
    if (m_duration <= 0.f) return;

    if (m_delay > 0.f) {
        m_delay -= dt;
        return;
    }

    m_elapsed = std::min(m_elapsed + dt, m_duration);
    float const t = m_elapsed / m_duration;
    applyProgress(m_from + (m_target - m_from) * (1.f - std::pow(1.f - t, 3.f)));

    if (m_elapsed >= m_duration) m_duration = 0.f;
}

} // namespace paimon::progression
