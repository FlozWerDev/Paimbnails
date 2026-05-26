#include "AnimatedTextInput.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

namespace paimon::guide {

// ─────────────────────────────────────────────────────────────────────────────
// Construccion
// ─────────────────────────────────────────────────────────────────────────────

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

    // Altura visual aproximada del TextInput de Geode con chatFont
    constexpr float kHeight = 30.f;
    this->setContentSize({width, kHeight});
    this->setAnchorPoint({0.5f, 0.5f});

    // ── Glow halo (detras) ────────────────────────────────────────────────
    m_glow = CCScale9Sprite::create("GJ_square01.png");
    m_glow->setColor({90, 150, 255});
    m_glow->setOpacity(0); // se anima al pulsar
    m_glow->setContentSize({width + 12.f, kHeight + 8.f});
    m_glow->setAnchorPoint({0.5f, 0.5f});
    m_glow->setPosition({width * 0.5f, kHeight * 0.5f});
    this->addChild(m_glow, 0);

    // ── TextInput central ──────────────────────────────────────────────────
    m_input = TextInput::create(width, placeholder.c_str(), "chatFont.fnt");
    if (m_input) {
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(120);
        m_input->setAnchorPoint({0.5f, 0.5f});
        m_input->setPosition({width * 0.5f, kHeight * 0.5f});
        m_input->setCallback([this](std::string const& s) { this->onTextChanged(s); });
        this->addChild(m_input, 1);
    }

    // ── Typing dot a la derecha ────────────────────────────────────────────
    // Usamos GJ_arrow_03_001.png (un disco pequeno) tinteado.
    m_typingDot = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (!m_typingDot) {
        // Fallback robusto: dibujar un cuadrado simple via CCSprite blank.
        m_typingDot = CCSprite::create();
    }
    m_typingDot->setColor({255, 220, 100});
    m_typingDot->setScale(0.35f);
    m_typingDot->setOpacity(150);
    m_typingDot->setPosition({width - 8.f, kHeight * 0.5f});
    this->addChild(m_typingDot, 2);

    // IDs para devtools
    this->setID("animated-text-input"_spr);
    if (m_input)     m_input->setID("animated-text-input-inner"_spr);
    if (m_glow)      m_glow->setID("animated-text-input-glow"_spr);
    if (m_typingDot) m_typingDot->setID("animated-text-input-dot"_spr);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// API publica
// ─────────────────────────────────────────────────────────────────────────────

void AnimatedTextInput::setCallback(std::function<void(std::string const&)> cb) {
    m_userCallback = std::move(cb);
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

// ─────────────────────────────────────────────────────────────────────────────
// Feedback visual
// ─────────────────────────────────────────────────────────────────────────────

void AnimatedTextInput::onTextChanged(std::string const& text) {
    // Pulso del glow cuando se escribe
    startGlowPulse();
    playTypingPulse();

    // Reenviar al callback del usuario
    if (m_userCallback) m_userCallback(text);
}

void AnimatedTextInput::startGlowPulse() {
    if (!m_glow) return;
    m_glow->stopActionByTag(kGlowPulseTag);

    // Pulso: opacidad 0 -> 110 -> 60 -> 0 en 0.6s
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

    // Glow intenso al enviar
    m_glow->stopActionByTag(kGlowPulseTag);
    auto big = CCSequence::create(
        CCFadeTo::create(0.05f, 200),
        CCFadeTo::create(0.40f, 0),
        nullptr
    );
    big->setTag(kGlowPulseTag);
    m_glow->runAction(big);

    // Raya luminosa que cruza el input de izquierda a derecha.
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
