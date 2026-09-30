// stub minimo de cocos2d cctypes solo para los tests geode-free de packgen v2
// (tests/packgen_paint_regression.cpp). define unicamente lo que usa
// engine/tintmath.hpp (cccolor3b con sus constructores) con el mismo layout
// (3 x uint8) que el header real, asi el diferencial compara contra el codigo
// de verdad. no usar fuera de los tests: el build del mod usa el geode real.
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
