#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::collab {

// Half-life, not per-frame lerp: closes half the distance every N seconds at any FPS.
inline float smoothingAlpha(float dt, float halfLife) {
    if (!std::isfinite(dt) || dt <= 0.f) return 0.f;
    if (!std::isfinite(halfLife) || halfLife <= 0.f) return 1.f;

    // Clamp suspended-frame deltas; extremes would poison exp2.
    dt = std::min(dt, 0.25f);
    return 1.f - std::exp2(-dt / halfLife);
}

} // namespace paimon::collab
