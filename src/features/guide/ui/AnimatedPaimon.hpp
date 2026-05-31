#pragma once

#include <Geode/Geode.hpp>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// AnimatedPaimon
//
// Wrapper visual sobre el sprite paim_Paimon.png con animaciones encadenables
// y compatibles entre si:
//
//   - Idle:      bobbing vertical infinito (siempre activo en lively=true)
//   - Blink:     pestaneo rapido (escala-Y x0.1) periodico
//   - Talk:      cabeceo y rotacion mientras Paimon "habla"
//   - Surprise:  scale-pop sorprendido
//   - Wave:      saludo (rotacion lateral)
//   - Point:     apuntar hacia un nodo objetivo
//   - Sleep:     opacidad baja + bobbing lentisimo
//
// Las animaciones unitarias (Blink, Talk, Surprise, Wave) corren por encima
// del Idle gracias a tags de CCAction:
//
//   - kIdleTag       = 1001
//   - kBlinkTag      = 1002
//   - kStateTag      = 1003 (Talk/Surprise/Wave/Point/Sleep)
//
// Asi setLively(true) puede tener Idle + Blink + Talk corriendo a la vez sin
// que se cancelen entre si.
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class AnimatedPaimon : public cocos2d::CCNode {
public:
    enum class Animation {
        Idle,
        Blink,
        Talk,
        Surprise,
        Wave,
        Point,
        Sleep,
    };

    static AnimatedPaimon* create(float spriteScale = 1.0f);

    // Reproduce una animacion. Idle/Blink son loop, el resto son one-shot
    // y al terminar la instancia vuelve al Idle natural.
    void play(Animation anim);

    // Apunta hacia un nodo objetivo (calcula angulo y rota el sprite).
    // Si target=null, vuelve a rotacion 0.
    void pointAt(cocos2d::CCNode* target, float duration = 0.3f);

    // En modo "lively" Paimon hace Idle+Blink continuo y reacciona mas a
    // las animaciones de chat. En no-lively (false), el sprite queda
    // semi-estatico (util cuando guide-enabled=false).
    void setLively(bool lively);
    bool isLively() const { return m_lively; }

    // Acceso al sprite interno por si el caller quiere ajustar opacidad,
    // color o flipX.
    cocos2d::CCSprite* getSprite() const { return m_sprite; }

    // Burbuja de chat opcional ("Preguntame!") posicionada arriba-derecha.
    // Si text esta vacio, la burbuja se oculta.
    void showBubble(std::string const& text, float duration = 3.0f);
    void hideBubble();
    bool hasBubble() const { return static_cast<bool>(m_bubble.lock()); }

protected:
    bool init(float spriteScale);

    void startIdleLoop();
    void scheduleNextBlink();
    void onBlinkTimer(float dt);

    // Action tags para no interferir entre estados.
    static constexpr int kIdleTag  = 1001;
    static constexpr int kBlinkTag = 1002;
    static constexpr int kStateTag = 1003;

    cocos2d::CCSprite* m_sprite = nullptr;
    geode::WeakRef<cocos2d::CCNode> m_bubble;
    cocos2d::CCLabelBMFont* m_bubbleText = nullptr;

    bool m_lively = false;
    Animation m_currentState = Animation::Idle;
};

} // namespace paimon::guide
