#pragma once

#include "../data/ImageBuffer.hpp"
#include "../data/ImageTransform.hpp"
#include "MaskBuilder.hpp"

#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

enum class FusionBlendMode : int {
    Replace = 0,
    // Preserve base luminance while applying texture hue/saturation.
    MultiplyLuma = 1,
    // Straight-alpha texture over base.
    Overlay = 2,
};

struct FusionApplyOptions {
    // Replace leaves texture/GIF colors untouched by pack tint.
    FusionBlendMode blendMode = FusionBlendMode::Replace;
    // Extra opacity multiplier (0..1).
    float opacity = 1.0f;
    // Scale, offset, rotation, flip, and fit.
    ImageTransform transform{};
    // Integer nudge; +X right, +Y down in image space.
    int pixelOffsetX = 0;
    int pixelOffsetY = 0;
};

class FusionEngine final {
public:
    // 8-way flood fill of color-similar pixels into an R8 mask. Transparent
    // pixels are hard borders; colorRadius and expandRadius control tolerance.
    static MaskBuffer floodFill(ImageBuffer const& sprite,
                                int seedX, int seedY,
                                int colorRadius = 120,
                                int alphaCutoff = 12,
                                int expandRadius = 1);

    // Expand into opaque pixels matching the seed color.
    static void expandMask(MaskBuffer& mask,
                           ImageBuffer const& sprite,
                           int radius,
                           std::uint8_t seedR, std::uint8_t seedG, std::uint8_t seedB,
                           int colorRadius,
                           int alphaCutoff = 12);

    // OR masks with matching dimensions.
    static void orMask(MaskBuffer& dst, MaskBuffer const& src);

    // Apply texture to non-zero mask pixels. Map over the full frame so preview
    // and export remain aligned as the mask grows.
    static void apply(ImageBuffer& base,
                      MaskBuffer const& mask,
                      ImageBuffer const& texture,
                      FusionApplyOptions const& options = {});

    // Apply into a copy.
    static ImageBuffer applyCopy(ImageBuffer const& base,
                                 MaskBuffer const& mask,
                                 ImageBuffer const& texture,
                                 FusionApplyOptions const& options = {});

    // Bounds of non-zero mask pixels; false when empty.
    static bool maskBounds(MaskBuffer const& mask,
                           int& outX, int& outY, int& outW, int& outH);

    // Soft 0..1 coverage for live preview.
    static std::vector<float> softCoverage(MaskBuffer const& mask);

    // Build a full-frame stamp; sample offsets from the source and fit to mask
    // bounds when supplied.
    static ImageBuffer buildStampCanvas(ImageBuffer const& texture,
                                        int frameW, int frameH,
                                        ImageTransform transform,
                                        MaskBuffer const* mask = nullptr,
                                        int pixelOffsetX = 0,
                                        int pixelOffsetY = 0);

    // Fast drag path reusing coverage and stamp; shifts only by pixelOffset.
    static void applyCached(ImageBuffer& base,
                            std::vector<float> const& coverage,
                            ImageBuffer const& stampCanvas,
                            FusionApplyOptions const& options);

private:
    FusionEngine() = delete;
};

}
