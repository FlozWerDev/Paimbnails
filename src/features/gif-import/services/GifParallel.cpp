#include "GifParallel.hpp"

#include <algorithm>

namespace paimon::gifimport {

namespace {

std::atomic<unsigned int> g_workerLimit{0};
thread_local bool t_inParallel = false;

} // namespace

unsigned int workerLimit() {
    unsigned int limit = g_workerLimit.load(std::memory_order_relaxed);
    if (limit == 0) limit = std::thread::hardware_concurrency();
    // Uno de los nucleos es del juego, que sigue dibujando mientras se importa.
    return std::max(1u, limit > 2 ? limit - 1 : limit);
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
