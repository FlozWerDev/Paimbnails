#pragma once

#include "../data/ImageBuffer.hpp"
#include "ClusterClassifier.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// One R8 mask in ImageBuffer's row-major, top-left layout.
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

// Masks per role plus detail pixels: enclosed glow components moved out of the
// outer ring so they keep their original color.
struct MaskSet {
    MaskBuffer color1;
    MaskBuffer color2;
    MaskBuffer glow;
    MaskBuffer outline;
    MaskBuffer detail;

    MaskBuffer&       get(ClusterRole r);
    MaskBuffer const& get(ClusterRole r) const;
};

// Per-mask grayscale opening removes isolated AA specks without shifting large
// contours. Off by default to keep exports bit-exact with PackGen.
struct MaskMorphology {
    int erode  = 0;
    int dilate = 0;

    bool enabled() const { return erode > 0 || dilate > 0; }
};

struct MaskBuilderOptions {
// 0 = hard, 1 = full soft; ambiguity scales the split so clear pixels stay pure.
    float softness = 0.0f;

    int alphaCutoff = 16;

    MaskMorphology morphology{};

// Edge-aware 3x3 refinement absorbs flat-region speckles while preserving color
// edges and pixel-alpha coverage.
    int edgeRefine = 0;

// Move enclosed glow components to detail; the glow color should not repaint
// inner white glyphs.
    bool separateInteriorGlow = true;
};

class MaskBuilder final {
public:
// Build same-size masks from cluster assignments and the source sprite.
    static MaskSet build(ImageBuffer const& sprite,
                         ClassifiedSet const& classified,
                         MaskBuilderOptions options = {});

private:
    MaskBuilder() = delete;
};

}
