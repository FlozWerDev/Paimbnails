#pragma once

#include "../engine/FusionEngine.hpp"
#include "../engine/PackExporterTypes.hpp"
#include "../engine/UiSpriteCatalog.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// Re-open slots after GD moves resources by re-resolving paths.
struct ProjectSheetRef {
    std::string baseName;
    std::string qualitySuffix;
    std::string sourcePlistPath;
    std::string sourcePngPath;
};

struct ManualOverrideRef {
    std::string spriteName;
    int  width  = 0;
    int  height = 0;
    int  version = 1;
    std::int64_t modifiedAt = 0;
};

struct AutoCacheRef {
    std::string  spriteName;
    std::uint64_t spriteHash = 0;  // FNV-1a of source RGBA.
    int          clusterCount = 0;
};

struct SpriteSetting {
    bool skip = false;
    bool useCustomColors = false;
    bool hasCustomImage = false;
    cocos2d::ccColor3B color1{149, 226, 3};
    cocos2d::ccColor3B color2{28, 233, 255};
    cocos2d::ccColor3B colorGlow{255, 255, 255};
    cocos2d::ccColor3B colorDetail{255, 255, 255};

    // Placement used when hasCustomImage is set.
    ImageTransform imageTransform{};

    // False replaces the sprite; true composites over it.
    bool imageOverlay = false;

    // Fusion region-fill; mask/texture live under SlotPaths::fusionsDir and are
    // stamped without pack recoloring.
    bool hasFusion = false;
    bool fusionAnimated = false;
    // Replace keeps texture colors pure; Luma/Overlay are optional.
    FusionBlendMode fusionBlend = FusionBlendMode::Replace;
    // Paint-bucket color radius; typical range 90–140.
    int   fusionTolerance = 110;
    // Grow into same-color neighbors to cover AA fringes; 0 disables it.
    int   fusionExpandRadius = 1;
    float fusionOpacity = 1.0f;
    ImageTransform fusionTransform{};
    // Pixel placement; +Y is down.
    int fusionPixelX = 0;
    int fusionPixelY = 0;

    bool hasAny() const {
        return skip || useCustomColors || hasCustomImage || hasFusion;
    }
};

struct TextureProject {
    int schemaVersion = 1;

    std::string id;
    std::string name;
    std::string author;
    std::int64_t createdAt  = 0;
    std::int64_t modifiedAt = 0;

    std::vector<ProjectSheetRef> sheets;
    std::string representativeFrame;
    int representativeSheetIndex = -1;

    cocos2d::ccColor3B color1{149, 226, 3};
    cocos2d::ccColor3B color2{28, 233, 255};
    cocos2d::ccColor3B colorGlow{255, 255, 255};
    // Interior glyph color; pure white keeps vanilla.
    cocos2d::ccColor3B colorDetail{255, 255, 255};
    int  brightness = 160;

    // Tint engine parameters; see SpritePreviewOptions.
    float maskSoftness     = 0.35f;
    int   clusterPrecision = 5;
    int   edgeCleanup      = 1;
    int   outlineProtect   = 0;
    float saturation       = 1.0f;
    float contrast         = 0.0f;

    bool includeMediumPort       = false;
    bool alternativeGlowOverlay  = false;
    bool transparentLists        = false;
    bool colorGradientBg         = false;
    bool colorMainMenu           = false;

    // PackGen precision-mode options; see PackExportConfig.
    bool usePackGenAssets   = true;
    bool tintGoldFont       = false;
    bool colorGoldTitles    = false;
    bool colorDemonFaces    = false;
    bool mythicCompat       = false;
    bool includeModTextures = true;
    // Export animated fusion GIFs alongside static sheets.
    bool exportAnimatedFusions = true;

    std::map<std::string, ManualOverrideRef> overrides;
    std::map<std::string, AutoCacheRef>      autoCache;
    std::map<std::string, SpriteSetting>     spriteSettings;
    TintScope tintScope = TintScope::ButtonsOnly;

    bool         hasBuiltOnce  = false;
    std::int64_t lastBuiltAt   = 0;
    std::string  lastZipRelPath;

    PackExportConfig toExportConfig() const;
};

std::int64_t nowUnixMs();

// False only when no selected plist contains a usable UI sprite.
bool ensureRepresentativeFrame(TextureProject& project);

}
