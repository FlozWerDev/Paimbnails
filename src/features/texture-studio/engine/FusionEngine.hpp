#pragma once

#include "../data/ImageBuffer.hpp"
#include "../data/ImageTransform.hpp"
#include "MaskBuilder.hpp"

#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// How the fusion texture is blended into the selected region.
enum class FusionBlendMode : int {
    // Texture fully replaces the region (respects texture alpha).
    Replace = 0,
    // Texture hue/sat * base luminance — best for buttons that keep 3D shading.
    MultiplyLuma = 1,
    // Straight-alpha "over" of the texture on top of the base.
    Overlay = 2,
};

struct FusionApplyOptions {
    // Default Replace: texture/GIF colours are never recolored by pack tint.
    FusionBlendMode blendMode = FusionBlendMode::Replace;
    // Extra opacity multiplier on top of ImageTransform::opacity (0..1).
    float opacity = 1.0f;
    // Placement (scale / fractional offset / rotation / flip / fit).
    ImageTransform transform{};
    // Extra integer pixel nudge (arrow keys). +X right, +Y down in image space.
    int pixelOffsetX = 0;
    int pixelOffsetY = 0;
};

class FusionEngine final {
public:
    // Paint-bucket flood-fill of colour-similar connected pixels (8-way).
    // Returns an R8 mask (0/255) the same size as `sprite`. Empty on invalid
    // seed. Never allocates more than O(W*H); uses an explicit stack.
    //
    // `colorRadius` (0..255): how far a colour may deviate from the seed.
    // Uses a luminance-tolerant distance so light/dark shades of the same
    // hue (e.g. pale yellow ↔ deeper yellow) stay connected.
    // `expandRadius` (0..12): after the fill, grow the mask by N pixels into
    // neighbours that still match the seed colour (AA / gradient fringes only).
    // Never expands into white outlines, glyphs, or other hues.
    // Transparent pixels (alpha < alphaCutoff) act as hard borders.
    static MaskBuffer floodFill(ImageBuffer const& sprite,
                                int seedX, int seedY,
                                int colorRadius = 120,
                                int alphaCutoff = 12,
                                int expandRadius = 1);

    // Grow a mask by `radius` pixels into opaque pixels that still match the
    // seed colour within `colorRadius` (same metric as floodFill).
    static void expandMask(MaskBuffer& mask,
                           ImageBuffer const& sprite,
                           int radius,
                           std::uint8_t seedR, std::uint8_t seedG, std::uint8_t seedB,
                           int colorRadius,
                           int alphaCutoff = 12);

    // OR `src` into `dst`. Dimensions must match; no-op otherwise.
    static void orMask(MaskBuffer& dst, MaskBuffer const& src);

    // Apply `texture` into every non-zero mask pixel of `base` (in place).
    // Texture is mapped over the FULL frame (mask = stencil) so placement is
    // stable as regions grow — preview and export stay pixel-aligned.
    // Default transform uses Fill when left at defaults.
    static void apply(ImageBuffer& base,
                      MaskBuffer const& mask,
                      ImageBuffer const& texture,
                      FusionApplyOptions const& options = {});

    // Convenience: apply into a copy and return it.
    static ImageBuffer applyCopy(ImageBuffer const& base,
                                 MaskBuffer const& mask,
                                 ImageBuffer const& texture,
                                 FusionApplyOptions const& options = {});

    // Axis-aligned bounds of non-zero mask pixels. Returns false if empty.
    static bool maskBounds(MaskBuffer const& mask,
                           int& outX, int& outY, int& outW, int& outH);

    // --- Live-preview caches (drag-friendly) -----------------------------
    // Soft coverage 0..1, same layout as mask. Call when the mask changes.
    static std::vector<float> softCoverage(MaskBuffer const& mask);

    // Map texture into a full W×H canvas. Pixel offsets are sampled directly
    // from the source image instead of shifting an already-clipped canvas.
    // Rebuild when the texture, transform, offsets, or mask changes.
    // When `mask` is non-null and non-empty, the texture is fitted into the
    // mask axis-aligned bounds (the painted region) so the image is not
    // cropped to a random slice of the full sprite frame.
    static ImageBuffer buildStampCanvas(ImageBuffer const& texture,
                                        int frameW, int frameH,
                                        ImageTransform transform,
                                        MaskBuffer const* mask = nullptr,
                                        int pixelOffsetX = 0,
                                        int pixelOffsetY = 0);

    // Fast apply for drag: reuses soft coverage + stamp canvas, only shifts
    // by pixelOffset. Intended for main-thread live previews of small UI
    // sprites (no worker thread needed).
    static void applyCached(ImageBuffer& base,
                            std::vector<float> const& coverage,
                            ImageBuffer const& stampCanvas,
                            FusionApplyOptions const& options);

private:
    FusionEngine() = delete;
};

}  // namespace paimon::texture_studio
