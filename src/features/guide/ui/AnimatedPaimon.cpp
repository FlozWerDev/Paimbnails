#include "AnimatedPaimon.hpp"

#include <Geode/Geode.hpp>
#include <cmath>
#include <random>

using namespace geode::prelude;

namespace paimon::guide {

namespace {

// Shared lightweight RNG for jittering blinks/bubbles; lazy-initialized.
std::mt19937& rng() {
    static std::mt19937 instance{ std::random_device{}() };
    return instance;
}

float randRange(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng());
}

} // namespace

AnimatedPaimon* AnimatedPaimon::create(float spriteScale) {
    auto ret = new AnimatedPaimon();
    if (ret && ret->init(spriteScale)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AnimatedPaimon::init(float spriteScale) {
    if (!CCNode::init()) return false;

    m_sprite = CCSprite::create("paim_Paimon.png"_spr);
    if (!m_sprite) {
    return true;
    }

    m_sprite->setScale(spriteScale);
    m_sprite->setAnchorPoint({0.5f, 0.5f});
    auto sz = m_sprite->getContentSize() * spriteScale;
    this->setContentSize(sz);
    m_sprite->setPosition({sz.width * 0.5f, sz.height * 0.5f});
    this->addChild(m_sprite);

    m_sprite->setID("animated-paimon-sprite"_spr);
    this->setID("animated-paimon"_spr);

    return true;
}

void AnimatedPaimon::onExit() {
    this->unschedule(schedule_selector(AnimatedPaimon::onBlinkTimer));
    CCNode::onExit();
}

void AnimatedPaimon::setLively(bool lively) {
    m_lively = lively;
    if (!m_sprite) return;

    m_sprite->stopActionByTag(kIdleTag);
    m_sprite->stopActionByTag(kBlinkTag);
    this->unschedule(schedule_selector(AnimatedPaimon::onBlinkTimer));

    if (lively) {
        startIdleLoop();
        scheduleNextBlink();
        m_sprite->setOpacity(255);
    } else {
        m_sprite->setOpacity(180);
        m_sprite->setRotation(0.f);
    }
}

void AnimatedPaimon::startIdleLoop() {
    if (!m_sprite) return;

    auto up = CCEaseSineInOut::create(CCMoveBy::create(1.5f, {0.f, 4.f}));
    auto down = CCEaseSineInOut::create(CCMoveBy::create(1.5f, {0.f, -4.f}));
    auto seq = CCSequence::create(up, down, nullptr);
    auto loop = CCRepeatForever::create(seq);
    loop->setTag(kIdleTag);
    m_sprite->runAction(loop);
}

void AnimatedPaimon::scheduleNextBlink() {
    if (!m_lively) return;
    float delay = randRange(3.f, 7.f);
    this->scheduleOnce(schedule_selector(AnimatedPaimon::onBlinkTimer), delay);
}

void AnimatedPaimon::onBlinkTimer(float /*dt*/) {
    if (!m_lively || !m_sprite) return;

    float scaleX = m_sprite->getScaleX();
    float scaleY = m_sprite->getScaleY();

    auto close = CCScaleTo::create(0.06f, scaleX, scaleY * 0.1f);
    auto open  = CCScaleTo::create(0.06f, scaleX, scaleY);
    auto blink = CCSequence::create(close, open, nullptr);
    blink->setTag(kBlinkTag);

    m_sprite->stopActionByTag(kBlinkTag);
    m_sprite->runAction(blink);

    scheduleNextBlink();
}

void AnimatedPaimon::play(Animation anim) {
    if (!m_sprite) return;
    m_currentState = anim;

    switch (anim) {
        case Animation::Idle: {
            m_sprite->stopActionByTag(kStateTag);
            auto resetRot = CCEaseBackOut::create(CCRotateTo::create(0.2f, 0.f));
            resetRot->setTag(kStateTag);
            m_sprite->runAction(resetRot);
            break;
        }

        case Animation::Blink: {
            onBlinkTimer(0.f);
            break;
        }

        case Animation::Talk: {
            m_sprite->stopActionByTag(kStateTag);
            auto a = CCEaseSineInOut::create(CCRotateBy::create(0.10f, 4.f));
            auto b = CCEaseSineInOut::create(CCRotateBy::create(0.10f, -8.f));
            auto c = CCEaseSineInOut::create(CCRotateBy::create(0.10f, 4.f));
            auto unit = CCSequence::create(a, b, c, nullptr);
            auto repeat = CCRepeat::create(unit, 2);
            auto reset  = CCEaseBackOut::create(CCRotateTo::create(0.15f, 0.f));
            auto talk   = CCSequence::create(repeat, reset, nullptr);
            talk->setTag(kStateTag);
            m_sprite->runAction(talk);
            break;
        }

        case Animation::Surprise: {
            m_sprite->stopActionByTag(kStateTag);
            float baseScale = m_sprite->getScaleX();
            auto pop = CCSequence::create(
                CCEaseBackOut::create(CCScaleTo::create(0.12f, baseScale * 1.30f)),
                CCEaseBackIn::create(CCScaleTo::create(0.18f, baseScale)),
                nullptr
            );
            pop->setTag(kStateTag);
            m_sprite->runAction(pop);
            break;
        }

        case Animation::Wave: {
            m_sprite->stopActionByTag(kStateTag);
            auto s1 = CCEaseSineInOut::create(CCRotateTo::create(0.18f, -10.f));
            auto s2 = CCEaseSineInOut::create(CCRotateTo::create(0.18f,  10.f));
            auto s3 = CCEaseSineInOut::create(CCRotateTo::create(0.18f, -10.f));
            auto s4 = CCEaseBackOut::create(CCRotateTo::create(0.18f,   0.f));
            auto wave = CCSequence::create(s1, s2, s3, s4, nullptr);
            wave->setTag(kStateTag);
            m_sprite->runAction(wave);
            break;
        }

        case Animation::Point: {
            m_sprite->stopActionByTag(kStateTag);
            auto tilt = CCEaseElasticOut::create(
                CCRotateTo::create(0.4f, 12.f), 0.5f
            );
            tilt->setTag(kStateTag);
            m_sprite->runAction(tilt);
            break;
        }

        case Animation::Sleep: {
            m_sprite->stopActionByTag(kStateTag);
            auto dim = CCFadeTo::create(0.4f, 130);
            auto fall = CCEaseSineInOut::create(CCRotateTo::create(0.4f, -8.f));
            auto seq = CCSpawn::create(dim, fall, nullptr);
            seq->setTag(kStateTag);
            m_sprite->runAction(seq);
            break;
        }
    }
}

void AnimatedPaimon::pointAt(cocos2d::CCNode* target, float duration) {
    if (!m_sprite) return;

    if (!target) {
        m_sprite->stopActionByTag(kStateTag);
        auto rot = CCEaseBackOut::create(CCRotateTo::create(duration, 0.f));
        rot->setTag(kStateTag);
        m_sprite->runAction(rot);
        return;
    }

    // Convert positions to world space and compute the angle.
    auto myWorld = this->convertToWorldSpace(this->getContentSize() * 0.5f);
    auto targetWorld = target->convertToWorldSpace(
        target->getContentSize() * 0.5f
    );
    auto delta = targetWorld - myWorld;

    // Cocos rotates clockwise; compute atan2 and apply a -90 offset so "0 degrees" points up.
    float angleRad = std::atan2(delta.y, delta.x);
    float angleDeg = -CC_RADIANS_TO_DEGREES(angleRad);
    // Clamp the angle to +-25 so it doesn't look forced.
    if (angleDeg > 25.f) angleDeg = 25.f;
    if (angleDeg < -25.f) angleDeg = -25.f;

    m_sprite->stopActionByTag(kStateTag);
    auto rot = CCEaseElasticOut::create(
        CCRotateTo::create(duration, angleDeg), 0.5f
    );
    rot->setTag(kStateTag);
    m_sprite->runAction(rot);
    m_currentState = Animation::Point;
}

void AnimatedPaimon::showBubble(std::string const& text, float duration) {
    hideBubble();
    if (text.empty()) return;

    // Background: small dark dialog panel.
    auto bg = CCScale9Sprite::create("GJ_square01.png");
    bg->setColor({40, 50, 70});
    bg->setOpacity(220);

    auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->setScale(0.45f);
    label->setColor({255, 255, 255});

    float padX = 10.f;
    float padY = 6.f;
    auto labelSz = label->getScaledContentSize();
    bg->setContentSize({labelSz.width + padX * 2, labelSz.height + padY * 2});
    label->setPosition(bg->getContentSize() * 0.5f);
    bg->addChild(label);

    auto holder = CCNode::create();
    holder->setContentSize(bg->getContentSize());
    bg->setPosition(holder->getContentSize() * 0.5f);
    holder->addChild(bg);

    // Position: top-right of the sprite.
    auto sz = this->getContentSize();
    holder->setAnchorPoint({0.f, 0.f});
    holder->setPosition({sz.width * 0.6f, sz.height + 4.f});
    holder->setScale(0.f);
    holder->setID("paimon-bubble"_spr);

    // Appear (back-out) and exit (fade + scale) animation.
    auto popIn = CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f));
    auto stay  = CCDelayTime::create(duration);
    auto popOut = CCSpawn::create(
        CCFadeOut::create(0.25f),
        CCScaleTo::create(0.25f, 0.6f),
        nullptr
    );
    auto remove = CCRemoveSelf::create();
    holder->runAction(CCSequence::create(popIn, stay, popOut, remove, nullptr));

    this->addChild(holder, 5);
    m_bubble = holder;
    m_bubbleText = label;
}

void AnimatedPaimon::hideBubble() {
    // m_bubble is a WeakRef: lock() is null if the node was already removed, avoiding a dangling pointer.
    if (auto bubble = m_bubble.lock()) {
        bubble->removeFromParent();
    }
    m_bubble = nullptr;
    m_bubbleText = nullptr;
}

} // namespace paimon::guide
