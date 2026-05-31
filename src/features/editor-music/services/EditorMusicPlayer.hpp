#pragma once

// EditorMusicPlayer — reproduce tracks de la libreria (MenuMusicLibrary)
// DENTRO del editor, en un ChannelGroup FMOD propio ("PaimonEditorMusic")
// colgado del master. Es independiente del canal principal
// (m_backgroundMusicChannel) que usa el menu/editor de GD, asi que no
// interfiere con el audio del nivel ni con el menu loop.
//
// Reutiliza MenuMusicLibrary para tracks/playlists. Mantiene su PROPIO
// modo y playlist activa para no pisar el estado del player del menu.

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
    void togglePause();          // si no hay nada sonando, arranca playNext()
    void stop();                 // detiene y libera el canal/sonido

    // Pausa/reanuda automatica segun el canal principal del editor: cuando
    // GD reproduce musica en m_backgroundMusicChannel (playtest, preview)
    // ducking nuestro canal; al callar, reanuda (salvo pausa del usuario).
    void setMainChannelActive(bool active);

    void cycleMode();            // Library <-> Playlist
    void cyclePlaylist();        // siguiente playlist (activa modo Playlist)

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
    bool m_paused = false;       // pausa explicita del usuario
    bool m_mainActive = false;   // el canal principal del editor esta sonando

    std::deque<std::string> m_history;
    static constexpr std::size_t kMaxHistory = 32;
};

} // namespace paimon::editormusic
