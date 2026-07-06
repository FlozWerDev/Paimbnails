#pragma once

#include "../engine/PackExporterTypes.hpp"
#include "../engine/UiSpriteCatalog.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// Re-opening a slot must work even if GD moved; the UI re-resolves paths
// via GdResourcesLocator and warns on mismatch.
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
    std::uint64_t spriteHash = 0;  // FNV-1a of the source sprite's RGBA bytes
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

    // Placement of the custom image inside the frame (only meaningful when
    // hasCustomImage is set).
    ImageTransform imageTransform{};

    // false = the image replaces the sprite entirely; true = the image is
    // composited on top of the (tinted or vanilla) sprite.
    bool imageOverlay = false;

    bool hasAny() const { return skip || useCustomColors || hasCustomImage; }
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
    // Interior white glyphs; pure white = keep vanilla.
    cocos2d::ccColor3B colorDetail{255, 255, 255};
    int  brightness = 160;

    // Tint engine tuning (see SpritePreviewOptions for semantics).
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

    // PackGen asset-pack precision mode (see PackExportConfig).
    bool usePackGenAssets   = true;
    bool tintGoldFont       = false;
    bool colorGoldTitles    = false;
    bool colorDemonFaces    = false;
    bool mythicCompat       = false;
    bool includeModTextures = true;

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

// Returns false only when no selected plist contains a usable UI sprite.
bool ensureRepresentativeFrame(TextureProject& project);

}  // namespace paimon::texture_studio
