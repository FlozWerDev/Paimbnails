#pragma once

#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace paimon::gifimport {

// tracing thread cap. 0 lets the machine decide.
unsigned int workerLimit();

// the tracked import thread joins all local workers before returning.
unsigned int parallelThreads(std::size_t count);
void enterParallelRegion();
void leaveParallelRegion();

// splits [0, count) across threads. never nested: render passes already take
// the whole machine and nesting only oversubscribes.
template <typename Fn>
void parallelFor(std::size_t count, Fn body) {
    unsigned int const threads = parallelThreads(count);
    if (threads <= 1) {
        for (std::size_t index = 0; index < count; ++index) body(index);
        return;
    }

    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex errorMutex;
    auto consume = [&] {
        enterParallelRegion();
        try {
            while (!failed.load(std::memory_order_relaxed)) {
                std::size_t const index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count) break;
                body(index);
            }
        } catch (...) {
            std::lock_guard lock(errorMutex);
            if (!error) error = std::current_exception();
            failed.store(true, std::memory_order_relaxed);
        }
        leaveParallelRegion();
    };

    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    try {
        for (unsigned int worker = 1; worker < threads; ++worker) {
            workers.emplace_back(consume);
        }
    } catch (...) {
        failed.store(true, std::memory_order_relaxed);
        for (auto& worker : workers) worker.join();
        throw;
    }
    consume();
    for (auto& worker : workers) worker.join();
    if (error) std::rethrow_exception(error);
}

} // namespace paimon::gifimport
