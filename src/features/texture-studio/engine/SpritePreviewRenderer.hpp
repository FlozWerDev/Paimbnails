#pragma once

#include "../data/ImageBuffer.hpp"
#include "LuminanceTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

struct SpritePreviewOptions {
    TintColors colors{};
    int   brightness = 160;
    bool  alternativeGlowOverlay = false;
    float maskSoftness = 0.35f;

    // Live previews enable a gentle morphological open on the masks to hide
    // clustering speckle on anti-aliased edges. The export path leaves this
    // off (it never goes through SpritePreviewRenderer), so generated packs
    // stay bit-exact with PackGen.
    bool  denoiseMasks = true;
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

    static ImageBuffer renderCustomImage(ImageBuffer const& userImage,
                                         int frameW, int frameH);

    static cocos2d::CCTexture2D* createTexture(ImageBuffer const& image);

    static cocos2d::CCSprite* createSprite(ImageBuffer const& image);

private:
    SpritePreviewRenderer() = delete;
};

}  // namespace paimon::texture_studio
