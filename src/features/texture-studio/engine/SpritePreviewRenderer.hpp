#pragma once

#include "../data/ImageBuffer.hpp"
#include "../data/ImageTransform.hpp"
#include "LuminanceTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

// tint params shared by live previews and export: editor shows exactly what the pack generates.
struct SpritePreviewOptions {
    TintColors colors{};
    int   brightness = 160;
    bool  alternativeGlowOverlay = false;
    float maskSoftness = 0.35f;

    // number of color clusters the segmentation looks for (2..10).
    int   clusterPrecision = 5;

    // edge-aware refinement (0..4): kills speckle, keeps real edges.
    int   edgeCleanup = 1;

    // pixels darker than this rec.601 luminance are never tinted (0 = off).
    int   outlineProtect = 0;

    // post-tint color grading (tinted pixels only).
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

    // tintless segmentation with identical role maps, so gpu preview and cpu bake agree.
    static MaskBuildResult renderMasks(ImageBuffer const& framePixels,
                                       SpritePreviewOptions const& options);

    // role weights packed rgba (r=c1 g=c2 b=detail a=glow) for gpu upload.
    static ImageBuffer renderRoleMask(MaskSet const& masks);

    // user image composited honoring the transform, bilinear.
    static ImageBuffer renderCustomImage(ImageBuffer const& userImage,
                                         int frameW, int frameH,
                                         ImageTransform const& transform = {},
                                         float pixelOffsetX = 0.f,
                                         float pixelOffsetY = 0.f);

    // straight-alpha "over" in place; size mismatch is a no-op.
    static void compositeOver(ImageBuffer& base, ImageBuffer const& top);

    static cocos2d::CCTexture2D* createTexture(ImageBuffer const& image);

    static cocos2d::CCSprite* createSprite(ImageBuffer const& image);

private:
    SpritePreviewRenderer() = delete;
};

}  // namespace paimon::texture_studio
