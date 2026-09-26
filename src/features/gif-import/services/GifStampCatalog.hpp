#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// Signature side for mold-vs-blob compare. 8x8 fits a uint64, so scanning the
// whole library costs two instructions per mold instead of walking its art.
constexpr int kStampSignatureSide = 8;
constexpr int kStampMaskSide = 32;

// A decoration object as the game draws it, unoriented. `mask` is trimmed to
// what paints; the offset places that trim inside the object frame.
struct CatalogEntry {
    int objectId = 0;
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    float offsetX = 0.f;
    float offsetY = 0.f;
    StampMask mask;
};

// One concrete orientation, which is what search tries.
struct StampVariant {
    PlanStamp stamp;
    std::uint64_t signature = 0;
    int filled = 0;
};

// The game fills the library by rasterizing its own objects. Frozen on the main
// thread before tracing starts and untouched while it runs: the only thing
// making it safe to read from worker threads.
void setStampCatalog(std::vector<CatalogEntry> entries);
std::vector<StampVariant> const& stampVariants();

// Analytic spares for the soft library (Blur/Vert) when GD exposes no native
// glow or gradient. Same formulas as the buildSoftStampLibrary fit: normalized
// radial gaussian, 1-v ramp and corner falloff, in kStampMaskSide cells.
StampMask analyticRadialGlowMask();
StampMask analyticVerticalGradientMask();
StampMask analyticQuarterGlowMask();

// The four evergreen figures, for when the game hasn't scanned its library yet:
// free mode never ends up with nothing to drop.
std::vector<CatalogEntry> builtinStampCatalog();

} // namespace paimon::gifimport
