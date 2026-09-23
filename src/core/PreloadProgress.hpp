#pragma once

#include <atomic>

namespace paimon::preload {

inline std::atomic<int> g_thumbsTotal{0};
inline std::atomic<int> g_thumbsLoaded{0};

inline std::atomic<bool> g_preloadStarted{false};

// Set from $on_game(Loaded). Deferred preload work waits on it so it never
// competes with the game's own asset loading on slow machines.
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
