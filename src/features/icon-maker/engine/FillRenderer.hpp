#pragma once

#include "../data/FillSpec.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>

namespace paimon::icon_maker {

class FillRenderer final {
public:
    // The shape's alpha bounds map the fill; its alpha stays in the output.
    static geode::Result<texture_studio::ImageBuffer> apply(
        texture_studio::ImageBuffer const& shape,
        FillSpec const& fill,
        std::filesystem::path const& imagesDir);

    // Alpha bounding box; false when the buffer is fully transparent.
    static bool alphaBounds(texture_studio::ImageBuffer const& buffer,
                            int& outX, int& outY, int& outW, int& outH);

    // layerOpacity is already in shape; apply it to the outline so both fade together.
    static texture_studio::ImageBuffer renderOutline(
        texture_studio::ImageBuffer const& shape,
        OutlineSpec const& outline,
        int layerOpacity);

private:
    FillRenderer() = delete;
};

}  // namespace paimon::icon_maker
