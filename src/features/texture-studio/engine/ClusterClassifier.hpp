#pragma once

#include "ColorClustering.hpp"
#include "../data/ImageBuffer.hpp"

#include <array>
#include <cstdint>

namespace paimon::texture_studio {

// Logical role of a cluster.
enum class ClusterRole : std::uint8_t {
    Unassigned = 0,
    Outline,
    Color1,
    Color2,
    Glow,
};

// Classified cluster — same fields as ColorCluster + the assigned role.
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

    // Of all pixels assigned to this cluster, the fraction (0..1) with at
    // least one fully-transparent 4-neighbour. Public for tests.
    static float computeBorderRatio(ImageBuffer const& sprite,
                                    ColorCluster const& cluster,
                                    ColorCluster const* allClusters,
                                    int clusterCount,
                                    int targetIndex);

private:
    ClusterClassifier() = delete;
};

}  // namespace paimon::texture_studio
