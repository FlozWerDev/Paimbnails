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

    ClusteringOptions copts;
    copts.k = std::clamp(options.clusterPrecision, 2, 10);
    auto clusters   = ColorClustering::compute(framePixels, copts);
    auto classified = ClusterClassifier::classify(clusters, framePixels);

    MaskBuilderOptions mopts;
    mopts.softness   = options.maskSoftness;
    mopts.edgeRefine = std::clamp(options.edgeCleanup, 0, 4);
    auto masks = MaskBuilder::build(framePixels, classified, mopts);

    TinterOptions topts;
    topts.brightness             = options.brightness;
    topts.alternativeGlowOverlay = options.alternativeGlowOverlay;
    topts.darkOutlineThreshold   = std::clamp(options.outlineProtect, 0, 255);
    topts.saturation             = options.saturation;
    topts.contrast               = options.contrast;
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

    // Flag tiny Color1 coverage for review even when the classifier was confident.
    if (visiblePixels > 0 && result.stats.color1Coverage < 0.01f) {
        result.stats.needsReview = true;
    }
    return result;
}

ImageBuffer SpritePreviewRenderer::renderCustomImage(ImageBuffer const& userImage,
                                                     int frameW, int frameH,
                                                     ImageTransform const& transform,
                                                     float pixelOffsetX,
                                                     float pixelOffsetY) {
    if (userImage.empty() || frameW <= 0 || frameH <= 0) return ImageBuffer();

    float imgW = static_cast<float>(userImage.width());
    float imgH = static_cast<float>(userImage.height());
    float fw   = static_cast<float>(frameW);
    float fh   = static_cast<float>(frameH);

    // Base scale per fit mode, then the user multiplier on top.
    float sx = 1.0f, sy = 1.0f;
    switch (transform.fitMode) {
        case ImageFitMode::Fill:
            sx = sy = std::max(fw / imgW, fh / imgH);
            break;
        case ImageFitMode::Stretch:
            sx = fw / imgW;
            sy = fh / imgH;
            break;
        case ImageFitMode::Fit:
        default:
            sx = sy = std::min(fw / imgW, fh / imgH);
            break;
    }
    float userScale = std::clamp(transform.scale, 0.01f, 32.0f);
    sx *= userScale;
    sy *= userScale;
    if (sx <= 0.0f || sy <= 0.0f) return ImageBuffer();

    // Image centre in canvas space. offsetY is "positive = up" in the UI;
    // pixel rows grow downward, hence the minus. Clamp ±2 = full frame shift.
    float cx = fw * 0.5f + std::clamp(transform.offsetX, -2.0f, 2.0f) * fw * 0.5f
             + pixelOffsetX;
    float cy = fh * 0.5f - std::clamp(transform.offsetY, -2.0f, 2.0f) * fh * 0.5f
             + pixelOffsetY;

    float rad  = transform.rotationDeg * (3.14159265358979323846f / 180.0f);
    float cosR = std::cos(rad);
    float sinR = std::sin(rad);

    float alphaMul = std::clamp(transform.opacity, 0, 255) / 255.0f;

    ImageBuffer canvas(frameW, frameH);
    if (alphaMul <= 0.0f) return canvas;

    auto* dst = canvas.data();
    auto const* src = userImage.data();
    int iw = userImage.width();
    int ih = userImage.height();

    // Inverse mapping: for each canvas pixel, un-rotate/un-scale back into
    // image space and bilinear-sample. No intermediate resize → one single
    // resampling step, maximum sharpness.
    for (int y = 0; y < frameH; ++y) {
        for (int x = 0; x < frameW; ++x) {
            float dx = (static_cast<float>(x) + 0.5f) - cx;
            float dy = (static_cast<float>(y) + 0.5f) - cy;

            // Inverse of a clockwise screen-space rotation.
            float ux = dx * cosR + dy * sinR;
            float uy = -dx * sinR + dy * cosR;

            float sxf = ux / sx + imgW * 0.5f;
            float syf = uy / sy + imgH * 0.5f;
            if (transform.flipX) sxf = imgW - sxf;
            if (transform.flipY) syf = imgH - syf;

            float fx = sxf - 0.5f;
            float fy = syf - 0.5f;
            int x0 = static_cast<int>(std::floor(fx));
            int y0 = static_cast<int>(std::floor(fy));
            if (x0 < -1 || y0 < -1 || x0 > iw - 1 || y0 > ih - 1) continue;

            float tx = fx - static_cast<float>(x0);
            float ty = fy - static_cast<float>(y0);

            auto sample = [&](int px, int py, float weight, float* acc) {
                if (px < 0 || py < 0 || px >= iw || py >= ih || weight <= 0.0f) return;
                auto const* p = src +
                    (static_cast<std::size_t>(py) * iw + px) * ImageBuffer::kBytesPerPixel;
                float a = static_cast<float>(p[3]) * weight;
                // Alpha-weighted color accumulation avoids dark halos where
                // opaque pixels border fully transparent ones.
                acc[0] += static_cast<float>(p[0]) * a;
                acc[1] += static_cast<float>(p[1]) * a;
                acc[2] += static_cast<float>(p[2]) * a;
                acc[3] += a;
            };

            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            sample(x0,     y0,     (1.0f - tx) * (1.0f - ty), acc);
            sample(x0 + 1, y0,     tx * (1.0f - ty),          acc);
            sample(x0,     y0 + 1, (1.0f - tx) * ty,          acc);
            sample(x0 + 1, y0 + 1, tx * ty,                   acc);

            if (acc[3] <= 0.0f) continue;
            float outA = acc[3] * alphaMul;
            if (outA < 0.5f) continue;
            float invA = 1.0f / acc[3];

            auto* d = dst + (static_cast<std::size_t>(y) * frameW + x)
                          * ImageBuffer::kBytesPerPixel;
            d[0] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(acc[0] * invA)), 0, 255));
            d[1] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(acc[1] * invA)), 0, 255));
            d[2] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(acc[2] * invA)), 0, 255));
            d[3] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(outA)), 0, 255));
        }
    }

    return canvas;
}

void SpritePreviewRenderer::compositeOver(ImageBuffer& base, ImageBuffer const& top) {
    if (base.empty() || top.empty()) return;
    if (base.width() != top.width() || base.height() != top.height()) return;

    auto* b = base.data();
    auto const* t = top.data();
    std::size_t n = base.pixelCount();
    for (std::size_t i = 0; i < n; ++i) {
        auto* bp = b + i * ImageBuffer::kBytesPerPixel;
        auto const* tp = t + i * ImageBuffer::kBytesPerPixel;
        int ta = tp[3];
        if (ta <= 0) continue;
        if (ta >= 255) {
            bp[0] = tp[0]; bp[1] = tp[1]; bp[2] = tp[2]; bp[3] = 255;
            continue;
        }
        float topA  = static_cast<float>(ta) / 255.0f;
        float baseA = static_cast<float>(bp[3]) / 255.0f;
        float outA  = topA + baseA * (1.0f - topA);
        if (outA <= 0.0f) {
            bp[0] = bp[1] = bp[2] = bp[3] = 0;
            continue;
        }
        float invA = 1.0f / outA;
        for (int c = 0; c < 3; ++c) {
            float v = (static_cast<float>(tp[c]) * topA +
                       static_cast<float>(bp[c]) * baseA * (1.0f - topA)) * invA;
            bp[c] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(v)), 0, 255));
        }
        bp[3] = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(outA * 255.0f)), 0, 255));
    }
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
