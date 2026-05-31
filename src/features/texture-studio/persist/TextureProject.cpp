#include "TextureProject.hpp"

#include <chrono>

namespace paimon::texture_studio {

std::int64_t nowUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

PackExportConfig TextureProject::toExportConfig() const {
    PackExportConfig cfg;
    cfg.packName               = name.empty() ? "Untitled" : name;
    cfg.author                 = author;
    cfg.colors.color1          = color1;
    cfg.colors.color2          = color2;
    cfg.colors.glow            = colorGlow;
    cfg.brightness             = brightness;
    cfg.alternativeGlowOverlay = alternativeGlowOverlay;
    cfg.includeMediumPort      = includeMediumPort;
    cfg.transparentLists       = transparentLists;
    cfg.colorGradientBg        = colorGradientBg;
    cfg.colorMainMenu          = colorMainMenu;

    // Convert sheet refs into SheetSelection. The caller is expected to
    // overwrite the path fields from a fresh GdResourcesLocator scan when
    // GD has been reinstalled or the user is on a different machine.
    cfg.sheets.reserve(sheets.size());
    for (auto const& s : sheets) {
        SheetSelection sel;
        sel.baseName      = s.baseName;
        sel.qualitySuffix = s.qualitySuffix;
        sel.sourcePlist   = std::filesystem::path(s.sourcePlistPath);
        sel.sourcePng     = std::filesystem::path(s.sourcePngPath);
        cfg.sheets.push_back(std::move(sel));
    }

    return cfg;
}

}  // namespace paimon::texture_studio
