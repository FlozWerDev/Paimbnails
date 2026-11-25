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

// fixed pool that caps concurrent disk i/o.
class ThreadPool {
public:
    struct SharedState {
        std::queue<std::function<void()>> jobs;
        std::queue<std::function<void()>> priorityJobs; // drained before jobs
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::condition_variable doneCv;
        std::atomic<bool> stopped{false};
        size_t aliveWorkers = 0;
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
                auto exited = std::make_shared<std::atomic<bool>>(false);
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    ++state->aliveWorkers;
                }
                try {
                    std::thread t([state, nameCopy, i, exited]() {
                        geode::utils::thread::setName(
                            fmt::format("{} #{}", nameCopy, i));
                        workerLoop(state);
                        exited->store(true, std::memory_order_release);
                        {
                            std::lock_guard<std::mutex> lock(state->mutex);
                            if (state->aliveWorkers > 0) --state->aliveWorkers;
                        }
                        state->doneCv.notify_all();
                    });
                    m_workers.push_back({std::move(t), std::move(exited)});
                } catch (...) {
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (state->aliveWorkers > 0) --state->aliveWorkers;
                    }
                    state->doneCv.notify_all();
                    throw;
                }
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

    // visible-cell work goes before predictive prefetches.
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

    void requestStop() {
        auto state = m_state;
        if (!state) return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped.exchange(true, std::memory_order_acq_rel)) return;
            // drop queued jobs; running ones finish.
            std::queue<std::function<void()>>().swap(state->jobs);
            std::queue<std::function<void()>>().swap(state->priorityJobs);
        }
        state->cv.notify_all();
    }

    void waitShutdown(std::chrono::steady_clock::time_point deadline) {
        requestStop();
        auto state = m_state;
        std::vector<Worker> workers;
        {
            std::lock_guard<std::mutex> lock(m_shutdownMutex);
            if (m_workers.empty()) return;
            workers.swap(m_workers);
        }
        if (state) {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->doneCv.wait_until(lock, deadline, [s = state.get()]() {
                return s->aliveWorkers == 0;
            });
        }
        for (auto& w : workers) {
            if (!w.thread.joinable()) continue;
            auto const now = std::chrono::steady_clock::now();
            auto const remaining = now < deadline
                ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                : std::chrono::milliseconds(0);
#ifdef _WIN32
            paimon::joinOrDetachAfter(w.thread, remaining, m_name);
#else
            paimon::joinOrDetachWithWarning(w.thread, remaining, m_name, [&w]() {
                return w.exited && w.exited->load(std::memory_order_acquire);
            });
#endif
        }
        geode::log::info("[ThreadPool] '{}' shut down", m_name);
    }

    void shutdown() {
        waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(2000));
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
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> exited;
    };

    static void workerLoop(std::shared_ptr<SharedState> const& state) {
        if (!state) return;
        auto* s = state.get();
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(s->mutex);
                s->cv.wait(lock, [s]() {
                    return s->stopped.load(std::memory_order_acquire) ||
                           !s->priorityJobs.empty() ||
                           !s->jobs.empty();
                });
                if (s->stopped.load(std::memory_order_acquire) &&
                    s->priorityJobs.empty() && s->jobs.empty()) return;
                if (!s->priorityJobs.empty()) {
                    job = std::move(s->priorityJobs.front());
                    s->priorityJobs.pop();
                } else if (!s->jobs.empty()) {
                    job = std::move(s->jobs.front());
                    s->jobs.pop();
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
            job = {};
        }
    }

    std::string m_name;
    std::shared_ptr<SharedState> m_state;
    std::vector<Worker> m_workers;
    std::mutex m_shutdownMutex;
};

} // namespace paimon
