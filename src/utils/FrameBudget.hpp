#pragma once

#include <Geode/cocos/CCDirector.h>
#include <algorithm>
#include <cstdint>
#include <chrono>

namespace paimon::framebudget {

// Per-frame microsecond budget shared by ALL main-thread thumbnail/LevelCell
// work (GPU uploads, load callbacks, deferred per-cell gradient/extras setup).
// It caps total thumbnail work per frame so loading many thumbnails at once
// doesn't pile up >5ms and tank the FPS; leftover work spreads across following
// frames. Main thread only; the frame key is CCDirector::getTotalFrames(), so
// the budget resets automatically each frame without an explicit frame hook.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
inline constexpr int64_t kFrameBudgetUs = 900;
#else
// ~2ms per frame: enough to make progress without piling up >5ms of thumbnail
// work when the queue is large (list scroll, preload, etc.).
inline constexpr int64_t kFrameBudgetUs = 2000;
#endif

inline int64_t& usedUsRef() { static int64_t v = 0; return v; }
inline int64_t& frameKeyRef() { static int64_t v = -1; return v; }

// reset the counter when entering a new frame
inline void refresh() {
    auto* dir = cocos2d::CCDirector::sharedDirector();
    int64_t visual = dir ? static_cast<int64_t>(dir->getTotalFrames()) : 0;
    if (visual != frameKeyRef()) {
        frameKeyRef() = visual;
        usedUsRef() = 0;
    }
}

// microseconds left in the thumbnail budget for this frame
inline int64_t remainingUs() {
    refresh();
    return std::max<int64_t>(0, kFrameBudgetUs - usedUsRef());
}

inline bool hasBudget() {
    return remainingUs() > 0;
}

// record microseconds consumed by thumbnail work this frame
inline void consume(int64_t us) {
    refresh();
    if (us > 0) usedUsRef() += us;
}

} // namespace paimon::framebudget
