#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// Traza un color con la biblioteca de decoracion entera: por cada mancha busca
// el objeto que mejor la cubre, y lo que ningun objeto cubre bien se lo queda el
// trazado de pintura de siempre. El `stamp` de cada figura es el indice de la
// variante en `stampVariants()`; el plan los recoge y los reindexa al final.
std::vector<Primitive> vectorizeFree(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked = {},
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true
);

} // namespace paimon::gifimport
