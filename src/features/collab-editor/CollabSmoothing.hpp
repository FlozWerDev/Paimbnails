#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::collab {

// half-life, not per-frame lerp: closes half the distance every n seconds at any fps.
inline float smoothingAlpha(float dt, float halfLife) {
    if (!std::isfinite(dt) || dt <= 0.f) return 0.f;
    if (!std::isfinite(halfLife) || halfLife <= 0.f) return 1.f;

    // clamp suspended-frame deltas; extremes would poison exp2.
    dt = std::min(dt, 0.25f);
    return 1.f - std::exp2(-dt / halfLife);
}

} // namespace paimon::collab
