#pragma once

#include <cmath>
#include <cstdint>

namespace paimon::texture_studio {

// How a replacement image fits the target frame before the user transform applies.
enum class ImageFitMode : int {
    Fit     = 0,  // contain: largest size that fully fits, keeps aspect
    Fill    = 1,  // cover: smallest size that covers the frame, keeps aspect
    Stretch = 2,  // ignore aspect, fill exactly
};

// User transform, frame-relative so one setting fits -uhd and downscaled -hd.
struct ImageTransform {
    ImageFitMode fitMode = ImageFitMode::Fit;

    // Multiplier over the fit base scale. 1.0 = exactly fitted.
    float scale = 1.0f;

    // -1..1, fraction of half frame (1.0 = half-frame shift).
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    // Clockwise degrees around the image centre.
    float rotationDeg = 0.0f;

    // 0..255 multiplied into the image alpha.
    int opacity = 255;

    bool flipX = false;
    bool flipY = false;

    bool isDefault() const {
        return fitMode == ImageFitMode::Fit &&
               std::fabs(scale - 1.0f) < 1e-4f &&
               std::fabs(offsetX) < 1e-4f && std::fabs(offsetY) < 1e-4f &&
               std::fabs(rotationDeg) < 1e-3f &&
               opacity == 255 && !flipX && !flipY;
    }
};

}  // namespace paimon::texture_studio
