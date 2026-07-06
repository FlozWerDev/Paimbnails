#include "AutoTuner.hpp"

#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

// Rec.601 luminance in the same 0..255 scale the tinter divides by.
float luminance601(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return 0.30f * static_cast<float>(r)
         + 0.59f * static_cast<float>(g)
         + 0.11f * static_cast<float>(b);
}

}  // anonymous namespace

AutoTuner::Suggestion AutoTuner::tuneForSprite(ImageBuffer const& framePixels,
                                               SpritePreviewOptions const& base) {
    Suggestion result;
    result.options = base;
    result.suggestedBrightness = base.brightness;
    if (framePixels.empty()) return result;

    auto clusters   = ColorClustering::compute(framePixels);
    auto classified = ClusterClassifier::classify(clusters, framePixels);
    if (classified.clusters.empty()) return result;

    // Centre the tint on the brightest colored role (Color1/Color2/Glow),
    // weighted toward dominant Color1. Outline is excluded — it's never tinted.
    float targetLum = -1.0f;
    bool  haveColored = false;
    for (auto const& c : classified.clusters) {
        if (c.role != ClusterRole::Color1 &&
            c.role != ClusterRole::Color2 &&
            c.role != ClusterRole::Glow) {
            continue;
        }
        haveColored = true;
        float lum = luminance601(c.source.r, c.source.g, c.source.b);
        if (c.role == ClusterRole::Color1) {
            targetLum = std::max(targetLum, lum);
        } else if (targetLum < 0.0f) {
            targetLum = std::max(targetLum, lum * 0.9f);
        }
    }
    if (!haveColored || targetLum < 0.0f) return result;

    // brightness == target luminance maps the dominant fill to factor ≈ 1.0
    // (full user color). PackGen supports ~100..300.
    int tuned = std::clamp(static_cast<int>(std::lround(targetLum)), 100, 300);

    result.suggestedBrightness = tuned;
    result.options.brightness  = tuned;
    result.changed = std::abs(tuned - base.brightness) >= 6;
    return result;
}

}  // namespace paimon::texture_studio
