#pragma once

#include "PackExporterTypes.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// animated standalone sprite next to static sheets: multi-frame fusions play in imageplus/happy textures.
struct AnimatedFusionExport {
    // zip entry path, e.g. "gj_playbtn_001.gif" (same basename as the frame).
    std::string entryName;
    std::vector<std::uint8_t> gifBytes;
    int frameCount = 0;
    int width = 0;
    int height = 0;
    std::string spriteName;
};

class AnimatedFusionExporter final {
public:
    // statics already live in the sheet; per-sprite failures log-and-skip so export survives.
    static geode::Result<std::vector<AnimatedFusionExport>> exportAll(
        PackExportConfig const& cfg);

    // encode a single sprite. empty frames → err.
    static geode::Result<AnimatedFusionExport> exportOne(
        PackExportConfig const& cfg,
        std::string const& frameName,
        SpriteFusionOverride const& fusion);

private:
    AnimatedFusionExporter() = delete;
};

// zip entry for a frame name: "foo_001.png" → "foo_001.gif".
std::string fusionGifEntryName(std::string const& frameName);

}  // namespace paimon::texture_studio
