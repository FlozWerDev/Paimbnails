#include "VideoAudioTrack.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/loader/Log.hpp>
#include <algorithm>
#include <mutex>
#include <unordered_set>

namespace paimon::video {

namespace {

std::mutex g_tracksMutex;
std::unordered_set<VideoAudioTrack*> g_liveTracks;

FMOD::System* fmodSystem() {
    auto* engine = FMODAudioEngine::sharedEngine();
    return engine ? engine->m_system : nullptr;
}

float musicVolume() {
    auto* engine = FMODAudioEngine::sharedEngine();
    return engine ? std::clamp(engine->m_musicVolume, 0.0f, 1.0f) : 1.0f;
}

// Own group so video sound can be mixed and stopped independently of GD's.
FMOD::ChannelGroup* videoAudioGroup(FMOD::System* system) {
    static FMOD::ChannelGroup* group = nullptr;
    if (!system) return nullptr;
    if (group) {
        bool muted = false;
        if (group->getMute(&muted) == FMOD_OK) return group;
        group = nullptr;
    }

    if (system->createChannelGroup("PaimonVideoAudio", &group) != FMOD_OK || !group) {
        return nullptr;
    }
    FMOD::ChannelGroup* master = nullptr;
    if (system->getMasterChannelGroup(&master) == FMOD_OK && master) {
        master->addGroup(group);
    }
    return group;
}

FMOD_SOUND_FORMAT pcmFormat(int bitsPerSample) {
    switch (bitsPerSample) {
        case 8:  return FMOD_SOUND_FORMAT_PCM8;
        case 16: return FMOD_SOUND_FORMAT_PCM16;
        case 32: return FMOD_SOUND_FORMAT_PCM32;
        default: return FMOD_SOUND_FORMAT_NONE;
    }
}

}

std::unique_ptr<VideoAudioTrack> VideoAudioTrack::create(std::string const& videoPath) {
    auto pcm = extractAudioToPcm(videoPath);
    if (!pcm.valid()) return nullptr;

    auto track = std::unique_ptr<VideoAudioTrack>(new (std::nothrow) VideoAudioTrack());
    if (!track || !track->init(std::move(pcm))) return nullptr;
    return track;
}

bool VideoAudioTrack::init(AudioPcm&& pcm) {
    auto* system = fmodSystem();
    if (!system) return false;

    FMOD_SOUND_FORMAT format = pcmFormat(pcm.bitsPerSample);
    if (format == FMOD_SOUND_FORMAT_NONE) {
        geode::log::warn("[VideoAudio] unsupported sample depth {}", pcm.bitsPerSample);
        return false;
    }

    FMOD_CREATESOUNDEXINFO exinfo{};
    exinfo.cbsize            = sizeof(exinfo);
    exinfo.length            = static_cast<unsigned int>(pcm.data.size());
    exinfo.numchannels       = pcm.channels;
    exinfo.defaultfrequency  = pcm.sampleRate;
    exinfo.format            = format;

    // FMOD_OPENMEMORY copies the buffer, so ours is free to die with this call.
    FMOD_MODE mode = FMOD_OPENMEMORY | FMOD_OPENRAW | FMOD_CREATESAMPLE |
                     FMOD_2D | FMOD_LOOP_OFF;

    FMOD_RESULT res = system->createSound(
        reinterpret_cast<const char*>(pcm.data.data()), mode, &exinfo, &m_sound);
    if (res != FMOD_OK || !m_sound) {
        geode::log::warn("[VideoAudio] createSound failed ({})", static_cast<int>(res));
        m_sound = nullptr;
        return false;
    }

    unsigned int lengthMs = 0;
    if (m_sound->getLength(&lengthMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
        m_duration = lengthMs / 1000.0;
    }

    std::lock_guard lock(g_tracksMutex);
    g_liveTracks.insert(this);
    return true;
}

VideoAudioTrack::~VideoAudioTrack() {
    {
        std::lock_guard lock(g_tracksMutex);
        g_liveTracks.erase(this);
    }
    releaseChannel();
    if (m_sound) {
        m_sound->release();
        m_sound = nullptr;
    }
}

void VideoAudioTrack::releaseChannel() {
    if (!m_channel) return;
    m_channel->stop();
    m_channel = nullptr;
}

void VideoAudioTrack::play(double fromSeconds) {
    if (!m_sound) return;

    if (isPlaying()) {
        m_channel->setPaused(false);
        return;
    }

    auto* system = fmodSystem();
    if (!system) return;

    m_channel = nullptr;
    if (system->playSound(m_sound, videoAudioGroup(system), true, &m_channel) != FMOD_OK
        || !m_channel) {
        m_channel = nullptr;
        return;
    }

    m_channel->setMode(m_loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
    m_channel->setVolume(std::clamp(m_volume, 0.0f, 1.0f) * musicVolume());
    seek(fromSeconds);
    m_channel->setPaused(false);
}

void VideoAudioTrack::pause() {
    if (m_channel) m_channel->setPaused(true);
}

void VideoAudioTrack::stop() {
    releaseChannel();
}

void VideoAudioTrack::seek(double seconds) {
    if (!m_channel) return;
    double clamped = std::max(0.0, seconds);
    if (m_duration > 0.0) clamped = std::min(clamped, m_duration);
    m_channel->setPosition(static_cast<unsigned int>(clamped * 1000.0), FMOD_TIMEUNIT_MS);
}

void VideoAudioTrack::setLoop(bool loop) {
    m_loop = loop;
    if (m_channel) m_channel->setMode(loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
}

void VideoAudioTrack::setVolume(float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_channel) m_channel->setVolume(m_volume * musicVolume());
}

bool VideoAudioTrack::isPlaying() const {
    if (!m_channel) return false;
    bool playing = false;
    if (m_channel->isPlaying(&playing) != FMOD_OK) return false;
    if (!playing) return false;
    bool paused = false;
    if (m_channel->getPaused(&paused) == FMOD_OK && paused) return false;
    return true;
}

double VideoAudioTrack::positionSeconds() const {
    if (!m_channel) return -1.0;
    unsigned int ms = 0;
    if (m_channel->getPosition(&ms, FMOD_TIMEUNIT_MS) != FMOD_OK) return -1.0;
    return ms / 1000.0;
}

void VideoAudioTrack::syncAllVolumes() {
    std::lock_guard lock(g_tracksMutex);
    float music = musicVolume();
    for (auto* track : g_liveTracks) {
        if (track->m_channel) {
            track->m_channel->setVolume(std::clamp(track->m_volume, 0.0f, 1.0f) * music);
        }
    }
}

} // namespace paimon::video
