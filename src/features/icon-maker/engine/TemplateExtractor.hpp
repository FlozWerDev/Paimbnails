#pragma once
// Extrae frames de los iconos oficiales de GD como piezas editables. Cada
// frame vanilla ya ES un canal de color (principal/secundario/brillo/extra),
// así que con fill blanco + conservar sombreado queda recolorable al instante.

#include "../../texture-studio/data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace paimon::icon_maker {

struct TemplateFrame {
    // Suffix after the sheet base name, without ".png": "_001", "_2_001",
    // "_02_glow_001"...
    std::string suffix;
    texture_studio::ImageBuffer pixels;  // canvasSize×canvasSize, art centered at UHD scale
};

class TemplateExtractor final {
public:
    // Sheet base for a vanilla icon, e.g. (Robot, 3) -> "robot_03".
    static std::string sheetBase(IconType type, int iconId);

    // All frames of one vanilla icon, each embedded centered (native UHD
    // pixel scale, honoring plist offsets) in a canvasSize×canvasSize buffer.
    static geode::Result<std::vector<TemplateFrame>> extract(
        IconType type, int iconId, int canvasSize);

    // Single frame variant; `suffix` as in TemplateFrame::suffix.
    static geode::Result<texture_studio::ImageBuffer> extractFrame(
        IconType type, int iconId, std::string_view suffix, int canvasSize);

private:
    TemplateExtractor() = delete;
};

}  // namespace paimon::icon_maker
