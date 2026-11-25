#include "GifParallel.hpp"

#include <algorithm>

namespace paimon::gifimport {

namespace {

thread_local bool t_inParallel = false;

} // namespace

unsigned int workerLimit() {
    // tracing shares the process with the editor. Leave half the logical CPUs
    // available, with at most four workers even on high-core-count machines.
    auto const available = std::thread::hardware_concurrency();
    return std::clamp(available / 2, 1u, 4u);
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
