#pragma once
//
// ClusterClassifier.hpp - Assigns semantic roles to a ClusterSet so the
// MaskBuilder knows which color the user wants to recolor with which input
// (Color 1, Color 2, or Glow), and which to leave untouched (Outline).
//
// The classifier looks at three features per cluster:
//
//   1. Average value (V)        — distinguishes "dark stuff" from "bright stuff"
//   2. Average saturation (S)   — distinguishes "grey/black/white" from "colored"
//   3. Border ratio             — fraction of the cluster's pixels that touch
//                                 the silhouette edge (transparent neighbour).
//                                 Glow-like layers tend to ring the sprite.
//
// The role assignment is score-based (not a fixed rule cascade): every
// cluster is scored for each role and roles are filled greedily under their
// constraints. This resolves ties by evidence strength instead of rule
// order. Thresholds are adaptive (scaled by the sprite's median value) so
// dark and bright sheets are treated alike.
//
//   Outline (>=0): score = 0.50*darkness + 0.35*(1-saturation)
//                          + 0.15*borderRatio, gated by silhouette contact.
//                  Every cluster above the bar is preserved untinted; if
//                  that would take them all, only the darkest is kept.
//   Glow (<=1):    score = 0.45*brightness + 0.25*borderRatio
//                          + 0.20*(1-saturation) + 0.10*(1-pixelShare),
//                  gated by an adaptive brightness floor. Highest wins.
//   Color 1 (1):   saliency = pixelCount * (0.4 + 0.6*saturation). Highest
//                  remaining cluster — the dominant inner fill.
//   Color 2 (<=1): pixelCount weighted by hue distance from Color 1, so the
//                  two user colors stay visually separable.
//   Leftovers:     folded into the nearest assigned role by HSV distance.
//
// Real-world behaviour for the Play button screenshot:
//   greys (S≈0, V≈0.7) → Color 1     (the four little squares)
//   black (S≈0, V≈0.05) → Color 2    (the central body — falls through to D)
//   purple (S≈0.5, V≈0.6, on edge) → Glow
//   pure black outline (V≈0, S≈0) → Outline (rule A)
//
// If the input has < 4 clusters we still produce a valid assignment by
// duplicating Color 1 wherever needed; a sprite with only 2 colors gets
// outline + C1 and the C2/Glow masks come out empty (which the tinter
// handles fine — no contribution from a zero mask).
//

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
    ColorCluster source;       // copy of the original cluster (HSV + RGB + count)
    ClusterRole  role = ClusterRole::Unassigned;

    // Helpful for the manual-edit UI: how confident the classifier is in
    // this assignment (0..1). Low confidence sprites get a "needs review"
    // flag in the editor grid.
    float confidence = 0.0f;
};

// Result of classification.
struct ClassifiedSet {
    std::vector<ClassifiedCluster> clusters;

    // True if the classifier could not unambiguously assign all four
    // roles. Sprites with this flag light up in the editor with a "⚠"
    // badge so the user knows to inspect manually.
    bool needsReview = false;
};

class ClusterClassifier final {
public:
    // Assign roles to the clusters in `set`, using the sprite's pixels to
    // compute borderRatio. Returns a parallel ClassifiedSet (does not
    // mutate the input).
    static ClassifiedSet classify(ClusterSet const& set,
                                  ImageBuffer const& sprite);

    // Compute the border ratio for one cluster: of all pixels assigned to
    // this cluster, what fraction has at least one fully-transparent
    // neighbour (within Manhattan distance 1)? Returned as 0..1.
    //
    // Public so unit tests / debug tools can call it without going
    // through the full classify() pipeline.
    static float computeBorderRatio(ImageBuffer const& sprite,
                                    ColorCluster const& cluster,
                                    ColorCluster const* allClusters,
                                    int clusterCount,
                                    int targetIndex);

private:
    ClusterClassifier() = delete;
};

}  // namespace paimon::texture_studio
