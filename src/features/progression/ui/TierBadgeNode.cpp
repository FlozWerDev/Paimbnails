#include "TierBadgeNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

constexpr float kPi = 3.14159265358979323846f;

ccColor3B mix(ccColor3B const& a, ccColor3B const& b, float t) {
    return {
        static_cast<GLubyte>(a.r + (b.r - a.r) * t),
        static_cast<GLubyte>(a.g + (b.g - a.g) * t),
        static_cast<GLubyte>(a.b + (b.b - a.b) * t),
    };
}

char const* plateFile(TierFrame frame) {
    switch (frame) {
        case TierFrame::Pill:    return "paim_progTierRound.png"_spr;
        case TierFrame::Shield:  return "paim_progTierShield.png"_spr;
        case TierFrame::Hexagon: return "paim_progTierHex.png"_spr;
        case TierFrame::Star:    return "paim_progTierStar.png"_spr;
        case TierFrame::Crown:   return "paim_progTierCrown.png"_spr;
    }
    return "paim_progTierRound.png"_spr;
}

// The plates are drawn on a square canvas, so one factor fits every shape.
void fitSquare(CCSprite* sprite, float size) {
    float const source = std::max(sprite->getContentSize().width, sprite->getContentSize().height);
    sprite->setScale(size / std::max(1.f, source));
}

} // namespace

CCSprite* makeRadialGlow(ccColor3B color, float radius, float peakAlpha) {
    auto* glow = paimon::SpriteHelper::safeCreate("paim_progGlow.png"_spr);
    if (!glow) return nullptr;

    fitSquare(glow, radius * 2.f);
    glow->setColor(color);
    glow->setOpacity(static_cast<GLubyte>(std::clamp(peakAlpha, 0.f, 1.f) * 255.f));
    glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    return glow;
}

CCSprite* makeTierPlate(TierFrame frame) {
    return paimon::SpriteHelper::safeCreate(plateFile(frame));
}

TierBadgeNode* TierBadgeNode::create(int level, float size) {
    auto ret = new TierBadgeNode();
    if (ret && ret->init(level, size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TierBadgeNode::init(int level, float size) {
    if (!CCNode::init()) return false;

    m_level = level;
    m_size = size;
    this->setContentSize({size, size});
    this->setAnchorPoint({0.5f, 0.5f});

    m_content = CCNode::create();
    m_content->setPosition({size / 2.f, size / 2.f});
    this->addChild(m_content);

    rebuild();
    return true;
}

void TierBadgeNode::setLevel(int level) {
    if (level == m_level) return;
    m_level = level;
    rebuild();
}

void TierBadgeNode::setProgress(float progress) {
    m_progress = progress;
    redrawRing(tierForLevel(m_level));
}

void TierBadgeNode::rebuild() {
    m_content->stopAllActions();
    m_content->removeAllChildren();
    m_ring = nullptr;
    m_ringFill = nullptr;
    m_levelLabel = nullptr;

    auto const& tier = tierForLevel(m_level);
    buildFrame(tier);
    buildEffects(tier);
    redrawRing(tier);
}

void TierBadgeNode::buildFrame(Tier const& tier) {
    if (tier.effects & TierEffectGlow) {
        if (auto* glow = makeRadialGlow(tier.accent, m_size * 0.95f, 0.5f)) {
            glow->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCScaleTo::create(1.6f, glow->getScale() * 1.12f)),
                CCEaseSineInOut::create(CCScaleTo::create(1.6f, glow->getScale())),
                nullptr
            )));
            m_content->addChild(glow, -2);
        }
    }

    // The plate is drawn light over black, so the accent lands on the body and
    // the recessed face keeps the number readable whatever the tier colour is.
    if (auto* plate = makeTierPlate(tier.frame)) {
        fitSquare(plate, m_size);
        plate->setColor(tier.accent);
        plate->setID("tier-plate"_spr);
        m_content->addChild(plate, 0);
    }

    // At chip size the number is unreadable and the chip prints it anyway.
    if (m_size < 26.f) return;

    m_levelLabel = CCLabelBMFont::create(std::to_string(m_level).c_str(), "bigFont.fnt");
    if (m_levelLabel) {
        // The crown recesses a band across its base instead of a middle, so the
        // number sits lower and smaller there than on the other plates.
        bool const crown = tier.frame == TierFrame::Crown;
        float const target = m_size * (crown ? 0.20f : (m_level >= 100 ? 0.30f : 0.38f));
        m_levelLabel->limitLabelWidth(
            m_size * (crown ? 0.58f : 0.52f),
            target / std::max(1.f, m_levelLabel->getContentSize().height), 0.05f);
        m_levelLabel->setPosition({0.f, crown ? -m_size * 0.24f : 0.f});
        m_levelLabel->setColor(mix(tier.accent, {255, 255, 255}, 0.72f));
        m_content->addChild(m_levelLabel, 3);
    }
}

void TierBadgeNode::buildEffects(Tier const& tier) {
    float const r = m_size * 0.5f;

    // A white copy of the plate flashing on and off, which reads like the shine
    // sweeping over the medal without needing a clipped light bar.
    if (tier.effects & TierEffectSweep) {
        if (auto* shine = makeTierPlate(tier.frame)) {
            fitSquare(shine, m_size);
            shine->setOpacity(0);
            shine->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            shine->runAction(CCRepeatForever::create(CCSequence::create(
                CCDelayTime::create(2.4f),
                CCEaseSineOut::create(CCFadeTo::create(0.35f, 110)),
                CCEaseSineIn::create(CCFadeTo::create(0.5f, 0)),
                nullptr
            )));
            m_content->addChild(shine, 2);
        }
    }

    m_pulses = (tier.effects & TierEffectPulse) != 0;
    if (m_pulses) startPulse();

    if (tier.effects & TierEffectSparks) {
        for (int i = 0; i < 5; ++i) {
            auto* spark = paimon::SpriteHelper::safeCreate("paim_progSpark.png"_spr);
            if (!spark) break;
            float const a = kPi * 0.5f + static_cast<float>(i) * kPi * 0.4f;
            spark->setPosition({std::cos(a) * r * 0.94f, std::sin(a) * r * 0.94f});
            fitSquare(spark, m_size * 0.24f);
            spark->setColor(tier.accent);
            spark->setOpacity(0);
            spark->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            float const peak = spark->getScale();
            spark->setScale(peak * 0.3f);
            spark->runAction(CCRepeatForever::create(CCSequence::create(
                CCDelayTime::create(0.28f * i),
                CCSpawn::create(
                    CCFadeTo::create(0.30f, 230),
                    CCScaleTo::create(0.30f, peak),
                    nullptr
                ),
                CCSpawn::create(
                    CCFadeTo::create(0.42f, 0),
                    CCScaleTo::create(0.42f, peak * 0.3f),
                    nullptr
                ),
                CCDelayTime::create(1.5f - 0.28f * i),
                nullptr
            )));
            m_content->addChild(spark, 4);
        }
    }

    if (tier.effects & TierEffectOrbit) {
        auto* orbit = CCNode::create();
        for (int i = 0; i < 2; ++i) {
            auto* dot = paimon::SpriteHelper::safeCreate("paim_progSpark.png"_spr);
            if (!dot) break;
            dot->setPosition({i == 0 ? r * 1.30f : -r * 1.30f, 0.f});
            fitSquare(dot, m_size * 0.30f);
            dot->setColor(mix(tier.accent, {255, 255, 255}, 0.4f));
            dot->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            orbit->addChild(dot);
        }
        orbit->setScaleY(0.42f);
        orbit->runAction(CCRepeatForever::create(CCRotateBy::create(4.2f, -360.f)));
        m_content->addChild(orbit, 5);
    }
}

void TierBadgeNode::redrawRing(Tier const& tier) {
    if (m_progress < 0.f) {
        if (m_ring) {
            m_ring->removeFromParent();
            m_ring = nullptr;
            m_ringFill = nullptr;
        }
        return;
    }

    float const ringSize = m_size * 1.24f;

    if (!m_ring) {
        m_ring = CCNode::create();
        m_content->addChild(m_ring, 1);

        if (auto* track = paimon::SpriteHelper::safeCreate("paim_progRing.png"_spr)) {
            fitSquare(track, ringSize);
            track->setColor({0, 0, 0});
            track->setOpacity(120);
            m_ring->addChild(track, 0);
        }

        if (auto* filled = paimon::SpriteHelper::safeCreate("paim_progRing.png"_spr)) {
            m_ringFill = CCProgressTimer::create(filled);
            if (m_ringFill) {
                m_ringFill->setType(kCCProgressTimerTypeRadial);
                m_ringFill->setMidpoint({0.5f, 0.5f});
                m_ringFill->setScale(ringSize / std::max(1.f, filled->getContentSize().width));
                m_ring->addChild(m_ringFill, 1);
            }
        }
    }

    if (m_ringFill) {
        m_ringFill->setColor(tier.accent);
        m_ringFill->setPercentage(std::clamp(m_progress, 0.f, 1.f) * 100.f);
    }
}

void TierBadgeNode::startPulse() {
    if (!m_pulses) return;
    m_content->runAction(CCRepeatForever::create(CCSequence::create(
        CCEaseSineInOut::create(CCScaleTo::create(1.4f, 1.045f)),
        CCEaseSineInOut::create(CCScaleTo::create(1.4f, 1.f)),
        nullptr
    )));
}

void TierBadgeNode::playIntro(float delay) {
    m_content->stopAllActions();
    m_content->setScale(0.f);
    m_content->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCEaseBackOut::create(CCScaleTo::create(0.42f, 1.f)),
        CCCallFunc::create(this, callfunc_selector(TierBadgeNode::startPulse)),
        nullptr
    ));
}

void TierBadgeNode::playLevelUp() {
    auto const& tier = tierForLevel(m_level);

    m_content->stopAllActions();
    m_content->setScale(0.25f);
    m_content->runAction(CCSequence::create(
        CCEaseElasticOut::create(CCScaleTo::create(0.9f, 1.f), 0.55f),
        CCCallFunc::create(this, callfunc_selector(TierBadgeNode::startPulse)),
        nullptr
    ));

    if (auto* flash = makeTierPlate(tier.frame)) {
        fitSquare(flash, m_size);
        flash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        flash->runAction(CCSequence::create(
            CCDelayTime::create(0.12f),
            CCFadeTo::create(0.45f, 0),
            CCRemoveSelf::create(),
            nullptr
        ));
        m_content->addChild(flash, 6);
    }

    if (auto* wave = makeTierPlate(tier.frame)) {
        fitSquare(wave, m_size);
        wave->setColor(tier.accent);
        wave->setOpacity(140);
        wave->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        float const from = wave->getScale();
        wave->runAction(CCSpawn::create(
            CCEaseSineOut::create(CCScaleTo::create(0.7f, from * 2.4f)),
            CCFadeTo::create(0.7f, 0),
            nullptr
        ));
        wave->runAction(CCSequence::create(
            CCDelayTime::create(0.75f),
            CCRemoveSelf::create(),
            nullptr
        ));
        m_content->addChild(wave, -3);
    }
}

} // namespace paimon::progression
