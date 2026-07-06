#pragma once

#include "../data/ImageTransform.hpp"
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

// Caller must ensure the pair refers to the SAME sprite sheet (matching base name + quality).
struct SheetSelection {
    std::string baseName;
    std::string qualitySuffix;
    std::filesystem::path sourcePlist;
    std::filesystem::path sourcePng;
};

// A custom replacement image plus how to place it inside the frame.
struct SpriteImageOverride {
    std::filesystem::path path;
    ImageTransform transform{};
    // false = replace the sprite; true = composite on top of the base sprite.
    bool overlay = false;
};

struct PackExportConfig {
    std::string packName = "My Pack";
    std::string author   = "Paimbnails";

    TintColors    colors{};
    int           brightness = 160;
    bool          alternativeGlowOverlay = false;

    // Tinting gameplay assets breaks in-game readability; keep UI-only by default.
    bool onlyTintUiSprites = true;
    TintScope tintScope = TintScope::ButtonsOnly;

    float maskSoftness = 0.35f;

    // Segmentation / grading parameters; must match the preview path
    // (SpritePreviewOptions) so exports look like the editor.
    int   clusterPrecision = 5;
    int   edgeCleanup = 1;
    int   outlineProtect = 0;
    float saturation = 1.0f;
    float contrast   = 0.0f;

    // spriteColors take priority over `colors` and tint even when the UI filter rejects the sprite.
    std::unordered_set<std::string> spriteSkip;
    std::unordered_map<std::string, TintColors> spriteColors;
    std::unordered_map<std::string, SpriteImageOverride> spriteImages;

    bool includeMediumPort = false;

    // Order matters: progress is reported by index.
    std::vector<SheetSelection> sheets;

    bool transparentLists = false;
    bool colorGradientBg  = false;
    bool colorMainMenu    = false;

    // --- PackGen asset-pack precision mode --------------------------------
    // Uses Asterveila's hand-drawn overlay masks (downloaded and cached) for
    // pixel-exact tinting; falls back to auto-clustering when offline or for
    // sheets the pack doesn't cover.
    bool usePackGenAssets = true;

    // The following only take effect when the asset pack is available:
    bool tintGoldFont       = false;  // recolor goldFont + ship its .fnt
    bool colorGoldTitles    = false;  // gold "quit / menu" titles → color2
    bool colorDemonFaces    = false;  // demon difficulty faces + DIB sheets
    bool mythicCompat       = false;  // DIB legendary/mythic + Godlike faces
    bool includeModTextures = true;   // recolored textures for popular mods

    // Empty = use GD default loading background.
    std::vector<std::uint8_t> customLoadingBgPng;
};

struct SheetExportResult {
    std::string baseName;
    std::string qualitySuffix;
    bool        success = false;
    std::string errorMessage;
    int         frameCount = 0;
    int         atlasWidth = 0;
    int         atlasHeight = 0;
    int         needsReviewCount = 0;
};

struct PackExportResult {
    bool                        success = false;
    std::string                 errorMessage;
    std::filesystem::path       outputZipPath;
    std::int64_t                outputZipSizeBytes = 0;
    std::vector<SheetExportResult> sheetResults;
    std::string                 packId;

    // PackGen precision-mode stats. `precisionUsed` is false when the mode
    // was requested but the asset pack could not be fetched (offline).
    bool precisionUsed        = false;
    int  standaloneProcessed  = 0;
    int  standaloneFailed     = 0;
    std::string precisionNote;
};

}  // namespace paimon::texture_studio
