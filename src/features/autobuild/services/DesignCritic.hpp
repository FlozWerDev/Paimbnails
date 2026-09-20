#pragma once

// Puntua fidelidad a la referencia; puro sin Geode para cubrir en tests.

#include <vector>

#include "../AutobuildTypes.hpp"
#include "Solver.hpp"

namespace paimon::autobuild {

struct DesignScore {
    double total = 0.0;      // 0..100, mas es mas fiel a la referencia
    double coverage = 0.0;   // celdas llenas / celdas pedidas
    double variety = 0.0;    // piezas distintas usadas / esperadas
    double palette = 0.0;    // colores usados dentro de la paleta muestra
    double clean = 0.0;      // 1 - (forzadas + huecos no pedidos)
    int forced = 0;
    int gaps = 0;
};

DesignScore scoreDesign(Template const& tpl, std::vector<Placement> const& placements,
                        SolveStats const& stats, Options const& opts);

// Desempate: mayor total gana; a igual total, menos forzadas y huecos.
bool isBetter(DesignScore const& a, DesignScore const& b);

} // namespace paimon::autobuild
