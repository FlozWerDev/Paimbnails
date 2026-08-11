#include "DynamicSongSubmerge.hpp"

#include "DynamicSongConfig.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::dynsong {

namespace {

constexpr float kDryCutoffHz = 22000.f;

// SFXREVERB takes its wet level in dB; -80 is silence.
float wetLevelDb(float percent) {
    if (percent <= 0.01f) return -80.f;
    return std::clamp(20.f * std::log10(percent / 100.f), -80.f, 0.f);
}

// Frequency reads logarithmically, so a linear slide from 22 kHz to 500 Hz
// spends almost the whole ramp in a range nobody can hear moving.
float glideHz(float dry, float wetHz, float t) {
    if (t <= 0.f) return dry;
    if (t >= 1.f) return wetHz;
    return dry * std::pow(wetHz / dry, t);
}

// Ticker for the wetness ramp. Registered straight with the scheduler because
// the node never joins the scene tree (same trick the fade node uses).
class SubmergeTickerNode : public cocos2d::CCNode {
public:
    static SubmergeTickerNode* create() {
        auto* node = new SubmergeTickerNode();
        if (node && node->init()) return node; // retained by hand, no autorelease
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void start() {
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(SubmergeTickerNode::onTick), this);
        scheduler->scheduleSelector(
            schedule_selector(SubmergeTickerNode::onTick),
            this, 0.0f, kCCRepeatForever, 0.0f, false
        );
    }

    void stop() {
        auto* scheduler = cocos2d::CCDirector::get()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(SubmergeTickerNode::onTick), this);
    }

private:
    void onTick(float dt) {
        if (paimon::isRuntimeShuttingDown()) {
            stop();
            return;
        }
        SubmergeEffect::get().tick(dt);
    }
};

} // namespace

SubmergeEffect& SubmergeEffect::get() {
    static SubmergeEffect instance;
    return instance;
}

void SubmergeEffect::bindTarget(FMOD::ChannelControl* target) {
    if (m_target == target) return;
    // Whatever was playing through the old target keeps its dry signal.
    if (m_attached && m_attached != target) detachDsps();
    m_target = target;
    if (m_target && m_wet > 0.0001f && ensureDsps()) applyWetness(m_wet);
}

void SubmergeEffect::rampTo(float wet, float seconds) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

    wet = std::clamp(wet, 0.f, 1.f);
    if (seconds <= 0.016f) {
        snapTo(wet);
        return;
    }

    m_from = m_wet;
    m_to = wet;
    m_elapsed = 0.f;
    m_duration = seconds;
    m_ramping = true;

    if (m_to > m_from && !ensureDsps()) {
        // No DSPs available: stay dry rather than pretend.
        m_ramping = false;
        return;
    }
    startTicker();
}

void SubmergeEffect::snapTo(float wet) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

    m_ramping = false;
    m_wet = std::clamp(wet, 0.f, 1.f);

    if (m_wet <= 0.0001f) {
        stopTicker();
        detachDsps();
        return;
    }
    if (!ensureDsps()) {
        m_wet = 0.f;
        return;
    }
    applyWetness(m_wet);
}

void SubmergeEffect::release() {
    m_ramping = false;
    m_wet = 0.f;
    destroyTicker();
    detachDsps();
}

void SubmergeEffect::shutdown() {
    m_shuttingDown = true;
    m_ramping = false;
    m_wet = 0.f;
    destroyTicker();
    detachDsps();
    m_target = nullptr;
}

void SubmergeEffect::tick(float dt) {
    if (!m_ramping) {
        if (m_wet <= 0.0001f) stopTicker();
        return;
    }
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) {
        release();
        return;
    }

    m_elapsed += dt;
    float const t = std::clamp(m_elapsed / std::max(m_duration, 0.016f), 0.f, 1.f);
    // Quadratic ease-in-out, same shape as the volume fade so a dive that runs
    // alongside one moves with it.
    float const eased = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);

    m_wet = std::clamp(m_from + (m_to - m_from) * eased, 0.f, 1.f);
    applyWetness(m_wet);

    if (t < 1.f) return;

    m_ramping = false;
    m_wet = m_to;
    if (m_wet <= 0.0001f) {
        stopTicker();
        detachDsps();
    }
}

bool SubmergeEffect::ensureDsps() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return false;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    // No explicit target means the shared music group, which is where a local
    // song plays.
    auto* target = m_target ? m_target
                            : static_cast<FMOD::ChannelControl*>(engine->m_backgroundMusicChannel);
    if (!target) return false;

    if (m_attached && m_attached != target) detachDsps();
    if (m_attached == target && m_lowpassDsp && m_highpassDsp && m_reverbDsp && m_gainDsp) {
        return true;
    }

    auto create = [&](FMOD_DSP_TYPE type, FMOD::DSP*& out) {
        return out || engine->m_system->createDSPByType(type, &out) == FMOD_OK;
    };
    if (!create(FMOD_DSP_TYPE_LOWPASS, m_lowpassDsp)
        || !create(FMOD_DSP_TYPE_HIGHPASS, m_highpassDsp)
        || !create(FMOD_DSP_TYPE_SFXREVERB, m_reverbDsp)
        || !create(FMOD_DSP_TYPE_FADER, m_gainDsp)) {
        log::warn("[DynSong] submerge: an FMOD effect is unavailable");
        detachDsps();
        return false;
    }

    // Added head-first, so the last one in (gain) is applied last.
    FMOD::DSP* chain[] = {m_lowpassDsp, m_highpassDsp, m_reverbDsp, m_gainDsp};
    for (auto* dsp : chain) {
        if (target->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, dsp) != FMOD_OK) {
            log::warn("[DynSong] submerge: could not attach the effect chain");
            detachDsps();
            return false;
        }
        dsp->setActive(true);
    }

    m_attached = target;
    return true;
}

void SubmergeEffect::detachDsps() {
    if (m_attached) {
        m_attached->setPitch(1.f);
        if (m_lowpassDsp)  m_attached->removeDSP(m_lowpassDsp);
        if (m_highpassDsp) m_attached->removeDSP(m_highpassDsp);
        if (m_reverbDsp)   m_attached->removeDSP(m_reverbDsp);
        if (m_gainDsp)     m_attached->removeDSP(m_gainDsp);
        m_attached = nullptr;
    }

    auto release = [](FMOD::DSP*& dsp) {
        if (!dsp) return;
        dsp->release();
        dsp = nullptr;
    };
    release(m_lowpassDsp);
    release(m_highpassDsp);
    release(m_reverbDsp);
    release(m_gainDsp);
}

void SubmergeEffect::applyWetness(float wet) {
    if (!m_attached) return;

    auto const& sub = config().submerge;

    float const cutoff = glideHz(kDryCutoffHz, sub.cutoffHz, wet);
    m_lowpassDsp->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, cutoff);
    m_lowpassDsp->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 1.f);
    m_lowpassDsp->setBypass(cutoff >= kDryCutoffHz - 10.f);

    float const highpass = glideHz(20.f, std::max(sub.highpassHz, 20.f), wet);
    m_highpassDsp->setParameterFloat(FMOD_DSP_HIGHPASS_CUTOFF, highpass);
    m_highpassDsp->setParameterFloat(FMOD_DSP_HIGHPASS_RESONANCE, 1.f);
    m_highpassDsp->setBypass(highpass <= 20.5f);

    float const reverbMix = sub.reverbMix * wet;
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 2200.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DIFFUSION, 80.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DENSITY, 80.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_HIGHCUT, 4000.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, 0.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, wetLevelDb(reverbMix));
    m_reverbDsp->setBypass(reverbMix <= 0.5f);

    float const gainDb = sub.duckDb * wet;
    m_gainDsp->setParameterFloat(FMOD_DSP_FADER_GAIN, gainDb);
    m_gainDsp->setBypass(std::abs(gainDb) < 0.05f);

    m_attached->setPitch(1.f + (sub.pitch - 1.f) * wet);
}

void SubmergeEffect::startTicker() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
    if (!m_ticker) {
        m_ticker = SubmergeTickerNode::create();
        if (!m_ticker) return;
    }
    static_cast<SubmergeTickerNode*>(m_ticker)->start();
}

void SubmergeEffect::stopTicker() {
    // Runs during shutdown too: the scheduler is still alive there, and an
    // orphaned selector is exactly what we are trying to avoid.
    if (!m_ticker) return;
    static_cast<SubmergeTickerNode*>(m_ticker)->stop();
}

void SubmergeEffect::destroyTicker() {
    if (!m_ticker) return;

    // ~CCNode unschedules itself, so it needs the scheduler to still exist. If
    // cocos is already gone we drop the pointer instead of touching dead state.
    auto* director = cocos2d::CCDirector::get();
    if (!director || !director->getScheduler()) {
        m_ticker = nullptr;
        return;
    }

    static_cast<SubmergeTickerNode*>(m_ticker)->stop();
    m_ticker->release();
    m_ticker = nullptr;
}

} // namespace paimon::dynsong
