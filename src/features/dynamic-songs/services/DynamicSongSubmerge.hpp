#pragma once

// The "dive" filter for dynamic songs: a lowpass + highpass + reverb + gain
// chain that slides between dry and fully submerged, so pressing play muffles
// the music instead of cutting it, and coming back from a level lets it
// surface again.
//
// It attaches to whatever ChannelControl the song is actually playing on: the
// shared background music group for a local file, or the raw channel a
// streaming preview owns. Everything is driven from a wetness in [0, 1]; the
// chain detaches itself once it is dry again so the menu music is never left
// running through our DSPs.

#include <fmod.hpp>

namespace cocos2d { class CCNode; }

namespace paimon::dynsong {

class SubmergeEffect {
public:
    static SubmergeEffect& get();

    // Re-points the chain. Passing a different target (or nullptr) drops the
    // DSPs from the previous one first.
    void bindTarget(FMOD::ChannelControl* target);

    void rampTo(float wet, float seconds);
    void snapTo(float wet);

    // Straight back to dry and detached, no ramp.
    void release();
    void shutdown();

    float wetness() const { return m_wet; }
    bool isRamping() const { return m_ramping; }
    bool isEngaged() const { return m_ramping || m_wet > 0.0001f; }

    // Called by the internal ticker; public so the node can reach it.
    void tick(float dt);

private:
    SubmergeEffect() = default;
    SubmergeEffect(SubmergeEffect const&) = delete;
    SubmergeEffect& operator=(SubmergeEffect const&) = delete;

    bool ensureDsps();
    void detachDsps();
    void applyWetness(float wet);
    void startTicker();
    void stopTicker();
    void destroyTicker();

    FMOD::ChannelControl* m_target = nullptr;
    FMOD::ChannelControl* m_attached = nullptr;

    FMOD::DSP* m_lowpassDsp = nullptr;
    FMOD::DSP* m_highpassDsp = nullptr;
    FMOD::DSP* m_reverbDsp = nullptr;
    FMOD::DSP* m_gainDsp = nullptr;

    cocos2d::CCNode* m_ticker = nullptr;

    float m_wet = 0.f;
    float m_from = 0.f;
    float m_to = 0.f;
    float m_elapsed = 0.f;
    float m_duration = 0.f;
    bool m_ramping = false;
    bool m_shuttingDown = false;
};

} // namespace paimon::dynsong
