#pragma once
#include <Geode/Geode.hpp>
#include <string>

// Crea un nodo stencil con la forma indicada.
// Soporta formas geometricas (circle, triangle, hexagon, diamond, star, heart, pentagon, octagon)
// y tambien sprites Scale9 (cualquier nombre que termine en .png).
// El nodo resultante tiene el contentSize indicado y esta centrado en su propio centro.
cocos2d::CCNode* createShapeStencil(std::string const& shapeName, float size);

// Crea un nodo con el BORDE/CONTORNO de la forma indicada (no relleno).
// util para dibujar marcos que siguen la misma forma del stencil.
cocos2d::CCNode* createShapeBorder(std::string const& shapeName, float size, float thickness, cocos2d::ccColor3B color, GLubyte opacity = 255);

// Lista de formas geometricas disponibles (no incluye sprites Scale9)
std::vector<std::pair<std::string, std::string>> getGeometricShapes();
