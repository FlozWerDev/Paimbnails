#pragma once

#include "../data/ImageBuffer.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// one cluster: its centroid in hsv + rgb form, plus aggregate statistics.
struct ColorCluster {
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;

    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    int pixelCount = 0;

    // fraction touching the silhouette edge; set by the classifier, 0.0 from the clusterer.
    float borderRatio = 0.0f;
};

// final clustering result for one sprite.
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

    // hsv distance weights; hue dominates the role assignment.
    float weightH = 0.5f;
    float weightS = 0.3f;
    float weightV = 0.2f;

    // alpha-weighted pixels: aa edges stop dragging centroids to muddy middles.
    bool alphaWeighting = true;

    // independent seeded runs; lowest inertia wins, guarding bad k-means++ draws.
    int restarts = 2;

    // merge near-duplicate centroids; kept small so dark outline vs accent (~0.015 apart) survive.
    float mergeThreshold = 0.012f;

    // stride subsample cap for big sprites; full-pixel counts still computed after. 0 = no cap.
    int maxSamples = 24000;
};

class ColorClustering final {
public:
    static ClusterSet compute(ImageBuffer const& sprite,
                              ClusteringOptions options = {});

    // weighted hsv distance; hue uses circular distance (350° and 10° = 20°).
    static float hsvDistance(float h1, float s1, float v1,
                             float h2, float s2, float v2,
                             float wh = 0.5f, float ws = 0.3f, float wv = 0.2f);

private:
    ColorClustering() = delete;
};

}  // namespace paimon::texture_studio
