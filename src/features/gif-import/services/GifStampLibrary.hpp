#pragma once

#include <array>
#include <cstddef>
#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// Rasterizes GD decoration into free-mode molds. Needs GL and the game sprite
// cache, so it runs on the main thread; tracing reads it ready-made from its
// threads. Built once per session.
// Full alpha masks: round glow, descending/ascending ramps, four radial quarters.
struct SoftStampLibrary {
    // Always 7 molds when the toolbox exists: natives found, best over-threshold
    // kept as fallback, analytic spare with fixed IDs (analyticFallback) for the
    // rest. Empty only when the toolbox is missing; the pipeline rejects it.
    std::vector<PlanStamp> stamps;
    // Best native error per shape (radial, vertical, quarters): feeds the
    // 'Native soft shapes' log and the pipeline error message.
    std::array<double, 3> errors{1.0, 1.0, 1.0};
};

SoftStampLibrary buildSoftStampLibrary();

bool stampLibraryReady();
std::size_t buildStampLibrary();

} // namespace paimon::gifimport
