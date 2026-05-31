#pragma once

#include <Geode/cocos/base_nodes/CCNode.h>

namespace paidraw {

// Crea el icono de PaiDraw usando solo assets de Geometry Dash
// (`GJ_paintBtn_001.png` + `GJ_starsIcon_001.png`). Devuelve un CCNode
// con tamaño `targetSize x targetSize`. Listo para insertarse en
// cualquier botón / menú GD-canónico.
cocos2d::CCNode* createPaiDrawIcon(float targetSize = 32.f);

} // namespace paidraw
