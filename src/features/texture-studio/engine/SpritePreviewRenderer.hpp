#pragma once

#include "../data/ImageBuffer.hpp"
#include "../data/ImageTransform.hpp"
#include "LuminanceTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

// Tint params shared by live previews and export: editor shows exactly what the pack generates.
struct SpritePreviewOptions {
    TintColors colors{};
    int   brightness = 160;
    bool  alternativeGlowOverlay = false;
    float maskSoftness = 0.35f;

    // Number of color clusters the segmentation looks for (2..10).
    int   clusterPrecision = 5;

    // Edge-aware refinement (0..4): kills speckle, keeps real edges.
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

struct MaskBuildResult {
    MaskSet masks;
    SpritePreviewStats stats;
};

class SpritePreviewRenderer final {
public:
    static ImageBuffer renderTinted(ImageBuffer const& framePixels,
                                    SpritePreviewOptions const& options);

    static SpritePreviewResult renderTintedWithStats(
        ImageBuffer const& framePixels,
        SpritePreviewOptions const& options);

    // Tintless segmentation with identical role maps, so GPU preview and CPU bake agree.
    static MaskBuildResult renderMasks(ImageBuffer const& framePixels,
                                       SpritePreviewOptions const& options);

    // Role weights packed RGBA (R=C1 G=C2 B=detail A=glow) for GPU upload.
    static ImageBuffer renderRoleMask(MaskSet const& masks);

    // User image composited honoring the transform, bilinear.
    static ImageBuffer renderCustomImage(ImageBuffer const& userImage,
                                         int frameW, int frameH,
                                         ImageTransform const& transform = {},
                                         float pixelOffsetX = 0.f,
                                         float pixelOffsetY = 0.f);

    // Straight-alpha "over" in place; size mismatch is a no-op.
    static void compositeOver(ImageBuffer& base, ImageBuffer const& top);

    static cocos2d::CCTexture2D* createTexture(ImageBuffer const& image);

    static cocos2d::CCSprite* createSprite(ImageBuffer const& image);

private:
    SpritePreviewRenderer() = delete;
};

}  // namespace paimon::texture_studio
