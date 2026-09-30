#pragma once

#include "AudioExtractor.hpp"
#include <memory>
#include <string>

namespace FMOD {
class Sound;
class Channel;
class ChannelGroup;
}

namespace paimon::video {

// own fmod channel per video; gd music keeps playing alongside video sound.
class VideoAudioTrack {
public:
    // null when the file has no audio; safe off the main thread.
    static std::unique_ptr<VideoAudioTrack> create(std::string const& videoPath);

    ~VideoAudioTrack();

    VideoAudioTrack(VideoAudioTrack const&) = delete;
    VideoAudioTrack& operator=(VideoAudioTrack const&) = delete;

    void play(double fromSeconds);
    void pause();
    void stop();
    void seek(double seconds);

    void setLoop(bool loop);
    void setVolume(float volume);
    float getVolume() const { return m_volume; }

    bool isPlaying() const;
    // seconds, or negative when not playing.
    double positionSeconds() const;
    double durationSeconds() const { return m_duration; }

    static void syncAllVolumes();

private:
    VideoAudioTrack() = default;
    bool init(AudioPcm&& pcm);
    void releaseChannel();

    FMOD::Sound*   m_sound   = nullptr;
    FMOD::Channel* m_channel = nullptr;
    double m_duration = 0.0;
    float  m_volume   = 1.0f;
    bool   m_loop     = false;
};

} // namespace paimon::video
