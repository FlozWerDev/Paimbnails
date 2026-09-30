#pragma once

#include <array>
#include <cstddef>
#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// main thread only: rasterization needs gl and the game sprite cache.
struct SoftStampLibrary {
    // a valid toolbox yields seven molds; missing native molds use deterministic analytic fallbacks.
    std::vector<PlanStamp> stamps;
    // best native error per shape (radial, vertical, quarters): feeds the
    // 'native soft shapes' log and the pipeline error message.
    std::array<double, 3> errors{1.0, 1.0, 1.0};
};

SoftStampLibrary buildSoftStampLibrary();

bool stampLibraryReady();
std::size_t buildStampLibrary();

} // namespace paimon::gifimport
