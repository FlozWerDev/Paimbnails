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

// re-open slots after gd moves resources by re-resolving paths.
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
    std::uint64_t spriteHash = 0;  // fnv-1a of source rgba.
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

    // placement used when hascustomimage is set.
    ImageTransform imageTransform{};

    // false replaces the sprite; true composites over it.
    bool imageOverlay = false;

    // fusion region-fill; mask/texture stamped without pack recoloring.
    bool hasFusion = false;
    bool fusionAnimated = false;
    // replace keeps texture colors pure; luma/overlay optional.
    FusionBlendMode fusionBlend = FusionBlendMode::Replace;
    // paint-bucket color radius; typical range 90–140.
    int   fusionTolerance = 110;
    // grow into same-color neighbors for aa fringes; 0 disables.
    int   fusionExpandRadius = 1;
    float fusionOpacity = 1.0f;
    ImageTransform fusionTransform{};
    // pixel placement; +y is down.
    int fusionPixelX = 0;
    int fusionPixelY = 0;

    bool hasAny() const {
        return skip || useCustomColors || hasCustomImage || hasFusion;
    }
};

struct TextureProject {
    int schemaVersion = 2;
    bool liveRendering = false;
    float tintStrength = 1.f;
    float glowStrength = 1.f;

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
    // interior glyph color; pure white keeps vanilla.
    cocos2d::ccColor3B colorDetail{255, 255, 255};
    int  brightness = 160;

    // tint engine parameters; see spritepreviewoptions.
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

    // deprecated name kept so old projects still parse.
    bool usePackGenAssets   = true;
    bool tintGoldFont       = false;
    bool colorGoldTitles    = false;
    bool colorDemonFaces    = false;
    bool mythicCompat       = false;
    bool includeModTextures = true;
    // export animated fusion gifs alongside static sheets.
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

// false only when no selected plist contains a usable ui sprite.
bool ensureRepresentativeFrame(TextureProject& project);

}
