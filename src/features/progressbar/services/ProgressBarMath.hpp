#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace paimon::progressbar {

// config floats come from a hand-editable JSON file and from sliders, so
// every value is forced finite and in range before it reaches cocos:
// a NaN position or scale makes the bar vanish without a trace.

inline float sanitizeFloat(float value, float fallback, float lo, float hi) {
    if (!std::isfinite(value)) return fallback;
    return value < lo ? lo : (value > hi ? hi : value);
}

// wraps any angle into [0, 360). Non-finite input becomes 0.
inline float wrapRotation(float degrees) {
    if (!std::isfinite(degrees)) return 0.f;
    float wrapped = std::fmod(degrees, 360.f);
    return wrapped < 0.f ? wrapped + 360.f : wrapped;
}

// Frame-rate independent exponential smoothing toward target.
// smoothing in [0, 1]: 0 = instant 1:1 tracking, 1 = silky damp.
inline float dampProgress(float current, float target, float smoothing, float dt) {
    target = std::clamp(target, 0.f, 100.f);
    if (!std::isfinite(current) || smoothing <= 0.001f || dt <= 0.f) {
        return target;
    }
    // Jump immediately when a run resets backwards sharply.
    if (target + 8.f < current) {
        return target;
    }
    float const rate = 38.f - std::clamp(smoothing, 0.f, 1.f) * 32.f; // 38 -> 6 rad/s
    float const alpha = 1.f - std::exp(-rate * std::clamp(dt, 0.f, 0.1f));
    return std::clamp(current + (target - current) * alpha, 0.f, 100.f);
}

} // namespace paimon::progressbar
