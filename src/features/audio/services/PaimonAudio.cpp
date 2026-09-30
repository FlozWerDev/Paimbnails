#include "PaimonAudio.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/loader/Log.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {

// bins assume 512-point fft at 44.1khz.
float bandAverage(float const* spectrum, int numBins, int lo, int hi) {
    int start = std::min(lo, numBins);
    int end = std::min(hi, numBins);
    if (end <= start) return 0.f;
    float sum = 0.f;
    for (int i = start; i < end; i++) sum += spectrum[i];
    return sum / (end - start);
}

} // namespace

PaimonAudio& PaimonAudio::get() {
    static PaimonAudio instance;
    return instance;
}

void PaimonAudio::resetValues() {
    m_smoothBass   = 0.f;
    m_smoothMid    = 0.f;
    m_smoothTreble = 0.f;
    m_prevBass     = 0.f;
    m_beatPulse    = 0.f;
    m_energy       = 0.f;
    m_peakBass     = 0.01f;
    m_peakMid      = 0.01f;
    m_peakTreble   = 0.01f;
}

void PaimonAudio::activate() {
    if (m_active) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) {
        log::warn("[PaimonAudio] No FMOD engine/system/channel available");
        return;
    }

    if (!m_fftDSP) {
        FMOD_RESULT res = engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &m_fftDSP);
        if (res != FMOD_OK || !m_fftDSP) {
            log::error("[PaimonAudio] Failed to create FFT DSP (result={})", static_cast<int>(res));
            m_fftDSP = nullptr;
            return;
        }
        m_fftDSP->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, 512);
    }

    FMOD_RESULT res = engine->m_backgroundMusicChannel->addDSP(2, m_fftDSP);
    if (res != FMOD_OK) {
        log::warn("[PaimonAudio] Failed to add FFT DSP to channel (result={})", static_cast<int>(res));
        // dsp may already be attached — continue anyway
    }

    resetValues();
    m_active = true;
    log::info("[PaimonAudio] Activated - FFT DSP attached to background music channel");
}

void PaimonAudio::deactivate() {
    if (!m_active) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel && m_fftDSP) {
        engine->m_backgroundMusicChannel->removeDSP(m_fftDSP);
    }
    if (m_fftDSP) {
        m_fftDSP->release();
        m_fftDSP = nullptr;
    }

    resetValues();
    m_active = false;
    log::info("[PaimonAudio] Deactivated - FFT DSP released");
}

void PaimonAudio::update(float dt) {
    if (!m_active || !m_fftDSP) {
        resetValues();
        return;
    }

    FMOD_DSP_PARAMETER_FFT* fftData = nullptr;
    FMOD_RESULT res = m_fftDSP->getParameterData(
        FMOD_DSP_FFT_SPECTRUMDATA, (void**)&fftData, nullptr, nullptr, 0);

    if (res != FMOD_OK || !fftData || fftData->numchannels < 1 || fftData->length < 1) {
        // no audio data available — decay smoothly
        m_smoothBass   *= std::max(0.f, 1.f - dt * 6.f);
        m_smoothMid    *= std::max(0.f, 1.f - dt * 6.f);
        m_smoothTreble *= std::max(0.f, 1.f - dt * 6.f);
        m_beatPulse     = std::max(0.f, m_beatPulse - dt * 3.5f);
        m_energy       *= std::max(0.f, 1.f - dt * 6.f);
        return;
    }

    int numBins = fftData->length;
    float const* spectrum = fftData->spectrum[0];


    float rawBass = bandAverage(spectrum, numBins, 0, 8);
    float rawMid = bandAverage(spectrum, numBins, 8, 48);
    float rawTreble = bandAverage(spectrum, numBins, 48, 128);

    // adaptive peak tracking (slow decay, fast attack)
    m_peakBass   = std::max(m_peakBass   * (1.f - dt * 0.3f), rawBass   + 0.001f);
    m_peakMid    = std::max(m_peakMid    * (1.f - dt * 0.3f), rawMid    + 0.001f);
    m_peakTreble = std::max(m_peakTreble * (1.f - dt * 0.3f), rawTreble + 0.001f);

    // normalize relative to running peak (keeps full 0-1 range at any volume)
    float normBass   = rawBass   / m_peakBass;
    float normMid    = rawMid    / m_peakMid;
    float normTreble = rawTreble / m_peakTreble;

    m_smoothBass   += (normBass - m_smoothBass)     * std::min(1.f, dt * 10.f);
    m_smoothMid    += (normMid - m_smoothMid)       * std::min(1.f, dt * 12.f);
    m_smoothTreble += (normTreble - m_smoothTreble) * std::min(1.f, dt * 14.f);

    // beat detection (onset relative to smoothed value, not absolute)
    float delta = normBass - m_prevBass;
    m_prevBass = normBass;

    if (delta > 0.08f) {
        m_beatPulse = std::min(1.f, m_beatPulse + delta * 3.5f);
    }
    m_beatPulse = std::max(0.f, m_beatPulse - dt * 4.0f);

    m_energy = m_smoothBass * 0.5f + m_smoothMid * 0.3f + m_smoothTreble * 0.2f;
}
