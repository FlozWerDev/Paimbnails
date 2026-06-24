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

    // Pick the luminance to centre the tint on: the brightest of the
    // *colored* roles (Color1/Color2/Glow), weighted toward the dominant
    // Color1 cluster. Outline is excluded — it's never tinted, so its
    // luminance shouldn't drag the exposure.
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
        // Color1 is the dominant fill: give it priority by treating its
        // luminance as the anchor even if a thin glow is brighter.
        if (c.role == ClusterRole::Color1) {
            targetLum = std::max(targetLum, lum);
        } else if (targetLum < 0.0f) {
            // Only let secondary roles set the anchor when there's no
            // Color1 at all.
            targetLum = std::max(targetLum, lum * 0.9f);
        }
    }
    if (!haveColored || targetLum < 0.0f) return result;

    // Setting brightness == target luminance makes the dominant fill map to
    // factor ≈ 1.0 (full user color, no saturation rescale). Clamp to the
    // tinter's supported range; PackGen recommends ~100..300.
    int tuned = std::clamp(static_cast<int>(std::lround(targetLum)), 100, 300);

    result.suggestedBrightness = tuned;
    result.options.brightness  = tuned;
    result.changed = std::abs(tuned - base.brightness) >= 6;
    return result;
}

}  // namespace paimon::texture_studio
