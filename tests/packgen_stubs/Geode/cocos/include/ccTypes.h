// Stub minimo de cocos2d ccTypes SOLO para los tests Geode-free de PackGen v2
// (tests/packgen_paint_regression.cpp). Define unicamente lo que usa
// engine/TintMath.hpp (ccColor3B con sus constructores) con el mismo layout
// (3 x uint8) que el header real, asi el diferencial compara contra el codigo
// de verdad. No usar fuera de los tests: el build del mod usa el Geode real.
#pragma once

#include <cstdint>

namespace cocos2d {

typedef std::uint8_t GLubyte;

struct ccColor3B {
    GLubyte r;
    GLubyte g;
    GLubyte b;
    ccColor3B() : r(0), g(0), b(0) {}
    ccColor3B(GLubyte _r, GLubyte _g, GLubyte _b) : r(_r), g(_g), b(_b) {}
};

}  // namespace cocos2d
