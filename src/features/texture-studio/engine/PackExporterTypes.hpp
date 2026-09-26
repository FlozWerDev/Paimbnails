#pragma once

#include "../data/ImageTransform.hpp"
#include "../data/SpriteFrameInfo.hpp"
#include "FusionEngine.hpp"
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

// The plist and PNG must belong to the same sheet and quality.
struct SheetSelection {
    std::string baseName;
    std::string qualitySuffix;
    std::filesystem::path sourcePlist;
    std::filesystem::path sourcePng;
};

struct SpriteImageOverride {
    std::filesystem::path path;
    ImageTransform transform{};
    // False replaces the sprite; true composites over it.
    bool overlay = false;
};

// Region fill after tint/image overrides. Sheets bake frame 0; animated fusions optionally ship as GIFs.
struct SpriteFusionOverride {
    std::filesystem::path maskPath;
    std::filesystem::path texturePath;
    FusionBlendMode blendMode = FusionBlendMode::Replace;
    float opacity = 1.0f;
    ImageTransform transform{};
    int pixelOffsetX = 0;
    int pixelOffsetY = 0;
};

struct PackExportConfig {
    std::string packName = "My Pack";
    std::string author   = "Paimbnails";

    TintColors    colors{};
    int           brightness = 160;
    bool          alternativeGlowOverlay = false;

    // Gameplay tint hurts readability: UI-only by default.
    bool onlyTintUiSprites = true;
    TintScope tintScope = TintScope::ButtonsOnly;

    float maskSoftness = 0.35f;

    // Must match SpritePreviewOptions so exports match the editor.
    int   clusterPrecision = 5;
    int   edgeCleanup = 1;
    int   outlineProtect = 0;
    float saturation = 1.0f;
    float contrast   = 0.0f;

    // Per-sprite colors override the global tint and UI filter.
    std::unordered_set<std::string> spriteSkip;
    std::unordered_map<std::string, TintColors> spriteColors;
    std::unordered_map<std::string, SpriteImageOverride> spriteImages;
    std::unordered_map<std::string, SpriteFusionOverride> spriteFusions;

    // Export animated fusions as standalone GIFs; sheets still contain frame 0.
    bool exportAnimatedFusions = true;

    bool includeMediumPort = false;

    // Order is used for progress reporting.
    std::vector<SheetSelection> sheets;

    bool transparentLists = false;
    bool colorGradientBg  = false;
    bool colorMainMenu    = false;

    // Local-extras mode: also export unselected local sheets via clustering fallback. Deprecated name, kept for parsing.
    bool usePackGenAssets = true;

    // These options apply only when local extras are enabled.
    bool tintGoldFont       = false;
    bool colorGoldTitles    = false;
    bool colorDemonFaces    = false;
    bool mythicCompat       = false;
    bool includeModTextures = true;

    // Empty uses GD's default loading background.
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

    // Local-extras result; false means only project sheets were exported.
    bool precisionUsed        = false;
    int  standaloneProcessed  = 0;
    int  standaloneFailed     = 0;
    std::string precisionNote;

    int  animatedFusionCount  = 0;
};

}
