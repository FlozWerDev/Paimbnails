#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace paimon::gifimport {

// Tracing thread cap. 0 lets the machine decide.
unsigned int workerLimit();

// Split threads are born and joined inside one call, so they skip ThreadTracker:
// nothing to close at game exit beyond the import thread, which is tracked and
// waits on these.
unsigned int parallelThreads(std::size_t count);
void enterParallelRegion();
void leaveParallelRegion();

// Splits [0, count) across threads. Never nested: render passes already take
// the whole machine and nesting only oversubscribes.
template <typename Fn>
void parallelFor(std::size_t count, Fn body) {
    unsigned int const threads = parallelThreads(count);
    if (threads <= 1) {
        for (std::size_t index = 0; index < count; ++index) body(index);
        return;
    }

    std::atomic<std::size_t> next{0};
    auto consume = [&] {
        for (;;) {
            std::size_t const index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) return;
            body(index);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (unsigned int worker = 1; worker < threads; ++worker) {
        workers.emplace_back([&consume] {
            enterParallelRegion();
            consume();
            leaveParallelRegion();
        });
    }
    enterParallelRegion();
    consume();
    leaveParallelRegion();
    for (auto& worker : workers) worker.join();
}

} // namespace paimon::gifimport
