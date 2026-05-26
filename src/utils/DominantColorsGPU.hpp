#pragma once

#include "DominantColors.hpp"
#include <Geode/cocos/textures/CCTexture2D.h>
#include <cstdint>
#include <utility>

namespace DominantColorsGPU {

/// GPU-accelerated dominant color extraction.
/// Renders the texture to a 32×32 FBO with a shader that converts sRGB→LAB,
/// then reads back only 1024 pixels for CPU-side K-means clustering.
///
/// Falls back to the CPU path (DominantColors::extract) if:
///   - The shader fails to compile
///   - The GL context is unavailable
///   - The texture is null
///
/// MUST be called from the main/GL thread.
///
/// @param texture  Source texture (thumbnail, video frame, etc.)
/// @return Pair of dominant colors {primary, secondary}
std::pair<DCColor, DCColor> extractFromTexture(cocos2d::CCTexture2D* texture);

/// GPU-accelerated extraction from raw RGB24 data.
/// Creates a temporary CCTexture2D, runs the GPU path, then releases it.
/// Falls back to CPU path if GPU is unavailable.
///
/// MUST be called from the main/GL thread.
std::pair<DCColor, DCColor> extractFromRGB(const uint8_t* rgb, int width, int height);

/// GPU-accelerated extraction from raw RGBA32 data.
/// Creates a temporary CCTexture2D, runs the GPU path, then releases it.
/// Falls back to CPU path if GPU is unavailable.
///
/// MUST be called from the main/GL thread.
std::pair<DCColor, DCColor> extractFromRGBA(const uint8_t* rgba, int width, int height);

/// Returns true if the GPU path is available (shader compiled, GL context ok).
/// Cheap to call — caches the result after first check.
bool isAvailable();

} // namespace DominantColorsGPU
