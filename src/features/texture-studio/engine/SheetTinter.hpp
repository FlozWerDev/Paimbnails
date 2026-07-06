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

// Whole-sheet overlay layers from the PackGen asset pack, aligned to the
// SOURCE atlas layout — they only make sense when sourcePlist/sourcePng also
// come from that pack. Empty buffers mean "no such overlay".
struct SheetOverlaySources {
    ImageBuffer overlay1;  // → color1
    ImageBuffer overlay2;  // → color2
    ImageBuffer gold;      // → color2 (gold titles)
    ImageBuffer demon1;    // → color1 (demon faces)
    ImageBuffer demon2;    // → color2 (demon faces)
    ImageBuffer glow;      // → glow

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

    // When set (and non-empty), frames are tinted by cropping these
    // hand-drawn overlays instead of auto-clustering: pixel-exact PackGen
    // output. Frames without overlay coverage stay vanilla on purpose.
    // Per-sprite color overrides still use the clustering path.
    std::shared_ptr<SheetOverlaySources const> overlaySources;

    TintColors    colors{};
    int           brightness = 160;
    bool          alternativeGlowOverlay = false;

    // When true (default), only menu/button UI sprites are tinted; gameplay
    // assets pass through untouched to keep the game readable.
    bool onlyTintUiSprites = true;
    TintScope tintScope = TintScope::ButtonsOnly;

    // 0 = hard assignment, 1 = fully soft. Only blurs ambiguous cluster edges.
    float maskSoftness = 0.35f;

    // Segmentation / grading parameters (see SpritePreviewOptions).
    int   clusterPrecision = 5;
    int   edgeCleanup = 1;
    int   outlineProtect = 0;
    float saturation = 1.0f;
    float contrast   = 0.0f;

    // spriteSkip: never tinted (passthrough). spriteColors: per-sprite colors
    // that take priority over `colors` and tint even when the UI filter rejects it.
    std::unordered_set<std::string> spriteSkip;
    std::unordered_map<std::string, TintColors> spriteColors;
    std::unordered_map<std::string, SpriteImageOverride> spriteImages;

    // Downscale factor applied before re-packing (1.0 = none). MediumPort uses 0.5.
    float resizeScale = 1.0f;

    // PackGen byte-compat: GJ_table_side_001's offset must not be scaled.
    bool preserveOffsetForTableSide = true;
};

class SheetTinter final {
public:
    static geode::Result<SheetTinterOutput> process(SheetTinterRequest const& req);

private:
    SheetTinter() = delete;
};

}  // namespace paimon::texture_studio
