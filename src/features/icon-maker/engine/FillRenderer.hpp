#pragma once

#include "../data/FillSpec.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>

namespace paimon::icon_maker {

class FillRenderer final {
public:
    // the shape's alpha bounds map the fill; its alpha stays in the output.
    // `canvasScale` is canvasSize / referenceCanvas (1.0 at 240px UHD).
    static geode::Result<texture_studio::ImageBuffer> apply(
        texture_studio::ImageBuffer const& shape,
        FillSpec const& fill,
        std::filesystem::path const& imagesDir,
        float canvasScale = 1.f);

    // alpha bounding box; false when the buffer is fully transparent.
    static bool alphaBounds(texture_studio::ImageBuffer const& buffer,
                            int& outX, int& outY, int& outW, int& outH);

    // layerOpacity is already in shape; apply it to the outline so both fade together.
    static texture_studio::ImageBuffer renderOutline(
        texture_studio::ImageBuffer const& shape,
        OutlineSpec const& outline,
        int layerOpacity,
        float canvasScale = 1.f);

    // soft outer glow or drop shadow behind the shape.
    static texture_studio::ImageBuffer renderShadow(
        texture_studio::ImageBuffer const& shape,
        ShadowSpec const& shadow,
        int layerOpacity,
        float canvasScale = 1.f);

    // blend `top` over `base` using the selected layer blend mode.
    static void compositeBlend(
        texture_studio::ImageBuffer& base,
        texture_studio::ImageBuffer const& top,
        BlendMode mode);

private:
    FillRenderer() = delete;
};

}  // namespace paimon::icon_maker
