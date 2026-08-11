// Pure dynamic-volume config and curve/gain math.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace paimon::dynvol {

enum class Mode {
    Adaptive,
    Fixed,
    Custom,
};

enum class Curve {
    Linear,
    EaseIn,
    EaseOut,
    SmoothStep,
    Exponential,
    Steps,
};

struct DynamicVolumeConfig {
    bool enabled = false;
    Mode mode    = Mode::Adaptive;

// Ramp back to full volume.
    float rampSeconds   = 8.0f;   // 1 .. 60
    Curve curve         = Curve::EaseOut;
    float curveStrength = 2.0f;   // 1 .. 6

// Initial attenuation before the meter has enough signal.
    float initialDuckDb = -7.0f;  // -24 .. 0

// Match-gain clamps in positive dB magnitudes.
    float maxCutDb   = 18.0f;     // 0 .. 30
    float maxBoostDb = 0.0f;      // 0 .. 12  (0 = never make quiet songs louder)

// Ignore differences below this threshold.
    float thresholdDb = 2.0f;     // 0 .. 12

// Fixed-mode LUFS target.
    float targetLufs = -14.0f;    // -30 .. -6

// Measurement window before locking loudness.
    float analysisSeconds = 0.9f; // 0.4 .. 5
// Gain slew time; avoids clicks.
    float smoothingSeconds = 0.35f; // 0.05 .. 2

    bool inMenus    = true;
    bool inGameplay = true;
    bool inEditor   = false;

// Re-apply attenuation when the same track restarts.
    bool reduckSameSong = false;
};

// Treat lower readings as silence.
inline constexpr float kInvalidLufs = -70.0f;

inline bool isValidLufs(float lufs) {
    return std::isfinite(lufs) && lufs > kInvalidLufs;
}

// Restore fraction at normalized time t.
inline float curveProgress(Curve curve, float t, float strength) {
    t = std::clamp(t, 0.0f, 1.0f);
    float const s = std::clamp(strength, 1.0f, 6.0f);

    switch (curve) {
        case Curve::Linear:
            return t;
        case Curve::EaseIn:
            return std::pow(t, s);
        case Curve::EaseOut:
            return 1.0f - std::pow(1.0f - t, s);
        case Curve::SmoothStep: {
// Symmetric curve shaped by strength.
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            float const a = std::pow(t, s);
            float const b = std::pow(1.0f - t, s);
            return a / (a + b);
        }
        case Curve::Exponential: {
            float const d = std::exp(s) - 1.0f;
            if (d <= 0.0f) return t;
            return (std::exp(s * t) - 1.0f) / d;
        }
        case Curve::Steps: {
            int const steps = std::clamp(static_cast<int>(std::lround(s * 2.0f)), 2, 12);
            float const stepped = std::floor(t * static_cast<float>(steps))
                                / static_cast<float>(steps - 1);
            return std::clamp(stepped, 0.0f, 1.0f);
        }
    }
    return t;
}

// Match songLufs to referenceLufs within the dead zone and clamps.
inline float matchGainDb(DynamicVolumeConfig const& cfg, float songLufs, float referenceLufs) {
    if (!isValidLufs(songLufs) || !isValidLufs(referenceLufs)) return 0.0f;

    float const diff = referenceLufs - songLufs;
    if (std::abs(diff) < std::max(0.0f, cfg.thresholdDb)) return 0.0f;

// Remove the dead zone while keeping correction continuous.
    float const trimmed = diff > 0.0f ? diff - cfg.thresholdDb : diff + cfg.thresholdDb;

    return std::clamp(trimmed,
                      -std::abs(cfg.maxCutDb),
                      std::abs(cfg.maxBoostDb));
}

// Gain after elapsed seconds; Fixed mode never climbs above floorDb.
inline float rampGainDb(DynamicVolumeConfig const& cfg, float floorDb, float elapsed) {
    if (cfg.mode == Mode::Fixed) return floorDb;

    float const ramp = std::max(0.01f, cfg.rampSeconds);
    float const t = std::clamp(elapsed / ramp, 0.0f, 1.0f);
    return floorDb * (1.0f - curveProgress(cfg.curve, t, cfg.curveStrength));
}

// Pin hidden mode-specific knobs without overwriting UI values.
inline void applyModeDefaults(DynamicVolumeConfig& cfg) {
    switch (cfg.mode) {
        case Mode::Adaptive:
            cfg.curve            = Curve::EaseOut;
            cfg.curveStrength    = 2.0f;
            cfg.initialDuckDb    = -7.0f;
            cfg.maxBoostDb       = 0.0f;
            cfg.thresholdDb      = 2.0f;
            cfg.analysisSeconds  = 0.9f;
            cfg.smoothingSeconds = 0.35f;
            break;
        case Mode::Fixed:
            cfg.curve            = Curve::Linear;
            cfg.initialDuckDb    = -5.0f;
            cfg.maxBoostDb       = 8.0f;
            cfg.thresholdDb      = 1.0f;
            cfg.analysisSeconds  = 1.4f;
            cfg.smoothingSeconds = 0.5f;
            break;
        case Mode::Custom:
            break;
    }
}

inline char const* modeId(Mode m) {
    switch (m) {
        case Mode::Adaptive: return "adaptive";
        case Mode::Fixed:    return "fixed";
        case Mode::Custom:   return "custom";
    }
    return "adaptive";
}

inline Mode modeFromId(std::string const& id) {
    if (id == "fixed")  return Mode::Fixed;
    if (id == "custom") return Mode::Custom;
    return Mode::Adaptive;
}

inline char const* curveId(Curve c) {
    switch (c) {
        case Curve::Linear:      return "linear";
        case Curve::EaseIn:      return "ease-in";
        case Curve::EaseOut:     return "ease-out";
        case Curve::SmoothStep:  return "smooth";
        case Curve::Exponential: return "exponential";
        case Curve::Steps:       return "steps";
    }
    return "ease-out";
}

inline Curve curveFromId(std::string const& id) {
    if (id == "linear")      return Curve::Linear;
    if (id == "ease-in")     return Curve::EaseIn;
    if (id == "smooth")      return Curve::SmoothStep;
    if (id == "exponential") return Curve::Exponential;
    if (id == "steps")       return Curve::Steps;
    return Curve::EaseOut;
}

}
