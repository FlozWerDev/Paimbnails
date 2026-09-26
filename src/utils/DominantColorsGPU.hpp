#pragma once

#include "DominantColors.hpp"
#include <Geode/cocos/textures/CCTexture2D.h>
#include <cstdint>
#include <utility>

namespace DominantColorsGPU {

// GPU path: render to a 32x32 LAB FBO, then cluster the readback on CPU.
// falls back to DominantColors::extract when GL, shader or input is missing.
// main/GL thread only.
std::pair<DCColor, DCColor> extractFromTexture(cocos2d::CCTexture2D* texture);

// extract from RGB24 via a temporary texture; falls back to CPU.
std::pair<DCColor, DCColor> extractFromRGB(const uint8_t* rgb, int width, int height);

// extract from RGBA32 via a temporary texture; falls back to CPU.
std::pair<DCColor, DCColor> extractFromRGBA(const uint8_t* rgba, int width, int height);

// whether shader and GL context are available; result is cached.
bool isAvailable();

// invalidate the cached readback FBO before GD recreates the GL context.
void onGLContextReload();

}
