#pragma once

#include <array>
#include <cstddef>
#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// Rasteriza la decoracion de GD en moldes para el modo libre. Necesita GL y el
// cache de sprites del juego, asi que corre en el hilo principal; el trazado la
// lee ya hecha desde sus hilos. Se construye una vez por sesion.
// Full alpha masks: round glow, descending/ascending ramps, four radial quarters.
struct SoftStampLibrary {
    // Siempre 7 moldes cuando el toolbox esta disponible: los nativos que se
    // encontraron, el mejor aunque supere el umbral como degradado, y repuesto
    // analitico con IDs fijos (analyticFallback) para lo que siga faltando.
    // Vacia solo si el toolbox aun no existe; el pipeline lo rechaza.
    std::vector<PlanStamp> stamps;
    // Mejor error nativo por forma (radial, vertical, cuartos): alimenta el log
    // 'Native soft shapes' y el mensaje de error del pipeline.
    std::array<double, 3> errors{1.0, 1.0, 1.0};
};

SoftStampLibrary buildSoftStampLibrary();

bool stampLibraryReady();
std::size_t buildStampLibrary();

} // namespace paimon::gifimport
