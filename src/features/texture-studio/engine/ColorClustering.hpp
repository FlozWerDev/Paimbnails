#pragma once
//
// ColorClustering.hpp - Deterministic k-means clustering in HSV space.
//
// Goal: given the pixels of one sprite, find the dominant colors that the
// algorithm should treat as separate "logical layers" (Color 1 / Color 2 /
// Glow / Outline). The classifier in ClusterClassifier.hpp consumes the
// output of this module to assign roles.
//
// Why HSV instead of RGB:
//   - Hue separates "the green of GD's menu" from "the cyan of GD's menu"
//     much more cleanly than RGB euclidean distance.
//   - Saturation tells us "this is a vivid colored layer" vs "this is a
//     grey shade" — used by the classifier to detect the Glow layer.
//   - Value is what the luminance tinter ultimately preserves, so working
//     in HSV makes the role boundaries align with the tinter's behaviour.
//
// Determinism:
//   - We seed centroids using a SplitMix64 PRNG with a fixed seed (the
//     image's pixel-count hash). Same input → same clusters every time.
//   - We early-terminate when no centroid moves more than `epsilon`.
//
// Performance:
//   - One sprite at 256x256 = 65k pixels. With k=6 and ~10 iterations the
//     clustering finishes in <5ms on a modern CPU. We don't need SIMD here.
//

#include "../data/ImageBuffer.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// One cluster: its centroid in HSV + RGB form, plus aggregate statistics.
struct ColorCluster {
    // Centroid (in HSV space, used for distance). H ∈ [0,360), S/V ∈ [0,1].
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;

    // Centroid in RGB form (cached so callers don't reconvert each call).
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    // Number of source pixels assigned to this cluster.
    int pixelCount = 0;

    // Fraction of this cluster's pixels that touch the silhouette edge of
    // the sprite (i.e. neighbour at least one fully transparent pixel).
    // A high border ratio is a strong hint that this cluster is the "glow"
    // or "outline" layer. Computed by the classifier, populated to 0.0 by
    // the clusterer itself.
    float borderRatio = 0.0f;
};

// Final clustering result for one sprite.
struct ClusterSet {
    std::vector<ColorCluster> clusters;
    int totalPixels = 0;     // total opaque pixels considered
    int rejected    = 0;     // pixels skipped (alpha < threshold)
};

struct ClusteringOptions {
    // How many clusters to compute. The classifier will pick the 3-4 most
    // useful ones (Outline, C1, C2, Glow). 5 is a good default for GD UI.
    int k = 5;

    // Pixels with alpha below this are excluded entirely (anti-alias dust).
    int alphaCutoff = 16;

    // Maximum k-means iterations. Convergence is usually <10 in practice.
    int maxIterations = 30;

    // Convergence threshold (max centroid drift across an iteration).
    float epsilon = 0.5f;

    // Distance weights in HSV. Hue dominates because the classifier's
    // hue-based role assignment is the strongest signal.
    float weightH = 0.5f;
    float weightS = 0.3f;
    float weightV = 0.2f;
};

class ColorClustering final {
public:
    // Compute clusters for the given sprite. Empty / fully-transparent
    // sprites return an empty ClusterSet. Always returns at most `options.k`
    // clusters; fewer if the image has fewer distinct colors.
    static ClusterSet compute(ImageBuffer const& sprite,
                              ClusteringOptions options = {});

    // Public for unit-test reachability and for ClusterClassifier to reuse.
    // Distance between two HSV points using the configured weights. The hue
    // component uses a circular distance (so 350° and 10° are 20° apart).
    static float hsvDistance(float h1, float s1, float v1,
                             float h2, float s2, float v2,
                             float wh = 0.5f, float ws = 0.3f, float wv = 0.2f);

private:
    ColorClustering() = delete;
};

}  // namespace paimon::texture_studio
