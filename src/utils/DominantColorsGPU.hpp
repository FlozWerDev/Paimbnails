#pragma once

#include "DominantColors.hpp"
#include <Geode/cocos/textures/CCTexture2D.h>
#include <cstdint>
#include <utility>

namespace DominantColorsGPU {

// main thread only: gpu readback falls back to cpu extraction when the gl path is unavailable.
std::pair<DCColor, DCColor> extractFromTexture(cocos2d::CCTexture2D* texture);

// extract from rgb24 via a temporary texture; falls back to cpu.
std::pair<DCColor, DCColor> extractFromRGB(const uint8_t* rgb, int width, int height);

// extract from rgba32 via a temporary texture; falls back to cpu.
std::pair<DCColor, DCColor> extractFromRGBA(const uint8_t* rgba, int width, int height);

// whether shader and gl context are available; result is cached.
bool isAvailable();

// invalidate the cached readback fbo before gd recreates the gl context.
void onGLContextReload();

}
