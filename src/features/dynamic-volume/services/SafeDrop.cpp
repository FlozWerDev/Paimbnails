#include "SafeDrop.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::dynvol {

namespace {

constexpr float kBodyThresholdDb       = 2.5f;
constexpr float kPeakThresholdDb       = 7.0f;
constexpr float kFastAttackSeconds     = 0.012f;
constexpr float kFastReleaseSeconds    = 0.12f;
constexpr float kReferenceFallSeconds  = 0.28f;
constexpr float kGainAttackSeconds     = 0.01f;
constexpr float kGainReleaseSeconds    = 0.16f;
constexpr float kHoldSeconds           = 0.09f;
constexpr float kRampTimeConstants     = 4.6f;

float approach(float current, float target, float dt, float seconds) {
    float const alpha = 1.0f - std::exp(-std::max(0.0f, dt) / std::max(0.001f, seconds));
    return current + (target - current) * alpha;
}

float updateReference(float value, float& reference, float dt, float recoverySeconds) {
    if (!isValidLufs(value)) return 0.0f;
    if (!isValidLufs(reference)) {
        reference = value;
        return 0.0f;
    }

    float const seconds = value > reference ? recoverySeconds : kReferenceFallSeconds;
    reference = approach(reference, value, dt, seconds);
    return value - reference;
}

} // namespace

void SafeDrop::reset() {
    m_fastRmsDb       = kInvalidLufs;
    m_rmsReferenceDb  = kInvalidLufs;
    m_peakReferenceDb = kInvalidLufs;
    m_lufsReference   = kInvalidLufs;
    m_gainDb           = 0.0f;
    m_holdSeconds      = 0.0f;
}

float SafeDrop::update(SafeDropLevels const& levels,
                       DynamicVolumeConfig const& cfg, float dt) {
    float const frameDt = std::clamp(dt, 0.0f, 0.1f);
    float const recoverySeconds = std::max(
        kReferenceFallSeconds, cfg.rampSeconds / kRampTimeConstants);

    float rmsRise = 0.0f;
    if (isValidLufs(levels.rmsDb)) {
        if (!isValidLufs(m_fastRmsDb)) {
            m_fastRmsDb = levels.rmsDb;
        } else {
            float const seconds = levels.rmsDb > m_fastRmsDb
                ? kFastAttackSeconds : kFastReleaseSeconds;
            m_fastRmsDb = approach(m_fastRmsDb, levels.rmsDb, frameDt, seconds);
        }
        rmsRise = updateReference(
            m_fastRmsDb, m_rmsReferenceDb, frameDt, recoverySeconds);
    }

    float const lufsRise = updateReference(
        levels.lufs, m_lufsReference, frameDt, recoverySeconds);
    float const peakRise = updateReference(
        levels.peakDb, m_peakReferenceDb, frameDt, recoverySeconds);

    float const bodyThreshold = std::max(kBodyThresholdDb, cfg.thresholdDb);
    float const bodyRise = std::max(rmsRise, lufsRise);
    float const bodyCut = std::max(0.0f, bodyRise - bodyThreshold);

    float const peakThreshold = std::max(kPeakThresholdDb, bodyThreshold + 4.0f);
    float const peakWeight = bodyRise >= bodyThreshold * 0.5f ? 1.0f : 0.35f;
    float const peakCut = std::max(0.0f, peakRise - peakThreshold) * peakWeight;

    float const desired = -std::clamp(
        std::max(bodyCut, peakCut), 0.0f, std::abs(cfg.maxCutDb));

    if (desired < m_gainDb) {
        m_gainDb = approach(m_gainDb, desired, frameDt, kGainAttackSeconds);
        m_holdSeconds = kHoldSeconds;
    } else if (m_holdSeconds > 0.0f) {
        m_holdSeconds = std::max(0.0f, m_holdSeconds - frameDt);
    } else {
        m_gainDb = approach(m_gainDb, desired, frameDt, kGainReleaseSeconds);
    }

    if (std::abs(m_gainDb) < 0.01f) m_gainDb = 0.0f;
    return m_gainDb;
}

} // namespace paimon::dynvol
