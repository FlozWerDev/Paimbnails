#pragma once

// MenuMusicPlayer — high-level API for controlling which song plays in the main menu.
// Bridges the library with the existing MenuLoopManager (which hooks GameManager::playMenuMusic).
// Delegates the FMOD override to MenuLoopManager since its hook already has the right priority;
// MenuMusicPlayer only tells it "the override should now be this path".
// Also keeps a history for prev/next and publishes track-change events for the popup.

#include "../model/MenuMusicTypes.hpp"
#include <Geode/Geode.hpp>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace paimon::menumusic {

class MenuMusicPlayer {
public:
    static MenuMusicPlayer& get();

    // Operaciones principales
    //
    // Decide que track suena a continuacion segun el modo activo y lo
    // empuja al override del menu loop. Devuelve true si se pudo elegir
    // un track (false si la libreria esta vacia o el modo es Disabled).
    bool playNext();

    // Retrocede al track anterior del historial (si lo hay). Devuelve
    // true si retrocedio.
    bool playPrevious();

    // Fuerza un track concreto (lo usa la UI al pinchar una cancion).
    // Si trackId no existe en la libreria, no hace nada.
    bool playSpecific(const std::string& trackId);

    // Pausa / reanuda el canal actual sin cambiar de track.
    void pause();
    void resume();
    void toggleVanillaFallback(); // apaga override y vuelve al menuLoop.mp3
    bool isPaused() const { return m_paused; }

    // Activa el modo y, si hay tracks, reproduce uno nuevo. Usa esto
    // desde la UI en los toggles de modo.
    void setMode(PlaybackMode mode, bool playNow = true);

    // Estado observable
    const PlaybackState& state() const { return m_state; }
    const MusicTrack* currentTrack() const;

    // Suscripcion a cambios
    //
    // Se dispara cada vez que el track actual cambia. Util para que el
    // popup abierto actualice el disco+blur.
    using TrackChangedListener = std::function<void(const std::string& trackId)>;
    std::size_t addListener(TrackChangedListener cb);
    void removeListener(std::size_t token);

    // Helpers expuestos para UI
    // Devuelve una vista de los ids disponibles segun el modo actual.
    std::vector<std::string> candidateTrackIds() const;

private:
    MenuMusicPlayer() = default;
    ~MenuMusicPlayer() = default;
    MenuMusicPlayer(const MenuMusicPlayer&) = delete;
    MenuMusicPlayer& operator=(const MenuMusicPlayer&) = delete;

    std::string pickRandomExcept(const std::string& exceptId) const;
    void applyOverrideAndPlay(const std::string& trackId, const std::string& audioPath);
    void notifyChanged();

    PlaybackState m_state;
    bool m_paused = false;

    // Historial acotado (ids) para soportar prev.
    std::deque<std::string> m_history;
    static constexpr std::size_t kMaxHistory = 32;

    std::vector<std::pair<std::size_t, TrackChangedListener>> m_listeners;
    std::size_t m_nextListenerToken = 1;
};

} // namespace paimon::menumusic
