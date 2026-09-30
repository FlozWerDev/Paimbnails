#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// near-identical palette colors add objects without visible detail; perceptual steps are about 0.02.
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
