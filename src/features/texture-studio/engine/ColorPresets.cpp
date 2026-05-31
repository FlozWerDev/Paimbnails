#include "ColorPresets.hpp"

namespace paimon::texture_studio {

std::vector<ColorPreset> const& ColorPresets::list() {
    static std::vector<ColorPreset> const kList = {
        // Default — PackGen's stock greens & cyan.
        {"Default",   {149, 226, 3},   {28,  233, 255}, {255, 255, 255}, 160},
        // Neon — vivid purples and pinks, white glow.
        {"Neon",      {255,  64, 220}, {64,  220, 255}, {255, 255, 255}, 130},
        // Pastel — softer, low-saturation tones with warm glow.
        {"Pastel",    {255, 170, 170}, {170, 220, 255}, {255, 240, 220}, 200},
        // Vaporwave — magenta + teal, cyan glow.
        {"Vaporwave", {255,  85, 200}, {0,   200, 200}, {200, 255, 255}, 140},
        // Mono Lime — single hue across both colors, near-white glow.
        {"Mono Lime", {180, 255, 100}, {110, 200,  60}, {240, 255, 220}, 170},
        // Sunset — warm oranges + dusky purple, gold glow.
        {"Sunset",    {255, 140,  60}, {120,  60, 130}, {255, 220, 120}, 150},
    };
    return kList;
}

}  // namespace paimon::texture_studio
