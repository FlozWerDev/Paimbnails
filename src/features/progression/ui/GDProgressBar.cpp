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

    m_width = std::max(12.f, width);
    m_height = std::max(4.f, height);

    this->setContentSize({m_width, m_height});
    this->setAnchorPoint({0.5f, 0.5f});

    // Dark recessed interior behind the native GD slider groove
    if (auto* grooveBg = makeCapsule()) {
        float const bgScale = m_height / kBarH;
        float const tPad = std::max(1.f, m_height * 0.14f);
        float const innerW = std::max(4.f, m_width - tPad * 2.f);
        float const innerScale = std::max(0.1f, (m_height - tPad * 2.f) / kBarH);
        (void)bgScale;
        grooveBg->setContentSize({innerW / innerScale, kBarH});
        grooveBg->setAnchorPoint({0.f, 0.5f});
        grooveBg->setScale(innerScale);
        grooveBg->setPosition({tPad, m_height / 2.f});
        grooveBg->setColor({0, 0, 0});
        grooveBg->setOpacity(125);
        this->addChild(grooveBg, 0);
    }

    m_pad = std::max(1.2f, m_height * 0.17f);

    // Native GD fill strip (sliderBar2.png with GL_REPEAT, fallback sliderBar.png)
    char const* fillFile = "sliderBar2.png";
    m_fillSprite = CCSprite::create(fillFile);
    if (!m_fillSprite || !m_fillSprite->getTexture()) {
        fillFile = "sliderBar.png";
        m_fillSprite = CCSprite::create(fillFile);
    }
    if (m_fillSprite && m_fillSprite->getTexture()) {
        auto* tex = m_fillSprite->getTexture();
        auto const w = tex->getPixelsWide();
        auto const h = tex->getPixelsHigh();
        bool const isPot = (w > 0 && (w & (w - 1)) == 0 && h > 0 && (h & (h - 1)) == 0);
        GLuint const wrapS = isPot ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        ccTexParams tp{GL_LINEAR, GL_LINEAR, wrapS, GL_CLAMP_TO_EDGE};
        tex->setTexParameters(&tp);
        m_fillSprite->setAnchorPoint({0.f, 0.5f});
        m_fillSprite->setPosition({m_pad, m_height / 2.f});
        m_fillSprite->setVisible(false);
        this->addChild(m_fillSprite, 1);
    } else {
        m_fillSprite = nullptr;
        // Fallback capsule fill if sliderBar sprites are unavailable
        m_artHeight = std::max(m_height, kBarH);
        m_squash = m_height / m_artHeight;
        m_fill = makeCapsule();
        if (m_fill) {
            m_fill->setAnchorPoint({0.f, 0.5f});
            m_fill->setScale(m_squash);
            m_fill->setPosition({kInset * m_squash, m_height / 2.f});
            m_fill->setVisible(false);
            this->addChild(m_fill, 1);
        }
    }

    // Native GD hollow capsule frame (slidergroove2.png / slidergroove_02.png) on top of fill
    bool usingGroove2 = true;
    char const* grooveFile = "slidergroove2.png";
    auto* grooveTex = CCTextureCache::sharedTextureCache()->addImage(grooveFile, false);
    if (!grooveTex) {
        usingGroove2 = false;
        grooveFile = "slidergroove_02.png";
        grooveTex = CCTextureCache::sharedTextureCache()->addImage(grooveFile, false);
    }
    if (grooveTex) {
        CCSize const sz = grooveTex->getContentSize();
        if (sz.width > 4.f && sz.height > 4.f) {
            float const visualH = usingGroove2 ? (sz.height * 0.75f) : sz.height;
            float const capX = std::min(10.f, sz.width * 0.25f);
            float const capY = std::min(4.f, sz.height * 0.30f);
            m_frame = CCScale9Sprite::create(
                grooveFile,
                CCRectMake(0.f, 0.f, sz.width, sz.height),
                CCRectMake(capX, capY, std::max(1.f, sz.width - capX * 2.f), std::max(1.f, sz.height - capY * 2.f))
            );
            if (m_frame) {
                float const scaleY = std::max(0.2f, m_height / std::max(1.f, visualH));
                float const contentW = std::max(capX * 2.f + 2.f, m_width / scaleY);
                m_frame->setContentSize({contentW, sz.height});
                m_frame->setScaleX(m_width / contentW);
                m_frame->setScaleY(scaleY);
                m_frame->setAnchorPoint({0.5f, 0.5f});
                m_frame->setPosition({m_width / 2.f, m_height / 2.f});
                this->addChild(m_frame, 2);
            }
        }
    }

    this->scheduleUpdate();
    return true;
}

void GDProgressBar::setFillColor(ccColor3B color) {
    if (m_fillSprite) m_fillSprite->setColor(color);
    if (m_fill) m_fill->setColor(color);
}

void GDProgressBar::setText(std::string const& text, float scale, ccColor3B color) {
    if (!m_label) {
        m_label = CCLabelBMFont::create("", "bigFont.fnt");
        if (!m_label) return;
        auto const size = this->getContentSize();
        m_label->setPosition({size.width / 2.f, size.height / 2.f});
        this->addChild(m_label, 3);
    }
    m_label->setString(text.c_str());
    m_label->limitLabelWidth(std::max(10.f, m_width - 14.f), scale, 0.16f);
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
    float const clamped = std::clamp(progress, 0.f, 1.f);

    if (m_fillSprite && m_fillSprite->getTexture()) {
        if (clamped <= 0.001f) {
            m_fillSprite->setVisible(false);
            return;
        }
        float const innerW = std::max(1.f, m_width - m_pad * 2.f);
        float const innerH = std::max(1.f, m_height - m_pad * 2.f);
        float const fillW = innerW * clamped;
        float const texH = std::max(1.f, m_fillSprite->getTexture()->getContentSize().height);
        float const sy = std::max(0.05f, innerH / texH);
        m_fillSprite->setTextureRect(CCRectMake(0.f, 0.f, std::max(1.f, fillW / sy), texH));
        m_fillSprite->setScale(sy);
        m_fillSprite->setVisible(true);
        return;
    }

    if (!m_fill) return;
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
