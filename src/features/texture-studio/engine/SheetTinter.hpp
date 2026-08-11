#pragma once

#include "PackExporterTypes.hpp"
#include "../data/ImageBuffer.hpp"
#include "../data/SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::texture_studio {

struct SheetTinterOutput {
    std::vector<std::uint8_t> pngBytes;
    std::string               plistXml;
    int   atlasWidth     = 0;
    int   atlasHeight    = 0;
    int   frameCount     = 0;
    int   needsReviewCnt = 0;
    int   tintedFrameCount = 0;
};

// PackGen overlays aligned to the source atlas; empty buffers mean unavailable.
struct SheetOverlaySources {
    ImageBuffer overlay1;  // color1.
    ImageBuffer overlay2;  // color2.
    ImageBuffer gold;      // color2 for gold titles.
    ImageBuffer demon1;    // color1 for demon faces.
    ImageBuffer demon2;    // color2 for demon faces.
    ImageBuffer glow;      // glow.

    bool any() const {
        return !overlay1.empty() || !overlay2.empty() || !gold.empty()
            || !demon1.empty() || !demon2.empty() || !glow.empty();
    }
};

struct SheetTinterRequest {
    std::filesystem::path sourcePlist;
    std::filesystem::path sourcePng;
    std::string outputBaseName;
    std::string outputQualitySuffix;

// Non-empty overlays use pixel-exact PackGen tinting; uncovered frames stay
// vanilla. Per-sprite color overrides still use clustering.
    std::shared_ptr<SheetOverlaySources const> overlaySources;

    TintColors    colors{};
    int           brightness = 160;
    bool          alternativeGlowOverlay = false;

// When true, tint only menu/button UI sprites for readability.
    bool onlyTintUiSprites = true;
    TintScope tintScope = TintScope::ButtonsOnly;

// 0 = hard, 1 = fully soft; softness affects ambiguous cluster edges.
    float maskSoftness = 0.35f;

// Segmentation/grading parameters; see SpritePreviewOptions.
    int   clusterPrecision = 5;
    int   edgeCleanup = 1;
    int   outlineProtect = 0;
    float saturation = 1.0f;
    float contrast   = 0.0f;

// spriteSkip bypasses tinting; spriteColors override global colors and filters.
    std::unordered_set<std::string> spriteSkip;
    std::unordered_map<std::string, TintColors> spriteColors;
    std::unordered_map<std::string, SpriteImageOverride> spriteImages;
    std::unordered_map<std::string, SpriteFusionOverride> spriteFusions;

// Downscale before repacking; 1.0 means none.
    float resizeScale = 1.0f;

// PackGen compatibility: do not scale GJ_table_side_001's offset.
    bool preserveOffsetForTableSide = true;
};

class SheetTinter final {
public:
    static geode::Result<SheetTinterOutput> process(SheetTinterRequest const& req);

private:
    SheetTinter() = delete;
};

}
