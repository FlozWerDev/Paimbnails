#pragma once

#include <Geode/binding/FMODAudioEngine.hpp>
#include <fmod.hpp>

// FMODAudioEngine::m_backgroundMusicChannel es el ChannelGroup que contiene
// TODOS los canales de musica, no solo la del menu. Llamar setPaused() sobre el
// grupo pausa tambien la cancion del nivel, y la bandera sobrevive a ->stop(),
// asi que cualquier cancion que arranque despues nace en silencio. Estos
// helpers trabajan sobre el canal concreto donde vive la cancion principal.

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

// Solo true mientras el handle siga apuntando a un canal vivo y pausado. Si la
// cancion se detuvo o fue reemplazada, FMOD responde FMOD_ERR_INVALID_HANDLE.
inline bool isMusicChannelPaused(FMOD::Channel* channel) {
    if (!channel) return false;
    bool paused = false;
    return channel->getPaused(&paused) == FMOD_OK && paused;
}

// Deshace una pausa a nivel de grupo dejada por otro flujo, para que la cancion
// que estamos por arrancar se escuche.
inline void clearMusicGroupPause() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return;
    bool paused = false;
    if (engine->m_backgroundMusicChannel->getPaused(&paused) == FMOD_OK && paused) {
        engine->m_backgroundMusicChannel->setPaused(false);
    }
}

} // namespace paimon::audio
