#pragma once

// CoverBlurBackground — fondo del popup con la portada difuminada.
//
// Se usa como hijo del content clipper del popup con zOrder 0.
// Cuando se le pasa una nueva portada, dispara buildPaimonBlurAsync
// sobre el sistema Shaders existente y, al completarse, crossfade del
// sprite antiguo al nuevo. Si la portada cambia antes de que termine
// el blur, se cancela el reemplazo (token de generacion).
//
// Esta version NO pinta overlay oscuro encima: solo la imagen. El
// recorte redondeado lo aporta el content clipper maestro del popup.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class CoverBlurBackground : public cocos2d::CCNode {
public:
    static CoverBlurBackground* create(cocos2d::CCSize const& size);

    // Reemplaza el fondo por la portada dada. Si el path esta vacio,
    // limpia (fondo queda transparente). Es seguro llamarla multiples
    // veces aunque el blur anterior no haya terminado.
    void setCoverFromPath(const std::string& absolutePath);

protected:
    bool init(cocos2d::CCSize const& size);
    void applyBlurFromTexture(cocos2d::CCTexture2D* tex, std::uint64_t generation);

    cocos2d::CCSize m_size;
    cocos2d::CCSprite* m_currentBlur = nullptr;
    std::uint64_t m_generation = 0;
    std::string m_lastPath;
};

} // namespace paimon::menumusic
