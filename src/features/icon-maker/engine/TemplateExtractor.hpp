#pragma once

#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace paimon::icon_maker {

struct TemplateFrame {
    // suffix after the sheet base name, without ".png": "_001", "_2_001",
    // "_02_glow_001"...
    std::string suffix;
    texture_studio::ImageBuffer pixels;  // canvassize×canvassize, art centered at uhd scale
};

class TemplateExtractor final {
public:
    // sheet base for a vanilla icon, e.g. (robot, 3) -> "robot_03".
    static std::string sheetBase(IconType type, int iconId);

    // all frames of one vanilla icon, each embedded centered (native uhd
    // pixel scale, honoring plist offsets) in a canvassize×canvassize buffer.
    static geode::Result<std::vector<TemplateFrame>> extract(
        IconType type, int iconId, int canvasSize);

    // single frame variant; `suffix` as in templateframe::suffix.
    static geode::Result<texture_studio::ImageBuffer> extractFrame(
        IconType type, int iconId, std::string_view suffix, int canvasSize);

private:
    TemplateExtractor() = delete;
};

}  // namespace paimon::icon_maker
