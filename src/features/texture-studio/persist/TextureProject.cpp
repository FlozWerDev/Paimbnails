#include "TextureProject.hpp"

#include "../data/PlistParser.hpp"
#include "SlotPaths.hpp"

#include <chrono>
#include <filesystem>

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
    cfg.tintScope              = tintScope;
    cfg.onlyTintUiSprites      = tintScope != TintScope::Everything;
    cfg.includeMediumPort      = includeMediumPort;
    cfg.transparentLists       = transparentLists;
    cfg.colorGradientBg        = colorGradientBg;
    cfg.colorMainMenu          = colorMainMenu;

    for (auto const& [frameName, setting] : spriteSettings) {
        if (setting.skip) cfg.spriteSkip.insert(frameName);
        if (setting.useCustomColors) {
            cfg.spriteColors.emplace(frameName, TintColors{
                setting.color1, setting.color2, setting.colorGlow});
        }
        if (setting.hasCustomImage) {
            cfg.spriteImages.emplace(frameName,
                SlotPaths::spriteImageFile(id, frameName));
        }
    }

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

bool ensureRepresentativeFrame(TextureProject& project) {
    if (!project.representativeFrame.empty() &&
        project.representativeSheetIndex >= 0 &&
        project.representativeSheetIndex < static_cast<int>(project.sheets.size())) {
        return true;
    }

    std::string fallbackFrame;
    int fallbackSheet = -1;
    for (int sheetIndex = 0; sheetIndex < static_cast<int>(project.sheets.size()); ++sheetIndex) {
        auto const& sheet = project.sheets[sheetIndex];
        auto parsed = PlistParser::parseFile(std::filesystem::path(sheet.sourcePlistPath));
        if (!parsed) continue;

        for (auto const& frame : parsed.unwrap().frames) {
            auto kind = UiSpriteCatalog::classify(frame.name, sheet.baseName);
            if (kind != SpriteKind::Button && kind != SpriteKind::MenuUi) continue;
            if (fallbackFrame.empty()) {
                fallbackFrame = frame.name;
                fallbackSheet = sheetIndex;
            }

            int w = frame.sourceW > 0 ? frame.sourceW : frame.spriteW;
            int h = frame.sourceH > 0 ? frame.sourceH : frame.spriteH;
            if (kind == SpriteKind::Button && w >= 32 && h >= 32 && w <= 512 && h <= 512) {
                project.representativeFrame = frame.name;
                project.representativeSheetIndex = sheetIndex;
                return true;
            }
        }
    }

    project.representativeFrame = std::move(fallbackFrame);
    project.representativeSheetIndex = fallbackSheet;
    return fallbackSheet >= 0;
}

}  // namespace paimon::texture_studio
