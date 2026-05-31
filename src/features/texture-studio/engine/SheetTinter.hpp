#pragma once
//
// SheetTinter.hpp - Processes one entire sheet (plist + png) through the
// full pipeline:
//
//   load → for each frame: cluster → classify → masks → tint
//        → re-pack atlas (RectPacker shelf)
//        → emit (.png bytes, .plist string)
//
// This is the workhorse called once per sheet in PackExporter. The output
// is in-memory only (no file IO) so the caller can write it into a zip,
// inspect it, or discard it.
//
// We intentionally keep this stateless / static. Multiple sheets can be
// processed concurrently from different threads if the caller wants — the
// only shared state is `log::*` which Geode handles thread-safely.
//

#include "PackExporterTypes.hpp"
#include "../data/SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// In-memory output of one sheet conversion. Both pieces are sized
// proportionally to the input: a 2048x2048 sheet typically produces a
// ~1-2MB PNG + ~150KB plist text.
struct SheetTinterOutput {
    std::vector<std::uint8_t> pngBytes;     // encoded PNG (RGBA8)
    std::string               plistXml;     // cocos2d-x format 3 plist
    int   atlasWidth     = 0;
    int   atlasHeight    = 0;
    int   frameCount     = 0;
    int   needsReviewCnt = 0;               // # of frames flagged by classifier
};

// Optional knobs; mirrors PackExportConfig but only the fields SheetTinter
// actually needs. The exporter populates this from the config + per-sheet
// metadata (e.g. quality suffix to use in the output filenames).
struct SheetTinterRequest {
    std::filesystem::path sourcePlist;       // input
    std::filesystem::path sourcePng;
    std::string outputBaseName;              // e.g. "GJ_GameSheet01"
    std::string outputQualitySuffix;         // e.g. "-uhd" or "-hd"

    TintColors    colors{};
    int           brightness = 160;
    bool          alternativeGlowOverlay = false;

    // When >0 and != 1.0, frames are downscaled by this factor before
    // re-packing. Used by MediumPort to produce -hd from -uhd.
    // 1.0 = no resize.
    float resizeScale = 1.0f;

    // PackGen has a special-case for `GJ_table_side_001` whose offset must
    // not be divided when scaling. We replicate it for byte-compat with
    // PackGen output.
    bool preserveOffsetForTableSide = true;
};

class SheetTinter final {
public:
    // Run the pipeline for one sheet. Returns the encoded PNG + plist on
    // success, or an Err with the first problem encountered. Frames whose
    // classification flagged needsReview are still tinted (using the best-
    // effort role assignment); the count is reported for UI surfacing.
    static geode::Result<SheetTinterOutput> process(SheetTinterRequest const& req);

private:
    SheetTinter() = delete;
};

}  // namespace paimon::texture_studio
