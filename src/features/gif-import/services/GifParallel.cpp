#include "GifParallel.hpp"

#include <algorithm>

namespace paimon::gifimport {

namespace {

thread_local bool t_inParallel = false;

} // namespace

unsigned int workerLimit() {
    // Use all available logical CPUs so heavy SVG and image processing finishes
    // as fast as possible.
    auto const available = std::thread::hardware_concurrency();
    return std::max(1u, available);
}

unsigned int parallelThreads(std::size_t count) {
    if (count <= 1 || t_inParallel) return 1;
    return static_cast<unsigned int>(std::min<std::size_t>(workerLimit(), count));
}

void enterParallelRegion() {
    t_inParallel = true;
}

void leaveParallelRegion() {
    t_inParallel = false;
}

} // namespace paimon::gifimport
