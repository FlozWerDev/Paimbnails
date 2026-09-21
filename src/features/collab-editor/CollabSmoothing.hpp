#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::collab {

// Half-life instead of per-frame lerp: half the distance closes every N seconds
// regardless of 30, 60 or 144 FPS.
inline float smoothingAlpha(float dt, float halfLife) {
    if (!std::isfinite(dt) || dt <= 0.f) return 0.f;
    if (!std::isfinite(halfLife) || halfLife <= 0.f) return 1.f;

    // A long suspended frame should catch up without feeding an extreme value
    // into exp2. Normal render deltas are far below this bound.
    dt = std::min(dt, 0.25f);
    return 1.f - std::exp2(-dt / halfLife);
}

} // namespace paimon::collab
