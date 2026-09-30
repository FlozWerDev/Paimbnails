#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

namespace paimon::audio {

inline void attachCaveEffects(FMODAudioEngine* engine, FMOD::DSP*& lowpass, FMOD::DSP*& reverb) {
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;

    if (!lowpass) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &lowpass);
        if (lowpass) {
            lowpass->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1200.f);
            lowpass->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
        }
    }
    if (!reverb) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_SFXREVERB, &reverb);
        if (reverb) {
            reverb->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 2500.f);
            reverb->setParameterFloat(FMOD_DSP_SFXREVERB_EARLYDELAY, 20.f);
            reverb->setParameterFloat(FMOD_DSP_SFXREVERB_LATEDELAY, 40.f);
            reverb->setParameterFloat(FMOD_DSP_SFXREVERB_HFREFERENCE, 3000.f);
            reverb->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, -4.f);
            reverb->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, -8.f);
        }
    }
    if (lowpass) engine->m_backgroundMusicChannel->addDSP(0, lowpass);
    if (reverb) engine->m_backgroundMusicChannel->addDSP(1, reverb);
}

inline void releaseCaveEffects(FMODAudioEngine* engine, FMOD::DSP*& lowpass, FMOD::DSP*& reverb) {
    if (engine && engine->m_backgroundMusicChannel) {
        if (lowpass) engine->m_backgroundMusicChannel->removeDSP(lowpass);
        if (reverb) engine->m_backgroundMusicChannel->removeDSP(reverb);
    }
    if (lowpass) {
        lowpass->release();
        lowpass = nullptr;
    }
    if (reverb) {
        reverb->release();
        reverb = nullptr;
    }
}

} // namespace paimon::audio
