#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::quickhub {

bool handleQuickButtonRightClick();
bool activateCustomQuickButton(std::string const& id);

// True cuando el boton guardado se puede pulsar ahora mismo (pantalla correcta).
bool isCustomQuickButtonReachable(std::string const& id);

} // namespace paimon::quickhub
