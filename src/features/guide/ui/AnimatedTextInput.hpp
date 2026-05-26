#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// AnimatedTextInput
//
// Wrapper visual sobre geode::TextInput con feedback animado:
//
//   - Glow halo:    halo azul claro semi-transparente que pulsa cuando el
//                   usuario escribe (cada cambio de texto reinicia un
//                   pulso suave). Se basa en CCScale9Sprite tinteado.
//   - Typing dot:   pequeno punto luminoso a la derecha del input que se
//                   agranda y vuelve cuando el usuario tipea (efecto
//                   "estoy escuchando").
//   - Send sweep:   raya luminosa que cruza el input de izquierda a
//                   derecha al pulsar Enviar (callable manual con
//                   playSendSweep()).
//
// Geode::TextInput no provee callback nativo de "Enter pressed"; el chat
// popup gestiona el submit con un boton externo y llama playSendSweep().
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class AnimatedTextInput : public cocos2d::CCNode {
public:
    static AnimatedTextInput* create(float width, std::string const& placeholder);

    void setCallback(std::function<void(std::string const&)> cb);
    std::string getString() const;
    void setString(std::string const& s);
    void clear();

    // Visual feedback explicit triggers
    void playSendSweep();
    void playTypingPulse();

    // Acceso al input subyacente por si hay que personalizar mas cosas.
    geode::TextInput* getInput() const { return m_input; }

protected:
    bool init(float width, std::string const& placeholder);

    void onTextChanged(std::string const& text);
    void startGlowPulse();
    void stopGlowPulse();

    static constexpr int kGlowPulseTag = 2001;
    static constexpr int kSweepTag     = 2002;

    geode::TextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_glow = nullptr;
    cocos2d::CCSprite* m_typingDot = nullptr;
    std::function<void(std::string const&)> m_userCallback;

    float m_width = 0.f;
};

} // namespace paimon::guide
