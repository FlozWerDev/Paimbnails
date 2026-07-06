#pragma once
#include <Geode/Geode.hpp>

namespace paimon::smoothscroll {

// Popup de configuracion de smooth-scroll, montado sobre PaiConfigKit:
// tarjetas por seccion, descripciones y valores siempre visibles.
class SmoothScrollConfigPopup : public geode::Popup {
public:
    static SmoothScrollConfigPopup* create();

protected:
    bool init() override;

    // Reconstruye el contenido scrolleable (tras un reset, por ejemplo).
    void rebuild();

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::smoothscroll
