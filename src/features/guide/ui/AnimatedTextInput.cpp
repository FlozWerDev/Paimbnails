#include "AnimatedTextInput.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::guide {

AnimatedTextInput* AnimatedTextInput::create(float width, std::string const& placeholder) {
    auto ret = new AnimatedTextInput();
    if (ret && ret->init(width, placeholder)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AnimatedTextInput::init(float width, std::string const& placeholder) {
    if (!CCNode::init()) return false;
    m_width = width;

    constexpr float kHeight = 30.f;
    this->setContentSize({width, kHeight});
    this->setAnchorPoint({0.5f, 0.5f});

    m_glow = CCScale9Sprite::create("GJ_square01.png");
    m_glow->setColor({90, 150, 255});
    m_glow->setOpacity(0); // animated on input
    m_glow->setContentSize({width + 12.f, kHeight + 8.f});
    m_glow->setAnchorPoint({0.5f, 0.5f});
    m_glow->setPosition({width * 0.5f, kHeight * 0.5f});
    this->addChild(m_glow, 0);

    m_input = TextInput::create(width, placeholder.c_str(), "chatFont.fnt");
    if (m_input) {
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(120);
        m_input->setAnchorPoint({0.5f, 0.5f});
        m_input->setPosition({width * 0.5f, kHeight * 0.5f});
        geode::WeakRef<AnimatedTextInput> weak = this;
        m_input->setCallback([weak](std::string const& s) {
            if (auto self = weak.lock()) {
                static_cast<AnimatedTextInput*>(self.data())->onTextChanged(s);
            }
        });
        this->addChild(m_input, 1);

        // Interpose the Enter relay between the input node and geode's
        // delegate so Enter presses reach setOnSubmit while everything else
        // keeps flowing to geode's TextInput.
        if (auto* node = m_input->getInputNode()) {
            m_enterRelay.forward = node->m_delegate;
            geode::WeakRef<AnimatedTextInput> weakSelf = this;
            m_enterRelay.onEnter = [weakSelf]() {
                auto self = weakSelf.lock();
                if (!self) return;
                auto* input = static_cast<AnimatedTextInput*>(self.data());
                if (input->m_onSubmit) input->m_onSubmit();
            };
            node->m_delegate = &m_enterRelay;
        }
    }

    m_typingDot = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (!m_typingDot) {
        // Robust fallback: a blank CCSprite.
        m_typingDot = CCSprite::create();
    }
    m_typingDot->setColor({255, 220, 100});
    m_typingDot->setScale(0.35f);
    m_typingDot->setOpacity(150);
    m_typingDot->setPosition({width - 8.f, kHeight * 0.5f});
    this->addChild(m_typingDot, 2);

    this->setID("animated-text-input"_spr);
    if (m_input)     m_input->setID("animated-text-input-inner"_spr);
    if (m_glow)      m_glow->setID("animated-text-input-glow"_spr);
    if (m_typingDot) m_typingDot->setID("animated-text-input-dot"_spr);

    return true;
}

void AnimatedTextInput::onExit() {
    m_enterRelay.onEnter = nullptr;
    m_enterRelay.forward = nullptr;
    paimon::ui::detachGeodeTextInput(m_input);
    m_input = nullptr;
    CCNode::onExit();
}

void AnimatedTextInput::setCallback(std::function<void(std::string const&)> cb) {
    m_userCallback = std::move(cb);
}

void AnimatedTextInput::setOnSubmit(std::function<void()> cb) {
    m_onSubmit = std::move(cb);
}

std::string AnimatedTextInput::getString() const {
    if (!m_input) return {};
    return m_input->getString();
}

void AnimatedTextInput::setString(std::string const& s) {
    if (m_input) m_input->setString(s);
}

void AnimatedTextInput::clear() {
    if (m_input) m_input->setString("");
}

void AnimatedTextInput::onTextChanged(std::string const& text) {
    // Pulse the glow while typing
    startGlowPulse();
    playTypingPulse();

    // Forward to the user callback
    if (m_userCallback) m_userCallback(text);
}

void AnimatedTextInput::startGlowPulse() {
    if (!m_glow) return;
    m_glow->stopActionByTag(kGlowPulseTag);

    // Pulse: opacity 0 -> 110 -> 60 -> 0 over 0.6s
    auto a = CCFadeTo::create(0.10f, 110);
    auto b = CCFadeTo::create(0.20f, 60);
    auto c = CCFadeTo::create(0.30f, 0);
    auto seq = CCSequence::create(a, b, c, nullptr);
    seq->setTag(kGlowPulseTag);
    m_glow->runAction(seq);
}

void AnimatedTextInput::stopGlowPulse() {
    if (!m_glow) return;
    m_glow->stopActionByTag(kGlowPulseTag);
    m_glow->setOpacity(0);
}

void AnimatedTextInput::playTypingPulse() {
    if (!m_typingDot) return;
    m_typingDot->stopAllActions();
    m_typingDot->setScale(0.35f);
    auto pop = CCSequence::create(
        CCEaseBackOut::create(CCScaleTo::create(0.10f, 0.55f)),
        CCEaseBackIn::create(CCScaleTo::create(0.10f, 0.35f)),
        nullptr
    );
    m_typingDot->runAction(pop);
}

void AnimatedTextInput::playSendSweep() {
    if (!m_glow) return;

    // Intense glow on send
    m_glow->stopActionByTag(kGlowPulseTag);
    auto big = CCSequence::create(
        CCFadeTo::create(0.05f, 200),
        CCFadeTo::create(0.40f, 0),
        nullptr
    );
    big->setTag(kGlowPulseTag);
    m_glow->runAction(big);

    // Light streak crossing the input left to right.
    auto sz = this->getContentSize();
    auto sweep = CCLayerColor::create({150, 220, 255, 100}, 6.f, sz.height);
    sweep->setPosition({-6.f, 0.f});
    sweep->setID("send-sweep"_spr);
    this->addChild(sweep, 3);

    auto move = CCEaseSineOut::create(
        CCMoveTo::create(0.30f, {sz.width + 4.f, 0.f})
    );
    auto fadeOut = CCFadeTo::create(0.30f, 0);
    auto remove = CCRemoveSelf::create();
    auto seq = CCSequence::create(
        CCSpawn::create(move, fadeOut, nullptr),
        remove,
        nullptr
    );
    seq->setTag(kSweepTag);
    sweep->runAction(seq);
}

} // namespace paimon::guide
