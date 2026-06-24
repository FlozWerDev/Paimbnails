#pragma once

// Plays MenuMusicLibrary tracks inside the editor on its own FMOD channel
// group, independent of GD's main channel. Keeps its own mode/playlist state.

#include "../../menu-music/model/MenuMusicTypes.hpp"
#include <deque>
#include <string>
#include <vector>

namespace FMOD {
    class ChannelGroup;
    class Sound;
    class Channel;
}

namespace paimon::editormusic {

class EditorMusicPlayer {
public:
    static EditorMusicPlayer& get();

    bool playNext();
    bool playPrevious();
    void togglePause();          // starts playNext() if nothing is playing
    void stop();                 // stops and frees the channel/sound

    // Auto-duck our channel while GD's main channel (playtest/preview) plays.
    void setMainChannelActive(bool active);

    void cycleMode();            // Library <-> Playlist
    void cyclePlaylist();        // next playlist (switches to Playlist mode)

    bool isPaused() const { return m_paused; }
    bool isPlaying() const;
    std::string currentDisplayName() const;
    std::string modeLabel() const;
    paimon::menumusic::PlaybackMode mode() const { return m_mode; }

private:
    EditorMusicPlayer() = default;
    ~EditorMusicPlayer() = default;
    EditorMusicPlayer(const EditorMusicPlayer&) = delete;
    EditorMusicPlayer& operator=(const EditorMusicPlayer&) = delete;

    bool ensureGroup();
    std::vector<std::string> candidateTrackIds() const;
    bool playTrackById(const std::string& trackId);

    FMOD::ChannelGroup* m_group = nullptr;
    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;

    paimon::menumusic::PlaybackMode m_mode = paimon::menumusic::PlaybackMode::Library;
    std::string m_activePlaylistId;
    std::string m_currentTrackId;
    std::string m_currentDisplayName;
    bool m_paused = false;       // explicit user pause
    bool m_mainActive = false;   // editor's main channel is playing

    std::deque<std::string> m_history;
    static constexpr std::size_t kMaxHistory = 32;
};

} // namespace paimon::editormusic
