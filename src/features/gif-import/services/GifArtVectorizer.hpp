#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// Las celdas de `spare` se pueden pisar pero no hace falta cubrirlas: son las que
// otra capa tapa despues. Dejar que el rectangulo las atraviese es lo que convierte
// un reguero de cuadrados de una celda en un rectangulo entero.
std::vector<Primitive> packBlocks(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& spare = {}
);

std::vector<Primitive> vectorizeArt(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked = {}
);

std::vector<std::uint8_t> renderPlanFrame(
    ImportPlan const& plan,
    int frame,
    int scale
);

} // namespace paimon::gifimport
