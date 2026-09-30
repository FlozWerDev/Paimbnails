#pragma once

#include "../data/ImageBuffer.hpp"
#include "ClusterClassifier.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// one r8 mask in imagebuffer's row-major, top-left layout.
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

// per-role masks plus detail: enclosed glow components leave the outer ring to keep original color.
struct MaskSet {
    MaskBuffer color1;
    MaskBuffer color2;
    MaskBuffer glow;
    MaskBuffer outline;
    MaskBuffer detail;

    MaskBuffer&       get(ClusterRole r);
    MaskBuffer const& get(ClusterRole r) const;
};

// grayscale opening kills isolated aa specks; off keeps packgen bit-exact exports.
struct MaskMorphology {
    int erode  = 0;
    int dilate = 0;

    bool enabled() const { return erode > 0 || dilate > 0; }
};

struct MaskBuilderOptions {
    // 0 = hard, 1 = full soft; clear pixels stay pure.
    float softness = 0.0f;

    int alphaCutoff = 16;

    MaskMorphology morphology{};

    // edge-aware 3x3: absorbs flat speckles, keeps color edges and alpha coverage.
    int edgeRefine = 0;

    // enclosed glow goes to detail: glow must not repaint inner white glyphs.
    bool separateInteriorGlow = true;
};

class MaskBuilder final {
public:
    static MaskSet build(ImageBuffer const& sprite,
                         ClassifiedSet const& classified,
                         MaskBuilderOptions options = {});

private:
    MaskBuilder() = delete;
};

}
