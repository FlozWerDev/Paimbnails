#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <memory>
#include <algorithm>
#include <exception>
#include <string>
#include <Geode/loader/Log.hpp>
#include "JoinWithWarning.hpp"

namespace paimon {

// fixed pool; bounds concurrent disk i/o.
class ThreadPool {
public:
    struct SharedState {
        std::queue<std::function<void()>> jobs;
        std::queue<std::function<void()>> priorityJobs; // drained before jobs
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> stopped{false};
    };

    explicit ThreadPool(int numThreads, char const* name = "PaimonPool")
        : m_name(name), m_state(std::make_shared<SharedState>())
    {
        numThreads = std::max(1, numThreads);
        geode::log::info("[ThreadPool] creating '{}' with {} threads", m_name, numThreads);
        m_workers.reserve(numThreads);
        try {
            for (int i = 0; i < numThreads; ++i) {
                auto state = m_state;
                auto nameCopy = m_name;
                m_workers.emplace_back([state, nameCopy, i]() {
                    geode::utils::thread::setName(
                        fmt::format("{} #{}", nameCopy, i));
                    workerLoop(std::move(state));
                });
            }
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    ThreadPool(ThreadPool const&) = delete;
    ThreadPool& operator=(ThreadPool const&) = delete;

    void enqueue(std::function<void()> job) {
        auto state = m_state;
        if (!state) return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped.load(std::memory_order_acquire)) return;
            state->jobs.push(std::move(job));
        }
        state->cv.notify_one();
    }

    // visible-cell work jumps predictive prefetches.
    void enqueueFront(std::function<void()> job) {
        auto state = m_state;
        if (!state) return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped.load(std::memory_order_acquire)) return;
            state->priorityJobs.push(std::move(job));
        }
        state->cv.notify_one();
    }

    void shutdown() {
        auto state = m_state;
        if (!state) return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped.load(std::memory_order_acquire)) return;
            state->stopped.store(true, std::memory_order_release);
            // drop pending jobs; running ones finish.
            std::queue<std::function<void()>>().swap(state->jobs);
            std::queue<std::function<void()>>().swap(state->priorityJobs);
        }
        state->cv.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) paimon::joinWithWarning(t, std::chrono::seconds(3));
        }
        m_workers.clear();
        geode::log::info("[ThreadPool] '{}' shut down", m_name);
    }

    bool isStopped() const {
        auto state = m_state;
        return !state || state->stopped.load(std::memory_order_acquire);
    }

    int pendingCount() const {
        auto state = m_state;
        if (!state) return 0;
        std::lock_guard<std::mutex> lock(state->mutex);
        return static_cast<int>(state->jobs.size() + state->priorityJobs.size());
    }

private:
    static void workerLoop(std::shared_ptr<SharedState> const& state) {
        if (!state) return;
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->cv.wait(lock, [state]() {
                    return state->stopped.load(std::memory_order_acquire) ||
                           !state->priorityJobs.empty() ||
                           !state->jobs.empty();
                });
                if (state->stopped.load(std::memory_order_acquire) &&
                    state->priorityJobs.empty() && state->jobs.empty()) return;
                if (!state->priorityJobs.empty()) {
                    job = std::move(state->priorityJobs.front());
                    state->priorityJobs.pop();
                } else if (!state->jobs.empty()) {
                    job = std::move(state->jobs.front());
                    state->jobs.pop();
                } else {
                    continue;
                }
            }
            try {
                job();
            } catch (std::exception const& e) {
                geode::log::error("[ThreadPool] Job failed: {}", e.what());
            } catch (...) {
                geode::log::error("[ThreadPool] Job failed with an unknown exception");
            }
        }
    }

    std::string m_name;
    std::shared_ptr<SharedState> m_state;
    std::vector<std::thread> m_workers;
};

} // namespace paimon
