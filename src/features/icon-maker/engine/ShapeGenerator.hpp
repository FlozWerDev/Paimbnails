#pragma once
// Analytical SDF rasterizer for built-in procedural shapes and GD-style decals.

#include "../data/IconProject.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"

#include <string_view>
#include <vector>

namespace paimon::icon_maker {

struct PrimitiveInfo {
    PrimitiveKind kind;
    std::string_view name;
    bool hasCornerRadius = false;
    bool hasPoints = false;
    bool hasInnerRatio = false;
};

struct DecalInfo {
    DecalKind kind;
    std::string_view name;
    std::string_view category;
};

class ShapeGenerator final {
public:
    static std::vector<PrimitiveInfo> const& primitives();
    static std::vector<DecalInfo> const& decals();

    static PrimitiveInfo const* infoFor(PrimitiveKind kind);
    static DecalInfo const* infoFor(DecalKind kind);

    // Rasterize a procedural primitive or decal into a crisp anti-aliased RGBA
    // buffer of size `size x size`. White RGB with smooth alpha coverage and
    // subtle 3D inner-shading luminance so `keepLuminance` works naturally.
    static texture_studio::ImageBuffer rasterize(PieceShape const& shape, int size);

    // Quick preview icon for ShapePickerPopup (small crisp white silhouette).
    static texture_studio::ImageBuffer rasterizePrimitive(PrimitiveKind kind, int size,
                                                          bool hollow = false);
    static texture_studio::ImageBuffer rasterizeDecal(DecalKind kind, int size);

private:
    ShapeGenerator() = delete;
};

}  // namespace paimon::icon_maker
