#pragma once

#include "DynamicVolumeTypes.hpp"

namespace paimon::dynvol {

struct SafeDropLevels {
    float rmsDb  = kInvalidLufs;
    float peakDb = kInvalidLufs;
    float lufs   = kInvalidLufs;
};

class SafeDrop {
public:
    void reset();
    float update(SafeDropLevels const& levels, DynamicVolumeConfig const& cfg, float dt);

    float gainDb() const { return m_gainDb; }

private:
    float m_fastRmsDb       = kInvalidLufs;
    float m_rmsReferenceDb  = kInvalidLufs;
    float m_peakReferenceDb = kInvalidLufs;
    float m_lufsReference   = kInvalidLufs;
    float m_gainDb          = 0.0f;
    float m_holdSeconds     = 0.0f;
};

} // namespace paimon::dynvol
