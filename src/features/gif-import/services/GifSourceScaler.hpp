#pragma once

#include "../GifImportTypes.hpp"

#include <memory>

namespace paimon::gifimport {

// Frames land at the resolution tracing actually looks at before the worker
// spins up. A 1080p video would average two million pixels per frame down to
// a 64-cell grid: this does it once (GPU with context, thread pool without)
// and everything downstream works on the small image.
//
// Main thread: touches GL. Returns the same source when there is nothing to trim.
std::shared_ptr<SourceAnimation> prescaleSource(
    std::shared_ptr<SourceAnimation> source,
    int maxDimension,
    float blurRadius = 0.f
);

} // namespace paimon::gifimport
