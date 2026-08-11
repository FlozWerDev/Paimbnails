#pragma once

#include <Geode/cocos/CCDirector.h>
#include <algorithm>
#include <cstdint>
#include <chrono>

namespace paimon::framebudget {

// Per-frame microsecond budget for main-thread thumbnail/LevelCell work.
// It limits total work per frame; unused capacity carries no state forward.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
inline constexpr int64_t kFrameBudgetUs = 900;
#else
// ~2 ms per frame keeps large queues from stalling the game.
inline constexpr int64_t kFrameBudgetUs = 2000;
#endif

// Each stage has a reserved slice plus unused shared capacity, so uploads,
// callbacks, and GIF frames all advance within the same total budget.
enum class Stage : int {
    Upload = 0,  // thumbnail GPU texture uploads
    Callback,    // per-cell thumbnail load callbacks
    GifFrame,    // animated GIF frame uploads
    Count
};

inline constexpr int kStageCount = static_cast<int>(Stage::Count);

// Reservations sum below 100%; the remainder is shared. GIFs reserve least
// because playback can fill in after the first frames.
inline constexpr int kStageReservePct[kStageCount] = { 30, 30, 10 };

inline int64_t& usedUsRef() { static int64_t v = 0; return v; }
inline int64_t& frameKeyRef() { static int64_t v = -1; return v; }
inline int64_t* stageUsedUs() { static int64_t v[kStageCount] = {}; return v; }

inline void refresh() {
    auto* dir = cocos2d::CCDirector::sharedDirector();
    int64_t visual = dir ? static_cast<int64_t>(dir->getTotalFrames()) : 0;
    if (visual != frameKeyRef()) {
        frameKeyRef() = visual;
        usedUsRef() = 0;
        auto* staged = stageUsedUs();
        for (int i = 0; i < kStageCount; ++i) staged[i] = 0;
    }
}

inline constexpr int64_t stageReserveUs(Stage stage) {
    return kFrameBudgetUs * kStageReservePct[static_cast<int>(stage)] / 100;
}

// Total microseconds left this frame; callers use it as a busy signal.
inline int64_t remainingUs() {
    refresh();
    return std::max<int64_t>(0, kFrameBudgetUs - usedUsRef());
}

// Capacity available to this stage: its reservation plus genuinely free budget.
inline int64_t remainingUs(Stage stage) {
    refresh();
    auto const* staged = stageUsedUs();
    int const idx = static_cast<int>(stage);

    int64_t const ownUnused = std::max<int64_t>(0, stageReserveUs(stage) - staged[idx]);

    int64_t othersUnused = 0;
    for (int i = 0; i < kStageCount; ++i) {
        if (i == idx) continue;
        othersUnused += std::max<int64_t>(
            0, stageReserveUs(static_cast<Stage>(i)) - staged[i]);
    }

    int64_t const globalLeft = std::max<int64_t>(0, kFrameBudgetUs - usedUsRef());
    return std::max<int64_t>(ownUnused, globalLeft - othersUnused);
}

inline bool hasBudget() {
    return remainingUs() > 0;
}

inline void consume(Stage stage, int64_t us) {
    refresh();
    if (us <= 0) return;
    usedUsRef() += us;
    stageUsedUs()[static_cast<int>(stage)] += us;
}

}
