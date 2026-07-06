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
    // Reconstruccion diferida al siguiente tick (cambio de pestana).
    void scheduleRebuild();

    geode::ScrollLayer* m_scroll = nullptr;
    int m_tab = 0; // 0 = Basico (menus), 1 = Avanzado (editor)
};

} // namespace paimon::smoothscroll
