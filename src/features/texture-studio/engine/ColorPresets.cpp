#include "ColorPresets.hpp"

namespace paimon::texture_studio {

std::vector<ColorPreset> const& ColorPresets::list() {
    static std::vector<ColorPreset> const kList = {
        {"Default",   {149, 226, 3},   {28,  233, 255}, {255, 255, 255}, 160},
        {"Neon",      {255,  64, 220}, {64,  220, 255}, {255, 255, 255}, 130},
        {"Pastel",    {255, 170, 170}, {170, 220, 255}, {255, 240, 220}, 200},
        {"Vaporwave", {255,  85, 200}, {0,   200, 200}, {200, 255, 255}, 140},
        {"Mono Lime", {180, 255, 100}, {110, 200,  60}, {240, 255, 220}, 170},
        {"Sunset",    {255, 140,  60}, {120,  60, 130}, {255, 220, 120}, 150},
    };
    return kList;
}

}  // namespace paimon::texture_studio
