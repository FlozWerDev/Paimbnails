#pragma once

#include <Geode/binding/FMODAudioEngine.hpp>
#include <fmod.hpp>

#include <atomic>

// m_backgroundMusicChannel groups all music: pausing the group also silences
// the level and survives stop(). The helpers below use the main song channel.

namespace paimon::audio {

// channelForChannelID() is a throwing stub (0x9999999) in the 2.209/2.2091
// bindings, and getActiveMusicChannel() calls it — the game would crash.
// the channel map can't be rebuilt here: the game hashes its keys with
// fNV-1a, not std::hash<int>, so an STL lookup returns the wrong channel.
// so we call the native function and probe it once: if the stub throws,
// remember it and return nullptr from then on.
inline FMOD::Channel* activeMusicChannel(FMODAudioEngine* engine, int musicID) {
    if (!engine) return nullptr;

    // 0 = not probed, 1 = binding works, 2 = throwing stub
    static std::atomic<int> lookupState{0};
    int state = lookupState.load(std::memory_order_acquire);
    if (state == 0) {
        try {
            // any id works: we only care whether the call throws.
            auto* probe = engine->channelForChannelID(0);
            (void)probe;
            state = 1;
        } catch (...) {
            state = 2;
        }
        lookupState.store(state, std::memory_order_release);
    }
    if (state != 1) return nullptr;

    return engine->getActiveMusicChannel(musicID);
}

inline FMOD::Channel* mainMusicChannel(FMODAudioEngine* engine) {
    if (!engine) return nullptr;
    if (auto* channel = activeMusicChannel(engine, 0)) {
        return channel;
    }
    auto* group = engine->m_backgroundMusicChannel;
    if (!group) return nullptr;
    int numCh = 0;
    group->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (group->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
}

inline FMOD::Channel* mainMusicChannel() {
    return mainMusicChannel(FMODAudioEngine::sharedEngine());
}

inline void setMusicChannelPaused(FMOD::Channel* channel, bool paused) {
    if (channel) channel->setPaused(paused);
}

// true only while the handle is a live paused channel; stopped or replaced
// songs answer fmod_err_invalid_handle.
inline bool isMusicChannelPaused(FMOD::Channel* channel) {
    if (!channel) return false;
    bool paused = false;
    return channel->getPaused(&paused) == FMOD_OK && paused;
}

// clear a group-level pause left by another flow so the next song is audible.
inline void clearMusicGroupPause() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return;
    bool paused = false;
    if (engine->m_backgroundMusicChannel->getPaused(&paused) == FMOD_OK && paused) {
        engine->m_backgroundMusicChannel->setPaused(false);
    }
}

} // namespace paimon::audio
