#pragma once

#include "../data/ImageBuffer.hpp"
#include "../data/ImageTransform.hpp"
#include "LuminanceTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

// Full tint parameter set, shared by the live previews and the export path
// so what you see in the editor is exactly what the pack generates.
struct SpritePreviewOptions {
    TintColors colors{};
    int   brightness = 160;
    bool  alternativeGlowOverlay = false;
    float maskSoftness = 0.35f;

    // Number of color clusters the segmentation looks for (2..10).
    int   clusterPrecision = 5;

    // Edge-aware mask refinement passes (0..4). Removes speckle without
    // blurring real color edges.
    int   edgeCleanup = 1;

    // Pixels darker than this Rec.601 luminance are never tinted (0 = off).
    int   outlineProtect = 0;

    // Post-tint color grading (tinted pixels only).
    float saturation = 1.0f;
    float contrast   = 0.0f;
};

struct SpritePreviewStats {
    float color1Coverage = 0.f;
    float color2Coverage = 0.f;
    float glowCoverage = 0.f;
    float outlineCoverage = 0.f;
    bool needsReview = false;
};

struct SpritePreviewResult {
    ImageBuffer image;
    SpritePreviewStats stats;
};

class SpritePreviewRenderer final {
public:
    static ImageBuffer renderTinted(ImageBuffer const& framePixels,
                                    SpritePreviewOptions const& options);

    static SpritePreviewResult renderTintedWithStats(
        ImageBuffer const& framePixels,
        SpritePreviewOptions const& options);

    // Composites the user image into a frameW×frameH canvas honoring the
    // transform (fit mode, scale, offset, rotation, opacity, flips) with
    // bilinear resampling.
    static ImageBuffer renderCustomImage(ImageBuffer const& userImage,
                                         int frameW, int frameH,
                                         ImageTransform const& transform = {},
                                         float pixelOffsetX = 0.f,
                                         float pixelOffsetY = 0.f);

    // Straight-alpha "over" blend of `top` onto `base` in place. Both buffers
    // must have identical dimensions; mismatches are a no-op.
    static void compositeOver(ImageBuffer& base, ImageBuffer const& top);

    static cocos2d::CCTexture2D* createTexture(ImageBuffer const& image);

    static cocos2d::CCSprite* createSprite(ImageBuffer const& image);

private:
    SpritePreviewRenderer() = delete;
};

}  // namespace paimon::texture_studio
