#pragma once

// Editor playback on its own FMOD channel, so it never fights the level song
// or the menu loop for the game channel.

#include <fmod.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::editormusic {

enum class RepeatMode : std::uint8_t { Off, All, One };

class EditorMusicPlayer {
public:
    static EditorMusicPlayer& get();

    // Rebuilds the queue from the library, keeping the current track selected.
    void refreshQueue();
    // Refresh once when empty; false when still empty.
    bool ensureQueue();
    std::vector<std::string> const& queue() const { return m_queue; }

    bool play(std::string const& trackId);
    bool playNext();
    bool playPrevious();
    void togglePause();
    void stop();

    void seekMs(int ms);

    float volume() const { return m_volume; }
    void setVolume(float value);

    bool shuffle() const { return m_shuffle; }
    void toggleShuffle();
    RepeatMode repeat() const { return m_repeat; }
    void cycleRepeat();

    bool isPlaying() const;
    std::string const& trackId() const { return m_trackId; }
    std::string trackName() const;

    int positionMs() const;
    int lengthMs() const;

    // A playtest silences the panel; ending it drops the song back where it was.
    void suspend();
    void resumeFromSuspend();
    bool isSuspended() const { return m_suspended; }

    // Advances the queue when a track ends. Driven by the panel tick.
    void tick();

private:
    EditorMusicPlayer();
    EditorMusicPlayer(const EditorMusicPlayer&) = delete;
    EditorMusicPlayer& operator=(const EditorMusicPlayer&) = delete;

    bool openFile(std::string const& path);
    void releaseSound();
    void applyLoopMode();
    void applyVolume();
    bool channelAlive() const;
    std::size_t nextIndex() const;

    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;

    std::vector<std::string> m_queue;
    std::string m_trackId;
    std::size_t m_index = 0;

    float m_volume = 0.6f;
    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatMode::All;

    bool m_paused = false;
    bool m_suspended = false;
    // Position kept while suspended: the channel dies with the sound if the
    // playtest outlives it, so resuming needs somewhere to seek back to.
    int m_suspendedPosMs = 0;
};

} // namespace paimon::editormusic
