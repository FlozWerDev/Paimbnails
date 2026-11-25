#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <Geode/Geode.hpp>
#include "JoinWithWarning.hpp"

namespace paimon {

class ThreadTracker {
public:
    enum class ShutdownPolicy {
        Join,
        BestEffort,
    };

private:
    struct TrackedThread {
        std::string name;
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> completed;
        ShutdownPolicy policy = ShutdownPolicy::Join;
    };

    struct DoneSignal {
        std::mutex mutex;
        std::condition_variable cv;
        uint64_t seq = 0;
    };

public:
    static ThreadTracker& get() {
        // Heap-leaked: ~ThreadTracker() must never run under Windows Loader Lock in DllMain.
        static auto* instance = new ThreadTracker();
        return *instance;
    }

    // false during shutdown; caller has to handle that.
    template<typename Function, typename... Args>
    bool spawnWithPolicy(std::string_view name, ShutdownPolicy policy, Function&& f, Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isShuttingDown.load(std::memory_order_acquire)) {
            geode::log::warn("[ThreadTracker] Rejecting thread spawn ('{}'): shutting down.", name);
            return false;
        }

        cleanupNoLock();
        if (m_threads.size() == m_threads.capacity()) {
            m_threads.reserve(m_threads.empty() ? 4 : m_threads.size() * 2);
        }

        auto completed = std::make_shared<std::atomic<bool>>(false);
        auto doneSignal = m_doneSignal;
        std::string nameCopy(name.empty() ? std::string_view("worker") : name);

        auto work = [f = std::forward<Function>(f), ...args = std::forward<Args>(args)]() mutable {
            f(std::forward<Args>(args)...);
        };
        std::thread t([completed, doneSignal, nameCopy, work = std::optional{std::move(work)}]() mutable {
            try {
                (*work)();
            } catch (const std::exception& e) {
                geode::log::error("[ThreadTracker] Exception in thread '{}': {}", nameCopy, e.what());
            } catch (...) {
                geode::log::error("[ThreadTracker] Unknown exception in thread '{}'.", nameCopy);
            }
            // destroy captured state before marking done: destructors can be slow.
            work.reset();
            completed->store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> doneLock(doneSignal->mutex);
                ++doneSignal->seq;
            }
            doneSignal->cv.notify_all();
        });

        m_threads.push_back({ std::move(nameCopy), std::move(t), std::move(completed), policy });
        return true;
    }

    template<typename Function, typename... Args>
    bool spawn(Function&& f, Args&&... args) {
        return spawnWithPolicy("worker", ShutdownPolicy::Join, std::forward<Function>(f), std::forward<Args>(args)...);
    }

    bool isShuttingDown() const {
        return m_isShuttingDown.load(std::memory_order_acquire);
    }

    void requestShutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isShuttingDown.store(true, std::memory_order_release);
    }

    void waitShutdown(
        std::chrono::steady_clock::time_point deadline,
        std::chrono::milliseconds bestEffortGrace = std::chrono::milliseconds(150)
    ) {
        std::vector<TrackedThread> threadsToJoin;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isShuttingDown.store(true, std::memory_order_release);
            threadsToJoin = std::move(m_threads);
            m_threads.clear();
        }

        if (threadsToJoin.empty()) {
            return;
        }

        std::vector<TrackedThread> remaining;
        remaining.reserve(threadsToJoin.size());
        for (auto& tt : threadsToJoin) {
            if (!tt.thread.joinable()) continue;
            if (tryReapCompleted(tt)) continue;
            remaining.push_back(std::move(tt));
        }

        if (remaining.empty()) {
            geode::log::info("[ThreadTracker] All {} background thread(s) already finished.", threadsToJoin.size());
            return;
        }

        geode::log::info("[ThreadTracker] Waiting on {} active background thread(s)...", remaining.size());

        auto const bestEffortDeadline = std::min(deadline, std::chrono::steady_clock::now() + bestEffortGrace);

        while (!remaining.empty()) {
            uint64_t observedSeq = 0;
            {
                std::lock_guard<std::mutex> doneLock(m_doneSignal->mutex);
                observedSeq = m_doneSignal->seq;
            }

            bool anyPendingOsFinish = false;
            size_t joinRemaining = 0;
            for (auto it = remaining.begin(); it != remaining.end(); ) {
                if (!it->thread.joinable()) {
                    it = remaining.erase(it);
                    continue;
                }
                if (it->completed->load(std::memory_order_acquire)) {
#ifdef _WIN32
                    if (WaitForSingleObject(it->thread.native_handle(), 0) != WAIT_OBJECT_0) {
                        anyPendingOsFinish = true;
                        if (it->policy == ShutdownPolicy::Join) ++joinRemaining;
                        ++it;
                        continue;
                    }
#endif
                    it->thread.join();
                    it = remaining.erase(it);
                } else {
                    if (it->policy == ShutdownPolicy::Join) ++joinRemaining;
                    ++it;
                }
            }

            if (remaining.empty()) break;

            auto const now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            if (joinRemaining == 0 && !anyPendingOsFinish && now >= bestEffortDeadline) break;

            auto wakeAt = (joinRemaining > 0) ? deadline : bestEffortDeadline;
            if (anyPendingOsFinish) {
                wakeAt = std::min(deadline, now + std::chrono::milliseconds(2));
            }
            if (wakeAt <= now) break;

            std::unique_lock<std::mutex> doneLock(m_doneSignal->mutex);
            m_doneSignal->cv.wait_until(doneLock, wakeAt, [this, observedSeq]() {
                return m_doneSignal->seq != observedSeq;
            });
        }

        if (!remaining.empty()) {
            std::string detachedNames;
            for (size_t i = 0; i < remaining.size(); ++i) {
                if (i > 0) detachedNames += ", ";
                detachedNames += remaining[i].name;
                detachedNames += (remaining[i].policy == ShutdownPolicy::BestEffort) ? " [BestEffort]" : " [Join]";
            }
            geode::log::warn(
                "[ThreadTracker] Detaching {} background thread(s) after shutdown budget: {}",
                remaining.size(),
                detachedNames
            );
            for (auto& tt : remaining) {
                if (tt.thread.joinable()) {
                    tt.thread.detach();
                }
            }
        } else {
            geode::log::info("[ThreadTracker] All background threads joined.");
        }
    }

    void shutdown(
        std::chrono::milliseconds totalBudget = std::chrono::milliseconds(1500),
        std::chrono::milliseconds bestEffortGrace = std::chrono::milliseconds(150)
    ) {
        waitShutdown(std::chrono::steady_clock::now() + totalBudget, bestEffortGrace);
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(m_mutex);
        cleanupNoLock();
    }

private:
    ThreadTracker() = default;

    ~ThreadTracker() {
        shutdown();
    }

    static bool tryReapCompleted(TrackedThread& tt) {
        if (!tt.completed->load(std::memory_order_acquire)) return false;
#ifdef _WIN32
        if (WaitForSingleObject(tt.thread.native_handle(), 0) != WAIT_OBJECT_0) {
            return false;
        }
#endif
        tt.thread.join();
        return true;
    }

    void cleanupNoLock() {
        for (auto it = m_threads.begin(); it != m_threads.end(); ) {
            if (it->completed->load(std::memory_order_acquire)) {
#ifdef _WIN32
                // body done, but tls/crt teardown may still be running. never block here.
                if (it->thread.joinable() &&
                    WaitForSingleObject(it->thread.native_handle(), 0) != WAIT_OBJECT_0) {
                    ++it;
                    continue;
                }
#endif
                // never join here: this runs from ui callbacks and must not block.
                // real joins happen in shutdown().
                if (it->thread.joinable()) {
                    it->thread.detach();
                }
                it = m_threads.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<TrackedThread> m_threads;
    std::mutex m_mutex;
    std::shared_ptr<DoneSignal> m_doneSignal = std::make_shared<DoneSignal>();
    std::atomic<bool> m_isShuttingDown{false};
};

} // namespace paimon
