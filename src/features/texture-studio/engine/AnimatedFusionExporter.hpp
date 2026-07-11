#pragma once

#include "PackExporterTypes.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// One animated standalone sprite shipped next to the static sheets so that
// ImagePlus / Happy Textures can play multi-frame fusions in-game.
struct AnimatedFusionExport {
    // Zip entry path, e.g. "GJ_playBtn_001.gif" (same basename as the frame).
    std::string entryName;
    std::vector<std::uint8_t> gifBytes;
    int frameCount = 0;
    int width = 0;
    int height = 0;
    std::string spriteName;
};

class AnimatedFusionExporter final {
public:
    // Build one animated GIF per spriteFusion whose texture is multi-frame.
    // Static (single-frame) fusions are skipped — they already live in the sheet.
    // Failures for individual sprites are logged and skipped so the pack
    // still exports; returns Ok with a (possibly empty) list.
    static geode::Result<std::vector<AnimatedFusionExport>> exportAll(
        PackExportConfig const& cfg);

    // Encode a single sprite. Empty frames → Err.
    static geode::Result<AnimatedFusionExport> exportOne(
        PackExportConfig const& cfg,
        std::string const& frameName,
        SpriteFusionOverride const& fusion);

private:
    AnimatedFusionExporter() = delete;
};

// Zip entry for a frame name: "foo_001.png" → "foo_001.gif".
std::string fusionGifEntryName(std::string const& frameName);

}  // namespace paimon::texture_studio
