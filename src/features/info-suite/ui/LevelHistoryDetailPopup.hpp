#pragma once

// La ficha de un snapshot suelto del historial: todo lo que ese registro
// guardo, incluido lo que no cabe en la fila (cancion, tiempo de edicion,
// tamano del archivo, monedas, copia de...).

#include "../services/LevelHistoryModel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

namespace paimon::info {

class LevelHistoryDetailPopup : public geode::Popup {
public:
    static LevelHistoryDetailPopup* create(HistoryEntry const& entry);

protected:
    bool init(HistoryEntry const& entry);
};

} // namespace paimon::info
