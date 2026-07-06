#pragma once

#include "../data/ImageBuffer.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// One cluster: its centroid in HSV + RGB form, plus aggregate statistics.
struct ColorCluster {
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;

    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    int pixelCount = 0;

    // Fraction of this cluster's pixels touching the silhouette edge.
    // Computed by the classifier; 0.0 from the clusterer itself.
    float borderRatio = 0.0f;
};

// Final clustering result for one sprite.
struct ClusterSet {
    std::vector<ColorCluster> clusters;
    int totalPixels = 0;
    int rejected    = 0;
};

struct ClusteringOptions {
    int k = 5;
    int alphaCutoff = 16;
    int maxIterations = 40;
    float epsilon = 0.25f;

    // HSV distance weights; hue dominates the role assignment.
    float weightH = 0.5f;
    float weightS = 0.3f;
    float weightV = 0.2f;

    // Weight each pixel by its alpha so anti-aliased edge pixels (which mix
    // the sprite color with the transparent background) stop dragging the
    // centroids toward muddy in-between colors.
    bool alphaWeighting = true;

    // Independent k-means runs with different seeds; the run with the lowest
    // weighted inertia wins. Protects against a bad k-means++ draw.
    int restarts = 2;

    // After convergence, centroids closer than this (weighted HSV distance)
    // are merged. Splitting one visual color into two clusters makes the
    // classifier assign the same surface to two different roles. Kept small:
    // dark outline vs dark accent colors sit ~0.015 apart and must survive.
    float mergeThreshold = 0.012f;

    // Iterating on every pixel of a large sprite is wasted work; a stride
    // subsample this size estimates the same centroids. Counts are still
    // computed over all pixels afterwards. 0 = no cap.
    int maxSamples = 24000;
};

class ColorClustering final {
public:
    static ClusterSet compute(ImageBuffer const& sprite,
                              ClusteringOptions options = {});

    // Weighted HSV distance; hue uses circular distance (350° and 10° = 20°).
    static float hsvDistance(float h1, float s1, float v1,
                             float h2, float s2, float v2,
                             float wh = 0.5f, float ws = 0.3f, float wv = 0.2f);

private:
    ColorClustering() = delete;
};

}  // namespace paimon::texture_studio
