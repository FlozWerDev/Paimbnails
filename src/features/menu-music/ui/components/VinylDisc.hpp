#pragma once

// VinylDisc — disco giratorio con la portada del track al centro.
//
// Visualmente: circulo negro (fondo de "vinilo"), la portada recortada
// circularmente girando, aro exterior gris y un pin brillante en el
// centro. El giro se pausa cuando el reproductor esta en pausa.
//
// Diferenciado del disco del reference mod:
//   * El ref muestra un CCMenu con notas pegadas, nosotros un disco real.
//   * La portada se recorta con CCClippingNode + stencil circular en vez
//     de usar solo un sprite cuadrado encima.
//   * Tres aros concentricos (vinilo) para dar sensacion de surco.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class VinylDisc : public cocos2d::CCNode {
public:
    static VinylDisc* create(float radius);

    // Setea o limpia la portada. Path vacio = solo aros.
    void setCoverFromPath(const std::string& absolutePath);
    void clearCover();

    // Giro
    void startSpinning();
    void stopSpinning();
    bool isSpinning() const { return m_spinning; }

    // Velocidad en grados por segundo. Default 40.
    void setSpinSpeed(float degPerSec) { m_spinSpeed = degPerSec; }

    // Marca visual "pausado": apaga los colores de la portada (semi gris)
    // y baja la opacidad del vinilo para indicar que esta quieto. Cuando
    // vuelve a reproducir (paused=false) restaura color + opacidad.
    void setPausedAppearance(bool paused);

protected:
    bool init(float radius);
    void tick(float dt);

    float m_radius = 60.f;
    float m_spinSpeed = 40.f;
    bool m_spinning = false;

    cocos2d::CCNode* m_rotating = nullptr;   // hijo que gira (contiene cover + aros)
    cocos2d::CCClippingNode* m_coverClip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    cocos2d::CCSprite* m_centerDot = nullptr;
};

} // namespace paimon::menumusic
