#pragma once

// dive filter: dulls the music without cutting it.

#include <fmod.hpp>

namespace cocos2d { class CCNode; }

namespace paimon::dynsong {

class SubmergeEffect {
public:
    static SubmergeEffect& get();

    // switches channel, dropping the previous one.
    void bindTarget(FMOD::ChannelControl* target);

    void rampTo(float wet, float seconds);
    void snapTo(float wet);

    // straight back to dry and detached.
    void release();
    void shutdown();

    float wetness() const { return m_wet; }
    bool isRamping() const { return m_ramping; }
    bool isEngaged() const { return m_ramping || m_wet > 0.0001f; }

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
