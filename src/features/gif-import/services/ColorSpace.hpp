#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// Perceptual distance below which two palette entries read as one color: apart
// they only split the drawing into more objects. In OkLab black-to-white is 1
// and a barely-visible step is ~0.02.
constexpr float kPaletteMinDistance = 0.025f;

struct OkLab {
    float L = 0.f;
    float a = 0.f;
    float b = 0.f;
};

OkLab rgbToOkLab(Color color);
Color oklabToRgb(OkLab color);
float oklabDistance(OkLab const& first, OkLab const& second);

} // namespace paimon::gifimport
