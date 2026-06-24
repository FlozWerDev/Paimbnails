#pragma once

// Color formatting helpers for the editor color picker.
// Produces strings in formats that are useful and Geometry-Dash-compatible:
//   - HEX  : "#RRGGBB"        (universal, also accepted by GD's color hex field)
//   - RGB  : "R, G, B"        (decimal triplet)
//   - GD   : "RRGGBB"         (no '#', exactly what GD's color picker hex input wants)
//   - HSV  : "H, S%, V%"      (hue 0-360, saturation/value 0-100, informational)

#include <Geode/Geode.hpp>
#include <string>
#include <algorithm>
#include <cmath>

namespace paimon::editorcp {

enum class ColorFmt : int {
    Hex   = 0, // #RRGGBB
    Rgb   = 1, // R, G, B
    GdHex = 2, // RRGGBB
    Hsv   = 3, // H, S%, V%
};

inline constexpr int kFormatCount = 4;

// Short display name for the format selector.
inline const char* formatName(int index) {
    switch (index) {
        case 1:  return "RGB";
        case 2:  return "GD HEX";
        case 3:  return "HSV";
        case 0:
        default: return "HEX";
    }
}

// Convert an RGB triplet to an "H, S%, V%" string (hue 0-360, sat/val 0-100).
inline std::string formatHsv(cocos2d::ccColor3B c) {
    const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d  = mx - mn;

    float h = 0.f;
    if (d > 1e-6f) {
        if      (mx == r) h = 60.f * std::fmod(((g - b) / d), 6.f);
        else if (mx == g) h = 60.f * (((b - r) / d) + 2.f);
        else              h = 60.f * (((r - g) / d) + 4.f);
    }
    if (h < 0.f) h += 360.f;

    const float s = (mx <= 0.f) ? 0.f : (d / mx);
    const float v = mx;
    return fmt::format("{}, {}%, {}%",
                       (int)std::lround(h),
                       (int)std::lround(s * 100.f),
                       (int)std::lround(v * 100.f));
}

// Format a color according to the selected format index.
inline std::string formatColor(cocos2d::ccColor3B c, int index) {
    switch (index) {
        case 1:  return fmt::format("{}, {}, {}", (int)c.r, (int)c.g, (int)c.b);
        case 2:  return fmt::format("{:02X}{:02X}{:02X}", (int)c.r, (int)c.g, (int)c.b);
        case 3:  return formatHsv(c);
        case 0:
        default: return fmt::format("#{:02X}{:02X}{:02X}", (int)c.r, (int)c.g, (int)c.b);
    }
}

inline int clampFormatIndex(int index) {
    if (index < 0 || index >= kFormatCount) return 0;
    return index;
}

} // namespace paimon::editorcp
