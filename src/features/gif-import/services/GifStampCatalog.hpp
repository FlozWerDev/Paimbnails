#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// Lado de la firma con la que se compara un molde contra una mancha. 8x8 cabe en
// un entero de 64 bits, asi que probar la biblioteca entera contra una mancha
// son dos instrucciones por molde en vez de recorrer su dibujo.
constexpr int kStampSignatureSide = 8;
constexpr int kStampMaskSide = 32;

// Un objeto de decoracion tal como lo dibuja el juego, sin orientar. `mask` va
// recortada a lo que pinta, y el desplazamiento dice donde queda ese recorte
// dentro del cuadro del objeto.
struct CatalogEntry {
    int objectId = 0;
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    float offsetX = 0.f;
    float offsetY = 0.f;
    StampMask mask;
};

// Una orientacion concreta, que es lo que la busqueda prueba.
struct StampVariant {
    PlanStamp stamp;
    std::uint64_t signature = 0;
    int filled = 0;
};

// La biblioteca la rellena el juego rasterizando sus propios objetos. Se fija
// desde el hilo principal antes de arrancar el trazado y no se toca mientras
// corre, que es lo unico que la hace segura de leer desde los hilos del reparto.
void setStampCatalog(std::vector<CatalogEntry> entries);
std::vector<StampVariant> const& stampVariants();
bool hasStampCatalog();

// Repuestos analiticos para la biblioteca suave (Blur/Vert) cuando GD no expone
// glow o gradiente nativo. Usan las mismas formulas que el ajuste de
// buildSoftStampLibrary: gaussiana radial normalizada, rampa 1-v y caida de
// esquina, en celdas de kStampMaskSide.
StampMask analyticRadialGlowMask();
StampMask analyticVerticalGradientMask();
StampMask analyticQuarterGlowMask();

// Las cuatro figuras de siempre, para cuando el juego todavia no ha mirado su
// biblioteca: asi el modo libre nunca se queda sin nada que soltar.
std::vector<CatalogEntry> builtinStampCatalog();

} // namespace paimon::gifimport
