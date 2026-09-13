#pragma once

#include "../GifImportTypes.hpp"

#include <memory>

namespace paimon::gifimport {

// Deja los frames en la resolucion que el trazado va a mirar de verdad antes de
// soltar el hilo de trabajo. Un video de 1080p se pasa el rato promediando dos
// millones de pixeles por frame para acabar en una rejilla de 64 celdas: esto lo
// hace una vez, en la GPU cuando hay contexto y por reparto de hilos cuando no,
// y todo lo que viene detras trabaja ya sobre la imagen pequena.
//
// Hilo principal: toca GL. Devuelve la misma fuente cuando no hay nada que
// recortar.
std::shared_ptr<SourceAnimation> prescaleSource(
    std::shared_ptr<SourceAnimation> source,
    int maxDimension,
    float blurRadius = 0.f
);

} // namespace paimon::gifimport
