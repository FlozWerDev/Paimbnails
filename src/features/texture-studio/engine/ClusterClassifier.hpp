#pragma once

#include "ColorClustering.hpp"
#include "../data/ImageBuffer.hpp"

#include <array>
#include <cstdint>

namespace paimon::texture_studio {

// logical role of a cluster.
enum class ClusterRole : std::uint8_t {
    Unassigned = 0,
    Outline,
    Color1,
    Color2,
    Glow,
    Detail,
};

// classified cluster — same fields as colorcluster + the assigned role.
struct ClassifiedCluster {
    ColorCluster source;
    ClusterRole  role = ClusterRole::Unassigned;
    float confidence = 0.0f;
};

struct ClassifiedSet {
    std::vector<ClassifiedCluster> clusters;
    bool needsReview = false;
};

class ClusterClassifier final {
public:
    static ClassifiedSet classify(ClusterSet const& set,
                                  ImageBuffer const& sprite);

    // UI gradients belong to one fill color; neutral controls still need a fill.
    static ClassifiedSet classifyUi(ClusterSet const& set,
                                    ImageBuffer const& sprite);

    // same decisions with border ratios (one per cluster) computed by the caller.
    static ClassifiedSet classifyWithRatios(ClusterSet const& set,
                                            std::vector<float> const& ratios);
    static ClassifiedSet classifyUiWithRatios(ClusterSet const& set,
                                              std::vector<float> const& ratios);

    // fraction (0..1) with a fully-transparent 4-neighbor. public for tests.
    static float computeBorderRatio(ImageBuffer const& sprite,
                                    ColorCluster const& cluster,
                                    ColorCluster const* allClusters,
                                    int clusterCount,
                                    int targetIndex);

private:
    static ClassifiedSet refineUi(ClassifiedSet classified);

    ClusterClassifier() = delete;
};

}  // namespace paimon::texture_studio
