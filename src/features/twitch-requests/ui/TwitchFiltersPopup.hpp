#pragma once

// Popup del boton "Filtros": niveles aceptados y limites por usuario.

#include <Geode/Geode.hpp>

namespace paimon::twitch {

class TwitchFiltersPopup : public geode::Popup {
public:
    static TwitchFiltersPopup* create();

protected:
    bool init() override;
    void rebuild();
    void onRemoveFiltered();

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::twitch
