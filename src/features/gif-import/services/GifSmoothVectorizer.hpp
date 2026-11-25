#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// modo sin optimizacion (el metodo de contornos): cada region se traza como
// contorno vectorial cerrado, se simplifica con Douglas-Peucker, se redondea
// con Chaikin restaurando su area y se le quitan los vertices-pico antes de
// emitir bandas. El convertidor normal ajusta rectas a la escalera de filas;
// aqui las bandas nacen de la curva, con lineas continuas y sin picos.
// `coveredLater` marca celdas que un color posterior repinta: se puede
// derramar sobre ellas y los huecos que repintan enteros no se pagan.
std::vector<Primitive> vectorizeSmoothPaint(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& coveredLater = {}
);

} // namespace paimon::gifimport
