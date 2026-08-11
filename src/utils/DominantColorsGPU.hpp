#pragma once

#include "DominantColors.hpp"
#include <Geode/cocos/textures/CCTexture2D.h>
#include <cstdint>
#include <utility>

namespace DominantColorsGPU {

/// GPU path: render to a 32×32 LAB FBO, then cluster the readback on CPU.
/// Falls back to DominantColors::extract when GL, the shader, or input is unavailable.
/// Must run on the main/GL thread.
std::pair<DCColor, DCColor> extractFromTexture(cocos2d::CCTexture2D* texture);

/// Extract from RGB24 using a temporary texture; falls back to CPU.
std::pair<DCColor, DCColor> extractFromRGB(const uint8_t* rgb, int width, int height);

/// Extract from RGBA32 using a temporary texture; falls back to CPU.
std::pair<DCColor, DCColor> extractFromRGBA(const uint8_t* rgba, int width, int height);

/// Whether the shader and GL context are available; the result is cached.
bool isAvailable();

/// Invalidate the cached readback FBO before GD recreates the GL context.
void onGLContextReload();

}
