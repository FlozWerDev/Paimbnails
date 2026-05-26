#pragma once
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <Geode/Geode.hpp>

namespace paimon {

class ThreadTracker {
private:
    struct TrackedThread {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> completed;
    };

public:
    static ThreadTracker& get() {
        static ThreadTracker instance;
        return instance;
    }

    // Spawn a tracked thread
    template<typename Function, typename... Args>
    void spawn(Function&& f, Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isShuttingDown) {
            geode::log::warn("[ThreadTracker] Rejecting thread spawn: shutting down.");
            return;
        }

        // Clean up completed threads to avoid leak of descriptors
        cleanupNoLock();

        auto completed = std::make_shared<std::atomic<bool>>(false);
        
        std::thread t([completed, f = std::forward<Function>(f), ...args = std::forward<Args>(args)]() mutable {
            try {
                f(std::forward<Args>(args)...);
            } catch (const std::exception& e) {
                geode::log::error("[ThreadTracker] Exception in thread: {}", e.what());
            } catch (...) {
                geode::log::error("[ThreadTracker] Unknown exception in thread.");
            }
            completed->store(true, std::memory_order_release);
        });

        m_threads.push_back({ std::move(t), completed });
    }

    // Check if the thread tracker is shutting down (for DLL unload verification)
    bool isShuttingDown() const {
        return m_isShuttingDown.load(std::memory_order_acquire);
    }

    // Join all threads. Called during shutdown or static destruction.
    void shutdown() {
        bool expected = false;
        if (!m_isShuttingDown.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        std::vector<TrackedThread> threadsToJoin;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            threadsToJoin = std::move(m_threads);
            m_threads.clear();
        }

        geode::log::info("[ThreadTracker] Shutting down. Joining {} active background threads...", threadsToJoin.size());
        for (auto& tt : threadsToJoin) {
            if (tt.thread.joinable()) {
                tt.thread.join();
            }
        }
        geode::log::info("[ThreadTracker] All background threads joined.");
    }

    // Periodic cleanup of completed threads
    void cleanup() {
        std::lock_guard<std::mutex> lock(m_mutex);
        cleanupNoLock();
    }

private:
    ThreadTracker() = default;
    
    ~ThreadTracker() {
        shutdown();
    }

    void cleanupNoLock() {
        for (auto it = m_threads.begin(); it != m_threads.end(); ) {
            if (it->completed->load(std::memory_order_acquire)) {
                if (it->thread.joinable()) {
                    it->thread.join();
                }
                it = m_threads.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<TrackedThread> m_threads;
    std::mutex m_mutex;
    std::atomic<bool> m_isShuttingDown{false};
};

} // namespace paimon
