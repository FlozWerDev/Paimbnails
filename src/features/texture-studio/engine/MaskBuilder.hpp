#pragma once

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
    std::vector<std::uint8_t> data;

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

// Masks per role, plus `detail`: glow-classified pixels that do NOT touch
// the sprite silhouette (interior white glyphs), split out spatially so the
// glow color only affects the actual outer ring.
struct MaskSet {
    MaskBuffer color1;
    MaskBuffer color2;
    MaskBuffer glow;
    MaskBuffer outline;
    MaskBuffer detail;

    MaskBuffer&       get(ClusterRole r);
    MaskBuffer const& get(ClusterRole r) const;
};

// Grayscale "open" (erode then dilate) applied per mask to remove isolated
// mis-classified specks on anti-aliased edges without shifting large
// contours.
//
// OFF by default ({0, 0}) so the export path stays bit-exact with PackGen;
// only the live preview turns it on.
struct MaskMorphology {
    int erode  = 0;
    int dilate = 0;

    bool enabled() const { return erode > 0 || dilate > 0; }
};

struct MaskBuilderOptions {
    // 0.0 = hard assignment (default). 1.0 = full soft. The split is scaled
    // by how ambiguous the pixel actually is (d0/d1 ratio), so pixels that
    // clearly belong to one cluster stay pure at any softness.
    float softness = 0.0f;

    int alphaCutoff = 16;

    MaskMorphology morphology{};

    // Edge-aware refinement passes (0 = off). Each pass re-averages the
    // masks over a 3x3 window weighted by color similarity in the source
    // sprite: speckles inside a flat region get absorbed, while mask edges
    // that coincide with color edges stay razor sharp. Mask sums are
    // renormalized to the pixel alpha, so tint coverage is preserved.
    int edgeRefine = 0;

    // Split glow into outer ring (touching transparency / image border) and
    // interior components, which move to the `detail` mask. On by default:
    // the glow color should never repaint a button's inner white glyph.
    bool separateInteriorGlow = true;
};

class MaskBuilder final {
public:
    // Build masks from a classified set + the source sprite, same size as
    // the sprite. Roles with no assigned cluster come back all-zero.
    static MaskSet build(ImageBuffer const& sprite,
                         ClassifiedSet const& classified,
                         MaskBuilderOptions options = {});

private:
    MaskBuilder() = delete;
};

}  // namespace paimon::texture_studio
