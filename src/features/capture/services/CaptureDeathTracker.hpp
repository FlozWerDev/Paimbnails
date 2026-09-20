#pragma once

#include <atomic>
#include <cstdint>

// Detects deaths via destroyPlayer's progress tick (not m_isDead, which
// noclip mods clear immediately). currentProgress resets per attempt, so stale
// ticks from previous attempts naturally fail the guard.
namespace paimon::capture {

inline std::atomic<int64_t>& lastDeathTickRef() {
    static std::atomic<int64_t> tick{-1};
    return tick;
}

inline void recordDeathTick(uint32_t tick) {
    lastDeathTickRef().store(static_cast<int64_t>(tick), std::memory_order_relaxed);
}

inline void clearDeathTick() {
    lastDeathTickRef().store(-1, std::memory_order_relaxed);
}

// Deaths at 240 physics steps/s can sit a few rendering frames behind the
// capture read; keep ~28 ticks so a level restarted right after dying still
// rejects the stale frame instead of thumbnailing the death pose.
inline bool hasRecentDeath(uint32_t currentTick, uint32_t window = 28) {
    int64_t recorded = lastDeathTickRef().load(std::memory_order_relaxed);
    if (recorded < 0) return false;
    auto r = static_cast<uint64_t>(recorded);
    if (currentTick < r) return false; // new attempt / progress rewound
    return (static_cast<uint64_t>(currentTick) - r) <= window;
}

} // namespace paimon::capture
