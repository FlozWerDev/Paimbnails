#pragma once
// El "balde de pintura": pinta un relleno (color, degradado o imagen) a
// través del alpha de la forma de una pieza, opcionalmente conservando el
// sombreado (luminancia) de la forma original.

#include "../data/FillSpec.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>

namespace paimon::icon_maker {

class FillRenderer final {
public:
    // `shape` is the piece already placed on its canvas. Returns a same-size
    // buffer where alpha comes from the shape and color from the fill,
    // mapped onto the shape's alpha bounding box.
    static geode::Result<texture_studio::ImageBuffer> apply(
        texture_studio::ImageBuffer const& shape,
        FillSpec const& fill,
        std::filesystem::path const& imagesDir);

    // Alpha bounding box; false when the buffer is fully transparent.
    static bool alphaBounds(texture_studio::ImageBuffer const& buffer,
                            int& outX, int& outY, int& outW, int& outH);

    // Contour grown outwards from `shape`'s alpha, to be composited *under*
    // the painted shape. `layerOpacity` (0..255) is the piece opacity already
    // baked into `shape`, needed both to find the silhouette and to keep the
    // outline as translucent as the layer it belongs to.
    static texture_studio::ImageBuffer renderOutline(
        texture_studio::ImageBuffer const& shape,
        OutlineSpec const& outline,
        int layerOpacity);

private:
    FillRenderer() = delete;
};

}  // namespace paimon::icon_maker
