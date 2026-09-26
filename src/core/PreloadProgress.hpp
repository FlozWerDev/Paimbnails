#pragma once

#include <atomic>

namespace paimon::preload {

inline std::atomic<int> g_thumbsTotal{0};
inline std::atomic<int> g_thumbsLoaded{0};

inline std::atomic<bool> g_preloadStarted{false};

// set on $on_game(Loaded); deferred work waits so preload never races game loading.
inline std::atomic<bool> g_gameLoaded{false};

inline int getTotalLoaded() {
    return g_thumbsLoaded.load(std::memory_order_relaxed);
}

inline int getTotalCount() {
    return g_thumbsTotal.load(std::memory_order_relaxed);
}

inline bool isFinished() {
    int total = getTotalCount();
    return total > 0 && getTotalLoaded() >= total;
}

inline bool tryClaimPreload() {
    bool expected = false;
    return g_preloadStarted.compare_exchange_strong(expected, true);
}

} // namespace paimon::preload
