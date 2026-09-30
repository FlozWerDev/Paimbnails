#pragma once

#include <Geode/cocos/include/ccTypes.h>

#include <string>
#include <vector>

namespace paimon::icon_maker {

// numeric values are frozen for save compatibility.
enum class FillType : int {
    Flat     = 0,
    Gradient = 1,
    Image    = 2,
};

enum class GradientKind : int {
    Linear = 0,
    Radial = 1,
};

struct GradientStop {
    float pos = 0.f;  // 0..1 along the gradient axis
    cocos2d::ccColor4B color{255, 255, 255, 255};
};

struct GradientSpec {
    GradientKind kind = GradientKind::Linear;
    float angleDeg = 0.f;   // linear: clockwise from "left to right"
    float centerX = 0.5f;   // radial center, fraction of the piece bbox
    float centerY = 0.5f;
    float radius = 0.7f;    // radial: fraction of half the bbox diagonal
    std::vector<GradientStop> stops{
        {0.f, {255, 0, 128, 255}},
        {1.f, {0, 128, 255, 255}},
    };
};

// how the fill image maps onto the piece before scale/offset/rotation.
// frozen values; tile has no counterpart in texture-studio's imagefitmode.
enum class FillFitMode : int {
    Fit     = 0,
    Fill    = 1,
    Stretch = 2,
    Tile    = 3,
};

struct ImageFillSpec {
    std::string file;  // filename inside the project's images/ dir
    FillFitMode fit = FillFitMode::Fill;
    float scale = 1.f;
    float offsetX = 0.f;   // -1..1, fraction of half the piece bbox
    float offsetY = 0.f;
    float rotationDeg = 0.f;
    int opacity = 255;
};

// contour drawn *under* the painted shape, grown outwards from its alpha.
struct OutlineSpec {
    bool enabled = false;
    // thickness in authoring-canvas pixels (the 240px uhd square), so the same
    // number holds up across the -uhd / -hd / sd exports.
    float width = 6.f;
    cocos2d::ccColor4B color{0, 0, 0, 255};
};

// the "paint bucket": what gets painted through the piece's alpha mask.
struct FillSpec {
    FillType type = FillType::Flat;

    // multiply the fill by the shape's luminance so shading survives.
    bool keepLuminance = true;

    // render white and let a runtime ticker hue-cycle the result (garage only).
    bool chroma = false;

    cocos2d::ccColor4B flat{255, 255, 255, 255};
    GradientSpec gradient{};
    ImageFillSpec image{};
    OutlineSpec outline{};
};

}  // namespace paimon::icon_maker
