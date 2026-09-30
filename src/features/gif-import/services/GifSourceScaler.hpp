#pragma once

#include "../GifImportTypes.hpp"

#include <memory>

namespace paimon::gifimport {

// main thread only: downscaling may touch gl before the tracing worker starts.
std::shared_ptr<SourceAnimation> prescaleSource(
    std::shared_ptr<SourceAnimation> source,
    int maxDimension,
    float blurRadius = 0.f
);

} // namespace paimon::gifimport
