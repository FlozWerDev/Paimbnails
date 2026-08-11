#pragma once

#include "../model/MenuMusicTypes.hpp"
#include <Geode/Geode.hpp>
#include <fmod.hpp>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace paimon::menumusic {

class MenuMusicPlayer {
public:
    static MenuMusicPlayer& get();

    bool playNext();

    bool playPrevious();

    bool playSpecific(const std::string& trackId);

    bool swapHeldTrack();
    const std::string& heldTrackId() const { return m_heldTrackId; }
    bool isManagingPlayback() const;

    void pause();
    void resume();
    void toggleVanillaFallback();
    bool isPaused() const;

    void setMode(PlaybackMode mode, bool playNow = true);

    const PlaybackState& state() const { return m_state; }
    const MusicTrack* currentTrack() const;

    // Clears the tracked library track (used when an external, non-library
    // override starts playing so the UI stops showing the stale track).
    void forgetCurrentTrack();

    using TrackChangedListener = std::function<void(const std::string& trackId)>;
    std::size_t addListener(TrackChangedListener cb);
    void removeListener(std::size_t token);

    std::vector<std::string> candidateTrackIds() const;

private:
    MenuMusicPlayer() = default;
    ~MenuMusicPlayer() = default;
    MenuMusicPlayer(const MenuMusicPlayer&) = delete;
    MenuMusicPlayer& operator=(const MenuMusicPlayer&) = delete;

    std::string pickRandomExcept(const std::string& exceptId) const;
    bool applyOverrideAndPlay(const std::string& trackId, const std::string& audioPath);
    void notifyChanged();

    PlaybackState m_state;
    bool m_paused = false;
    // Canal concreto que pausamos, para no tocar el resto de la musica.
    FMOD::Channel* m_pausedChannel = nullptr;

    std::deque<std::string> m_history;
    std::string m_heldTrackId;
    static constexpr std::size_t kMaxHistory = 32;

    std::vector<std::pair<std::size_t, TrackChangedListener>> m_listeners;
    std::size_t m_nextListenerToken = 1;
};

} // namespace paimon::menumusic
