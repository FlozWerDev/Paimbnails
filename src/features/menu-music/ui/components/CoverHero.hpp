#pragma once

// CoverHero — "hero" visual del popup Menu Music.
//
// Ocupa la columna izquierda completa del popup como lo hace una LevelCell:
// un rectangulo alto con un corte diagonal en el borde derecho. Dentro se
// muestra la portada en modo "cover" (llena el area y se recorta). Encima
// de la portada se superpone un pequeno vinilo giratorio como detalle
// decorativo, para mantener la idea de "musica" sin que el disco sea el
// elemento principal.
//
//   ┌────────────┐╲
//   │            │ ╲
//   │   cover    │  ╲
//   │   (image)  │   ╲
//   │            │    ╲
//   │    ◯       │    │  ← disco pequeno flotando en la esquina
//   └────────────┘────┘
//
// Si no hay cover muestra un gradiente oscuro + nota musical de fallback
// para que nunca quede un rectangulo vacio.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class VinylDisc;

class CoverHero : public cocos2d::CCNode {
public:
    // `size` es el tamano total del hero (incluye el corte diagonal).
    // `skew` controla cuanto se inclina el borde derecho; 0 = recto.
    static CoverHero* create(const cocos2d::CCSize& size, float skew = 28.f);

    void setCoverFromPath(const std::string& absolutePath);
    void clearCover();

    // Controla la animacion del vinilo decorativo.
    void startSpinning();
    void stopSpinning();

    // Acceso al disco interno para conectarlo como boton play/pause.
    VinylDisc* getDisc() const { return m_smallDisc; }

    // Alterna la apariencia pausada (cover gris + disco quieto) sin tocar
    // la decision logica del player. El MenuMusicPopup la llama segun el
    // estado del player.
    void setPausedAppearance(bool paused);

protected:
    bool init(const cocos2d::CCSize& size, float skew);

    cocos2d::CCSize m_size;
    float m_skew = 0.f;

    cocos2d::CCClippingNode* m_clip = nullptr;   // recorte diagonal
    cocos2d::CCSprite* m_coverSprite = nullptr;  // portada ajustada
    cocos2d::CCNode* m_fallback = nullptr;       // visual cuando no hay cover
    cocos2d::CCNode* m_gradient = nullptr;       // overlay oscuro para legibilidad
    VinylDisc* m_smallDisc = nullptr;            // disco decorativo
    cocos2d::CCDrawNode* m_borderGloss = nullptr;// highlight en el borde diagonal
};

} // namespace paimon::menumusic
