#pragma once

#include "DiscordIpcClient.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace paimon::discord {

// main-thread producers publish values only. The IPC worker takes the latest
// value and releases the lock before any pipe/socket operation can wait.
class DiscordActivityMailbox {
public:
    bool publish(std::optional<DiscordActivity> activity) {
        {
            std::lock_guard lock(m_mutex);
            if (m_stopped.load(std::memory_order_relaxed)) return false;
            m_pending.emplace(std::move(activity));
        }
        m_ready.notify_one();
        return true;
    }

    bool take(std::optional<DiscordActivity>& activity, std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        m_ready.wait_for(lock, timeout, [this] { return m_stopped.load() || m_pending.has_value() || m_clearRequested.load(); });
        if (m_stopped.load() || !m_pending) return false;
        activity = std::move(*m_pending);
        m_pending.reset();
        return true;
    }

    void stop() {
        {
            std::lock_guard lock(m_mutex);
            m_stopped.store(true, std::memory_order_release);
            m_pending.reset();
        }
        m_ready.notify_one();
    }

    bool stopped() const { return m_stopped.load(std::memory_order_acquire); }
    void requestClear() { m_clearRequested.store(true, std::memory_order_release); m_ready.notify_one(); }
    bool takeClearRequest() { return m_clearRequested.exchange(false, std::memory_order_acq_rel); }
    void setRefreshInterval(int milliseconds) {
        m_refreshInterval.store(std::clamp(milliseconds, 2000, 30000), std::memory_order_relaxed);
    }
    int refreshInterval() const { return m_refreshInterval.load(std::memory_order_relaxed); }
    bool claimRefresh() { return !m_refreshQueued.exchange(true, std::memory_order_acq_rel); }
    void finishRefresh() { m_refreshQueued.store(false, std::memory_order_release); }

private:
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::optional<std::optional<DiscordActivity>> m_pending;
    std::atomic<bool> m_stopped{false};
    std::atomic<bool> m_refreshQueued{false};
    std::atomic<int> m_refreshInterval{5000};
    std::atomic<bool> m_clearRequested{false};
};

} // namespace paimon::discord
