#pragma once
//
// PackExporterTypes.hpp - Shared data types for the pack-export pipeline.
//
// Split into its own header so that both the exporter implementation and
// future UI code can include just the types without dragging in the (heavier)
// engine implementation headers.
//

#include "../data/SpriteFrameInfo.hpp"
#include "LuminanceTinter.hpp"
#include "UiSpriteCatalog.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::texture_studio {

// Inputs

// One sheet to include in the export. Caller is responsible for ensuring
// the pair refers to the SAME sprite sheet (matching base name + quality).
struct SheetSelection {
    std::string baseName;                   // "GJ_GameSheet01"
    std::string qualitySuffix;              // "-uhd" (matches the source on disk)
    std::filesystem::path sourcePlist;      // absolute path to the .plist
    std::filesystem::path sourcePng;        // absolute path to the .png
};

// Full export configuration. Mirrors PackGen's controls:
// pack name + 3 colors + brightness + boolean toggles.
struct PackExportConfig {
    // Identity
    std::string packName = "My Pack";       // human-readable name
    std::string author   = "Paimbnails";    // pack.json "author"

    // Colors
    TintColors    colors{};                 // C1, C2, glow
    int           brightness = 160;         // 100..300; PackGen default 160
    bool          alternativeGlowOverlay = false;

    // Solo teñir botones y UI de menú (default). Con false se recolorea
    // todo el sheet (comportamiento legacy estilo PackGen). Mantener en
    // true salvo que el usuario lo desactive explícitamente: pintar assets
    // de gameplay rompe la legibilidad del juego.
    bool onlyTintUiSprites = true;
    TintScope tintScope = TintScope::ButtonsOnly;

    // Suavidad de máscara para el anti-aliasing del tinte (ver
    // SheetTinterRequest::maskSoftness).
    float maskSoftness = 0.35f;

    // Overrides por sprite, poblados por la UI desde ManualOverrideStore.
    // spriteSkip tiñe nunca; spriteColors usa colores propios por sprite
    // (prioridad sobre `colors`, y tiñe aunque el filtro de UI no acepte
    // el sprite).
    std::unordered_set<std::string> spriteSkip;
    std::unordered_map<std::string, TintColors> spriteColors;
    std::unordered_map<std::string, std::filesystem::path> spriteImages;

    // Quality ports
    // Always emits the user-selected quality (whatever the SheetSelection
    // pairs say). When `includeMediumPort` is true and the source is -uhd,
    // we also emit a -hd version at half resolution.
    bool includeMediumPort = false;

    // Sheets to process
    // Order matters for status reporting: we report progress by index.
    std::vector<SheetSelection> sheets;

    // HappyTextures-compat settings (optional)
    bool transparentLists = false;          // ui/ModsLayer.json — list bg α=0
    bool colorGradientBg  = false;          // ui/colors.json    — overrides
    bool colorMainMenu    = false;          // ui/colors.json    — main menu G/BG

    // Raw bytes of a PNG to use as custom loading background. Empty = use
    // GD default. Populated by the UI's file picker.
    std::vector<std::uint8_t> customLoadingBgPng;
};

// Outputs

// Per-sheet export result. We capture both success metadata and per-sheet
// errors so the UI can show "5/7 sheets exported, 2 failed" style messages.
struct SheetExportResult {
    std::string baseName;
    std::string qualitySuffix;             // "-uhd" / "-hd"
    bool        success = false;
    std::string errorMessage;              // populated when success == false
    int         frameCount = 0;
    int         atlasWidth = 0;
    int         atlasHeight = 0;
    int         needsReviewCount = 0;      // frames the classifier flagged
};

// Top-level export result. The exporter writes the final .zip to disk and
// reports its location here.
struct PackExportResult {
    bool                        success = false;
    std::string                 errorMessage;
    std::filesystem::path       outputZipPath;        // absolute
    std::int64_t                outputZipSizeBytes = 0;
    std::vector<SheetExportResult> sheetResults;
    std::string                 packId;               // slug derived from packName
};

}  // namespace paimon::texture_studio
