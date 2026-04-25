#pragma once

#include <fmod.hpp>

class PaimonAudio {
public:
    static PaimonAudio& get();

    void activate();
    void deactivate();
    void update(float dt);

    bool isActive() const { return m_active; }

    float bass()      const { return m_smoothBass; }
    float mid()       const { return m_smoothMid; }
    float treble()    const { return m_smoothTreble; }
    float beatPulse() const { return m_beatPulse; }
    float energy()    const { return m_energy; }

private:
    PaimonAudio() = default;

    FMOD::DSP* m_fftDSP = nullptr;
    bool m_active = false;

    // smoothed band values (log-compressed, typically 0-3+)
    float m_smoothBass   = 0.f;
    float m_smoothMid    = 0.f;
    float m_smoothTreble = 0.f;

    // running peak for adaptive normalization
    float m_peakBass   = 0.01f;
    float m_peakMid    = 0.01f;
    float m_peakTreble = 0.01f;

    // beat detection
    float m_prevBass  = 0.f;
    float m_beatPulse = 0.f;

    // combined energy
    float m_energy = 0.f;

    void resetValues();
};
