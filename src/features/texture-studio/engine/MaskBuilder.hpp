#pragma once
//
// MaskBuilder.hpp - Converts a ClassifiedSet into per-role coverage masks
// (one R8 mask per role: Color 1, Color 2, Glow, Outline). Each mask shares
// the dimensions of the source sprite.
//
// Why R8 (single byte per pixel) instead of float?
//   - The luminance tinter only needs values in [0, 1] anyway.
//   - 4 masks of a 256x256 sprite = 256KB; 4× smaller than float32 makes
//     ManualOverride storage cheaper to persist.
//   - Manual paint operations work on bytes natively (a brush pixel is just
//     "set this byte to 255 / 0").
//
// Soft assignment vs hard assignment:
//   - Hard: each pixel belongs to exactly one cluster (its nearest), gets
//     mask value = 255 in that cluster's role mask, 0 elsewhere.
//   - Soft: distribute weight across nearest-2 clusters by inverse distance.
//
// We use HARD assignment by default — it's faster, deterministic, and gives
// crisp results that match what the user "sees" when picking colors. Soft
// assignment is opt-in via MaskBuilderOptions.softness ∈ (0, 1].
//

#include "../data/ImageBuffer.hpp"
#include "ClusterClassifier.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// One R8 mask. Pixel layout matches ImageBuffer (row-major, top-left origin).
struct MaskBuffer {
    int width  = 0;
    int height = 0;
    std::vector<std::uint8_t> data;  // size = width * height; 0 = no contribution

    bool empty() const { return width <= 0 || height <= 0; }

    std::uint8_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x];
    }
    void setAt(int x, int y, std::uint8_t v) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        data[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = v;
    }
};

// Set of 4 masks, one per role. Index by ClusterRole (-1 because Unassigned
// gets folded into Color 1 by the classifier, so we never see it here).
struct MaskSet {
    MaskBuffer color1;
    MaskBuffer color2;
    MaskBuffer glow;
    MaskBuffer outline;

    // Convenience: get a mask by role.
    MaskBuffer&       get(ClusterRole r);
    MaskBuffer const& get(ClusterRole r) const;
};

struct MaskBuilderOptions {
    // 0.0 = pure hard assignment (default). 1.0 = full soft (split between
    // top-2 nearest clusters). Most users want 0.0; the option exists for
    // future "anti-alias" smoothing.
    float softness = 0.0f;

    // Pixels with alpha below this contribute nothing to any mask. Matches
    // the threshold in ColorClustering / ClusterClassifier.
    int alphaCutoff = 16;
};

class MaskBuilder final {
public:
    // Build masks from a classified set + the source sprite. The masks
    // returned have the same dimensions as the sprite. Roles that have no
    // assigned cluster come back as all-zero masks (the tinter handles
    // them as "no contribution").
    static MaskSet build(ImageBuffer const& sprite,
                         ClassifiedSet const& classified,
                         MaskBuilderOptions options = {});

private:
    MaskBuilder() = delete;
};

}  // namespace paimon::texture_studio
