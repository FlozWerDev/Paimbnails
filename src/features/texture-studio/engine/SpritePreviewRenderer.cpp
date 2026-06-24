#include "SpritePreviewRenderer.hpp"

#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "MaskBuilder.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {

ImageBuffer SpritePreviewRenderer::renderTinted(ImageBuffer const& framePixels,
                                                SpritePreviewOptions const& options) {
    return renderTintedWithStats(framePixels, options).image;
}

SpritePreviewResult SpritePreviewRenderer::renderTintedWithStats(
    ImageBuffer const& framePixels,
    SpritePreviewOptions const& options) {

    SpritePreviewResult result;
    if (framePixels.empty()) return result;

    auto clusters   = ColorClustering::compute(framePixels);
    auto classified = ClusterClassifier::classify(clusters, framePixels);

    MaskBuilderOptions mopts;
    mopts.softness = options.maskSoftness;
    if (options.denoiseMasks) {
        // Gentle open: erase 1px specks, keep large fills and their edges.
        mopts.morphology = MaskMorphology{1, 1};
    }
    auto masks = MaskBuilder::build(framePixels, classified, mopts);

    TinterOptions topts;
    topts.brightness             = options.brightness;
    topts.alternativeGlowOverlay = options.alternativeGlowOverlay;
    result.image = LuminanceTinter::apply(framePixels, masks, options.colors, topts);
    result.stats.needsReview = classified.needsReview;

    std::size_t visiblePixels = 0;
    for (std::size_t i = 0; i < framePixels.pixelCount(); ++i) {
        if (framePixels.data()[i * ImageBuffer::kBytesPerPixel + 3] >= 16) {
            ++visiblePixels;
        }
    }
    auto coverage = [visiblePixels](MaskBuffer const& mask) {
        if (visiblePixels == 0 || mask.data.empty()) return 0.f;
        double weighted = 0.0;
        for (auto value : mask.data) weighted += static_cast<double>(value) / 255.0;
        return static_cast<float>(weighted / static_cast<double>(visiblePixels));
    };
    result.stats.color1Coverage = coverage(masks.color1);
    result.stats.color2Coverage = coverage(masks.color2);
    result.stats.glowCoverage = coverage(masks.glow);
    result.stats.outlineCoverage = coverage(masks.outline);

    // A nominal Color1 role that covers almost nothing is not useful to the
    // user, even if the cluster-level classifier was otherwise confident.
    if (visiblePixels > 0 && result.stats.color1Coverage < 0.01f) {
        result.stats.needsReview = true;
    }
    return result;
}

ImageBuffer SpritePreviewRenderer::renderCustomImage(ImageBuffer const& userImage,
                                                     int frameW, int frameH) {
    if (userImage.empty() || frameW <= 0 || frameH <= 0) return ImageBuffer();

    float scale = std::min(
        static_cast<float>(frameW) / static_cast<float>(userImage.width()),
        static_cast<float>(frameH) / static_cast<float>(userImage.height()));
    int scaledW = std::max(1, static_cast<int>(std::lround(userImage.width()  * scale)));
    int scaledH = std::max(1, static_cast<int>(std::lround(userImage.height() * scale)));
    scaledW = std::min(scaledW, frameW);
    scaledH = std::min(scaledH, frameH);

    ImageBuffer scaled = userImage.resizedBilinear(scaledW, scaledH);
    if (scaled.empty()) return ImageBuffer();

    ImageBuffer canvas(frameW, frameH);
    canvas.blitOverwrite((frameW - scaledW) / 2, (frameH - scaledH) / 2, scaled);
    return canvas;
}

cocos2d::CCTexture2D* SpritePreviewRenderer::createTexture(ImageBuffer const& image) {
    if (image.empty()) return nullptr;

    auto* tex = new cocos2d::CCTexture2D();
    bool ok = tex->initWithData(
        image.data(),
        cocos2d::kCCTexture2DPixelFormat_RGBA8888,
        static_cast<unsigned int>(image.width()),
        static_cast<unsigned int>(image.height()),
        cocos2d::CCSize(static_cast<float>(image.width()),
                        static_cast<float>(image.height())));
    if (!ok) {
        delete tex;
        return nullptr;
    }
    tex->autorelease();
    return tex;
}

cocos2d::CCSprite* SpritePreviewRenderer::createSprite(ImageBuffer const& image) {
    auto* tex = createTexture(image);
    if (!tex) return nullptr;
    return cocos2d::CCSprite::createWithTexture(tex);
}

}  // namespace paimon::texture_studio
