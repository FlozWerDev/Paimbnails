#pragma once

#include <Geode/binding/FMODAudioEngine.hpp>
#include <fmod.hpp>

// m_backgroundMusicChannel groups all music; group pause also silences the
// level and survives stop(). Helpers below use the main song channel.

namespace paimon::audio {

inline FMOD::Channel* mainMusicChannel(FMODAudioEngine* engine) {
    if (!engine) return nullptr;
    if (auto* channel = engine->getActiveMusicChannel(0)) {
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

// true only while the handle points at a live paused channel; stopped or
// replaced songs answer FMOD_ERR_INVALID_HANDLE.
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
