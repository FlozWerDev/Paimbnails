#pragma once

#include "../data/FillSpec.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

namespace paimon::icon_maker {

class GradientRasterizer final {
public:
    // Color at position t (0..1) along the gradient, interpolating sorted stops.
    static cocos2d::ccColor4B sample(GradientSpec const& spec, float t);

    // Normalized gradient parameter for a pixel inside a w×h region.
    static float paramAt(GradientSpec const& spec, float x, float y, float w, float h);

    // Standalone raster (used for the inspector's preview strip).
    static texture_studio::ImageBuffer rasterize(int w, int h, GradientSpec const& spec);

private:
    GradientRasterizer() = delete;
};

}  // namespace paimon::icon_maker
