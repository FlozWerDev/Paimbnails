#pragma once
// Punto de entrada desde src/hooks/GJGarageLayer.cpp: cuelga el boton de la
// tienda en la garage.

class GJGarageLayer;

namespace paimon::icon_gallery::garage {

// Se llama desde PaimonGJGarageLayer::init DESPUES del original.
void onGarageInit(GJGarageLayer* layer);

}  // namespace paimon::icon_gallery::garage
