#pragma once

// Editor playback on its own FMOD ChannelGroup, isolated from both the level
// song and the main-menu loop, with live metering, smooth fades, and FX.

#include <fmod.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class GJGameLevel;

namespace paimon::editormusic {

enum class RepeatMode : std::uint8_t { Off = 0, All = 1, One = 2 };

enum class EditorQueueSource : std::uint8_t {
    All = 0,
    Favorites,
    Playlist,
    GeometryDash,
    Local,
    Count,
};

enum class PlaytestBehavior : std::uint8_t {
    PauseAndResume = 0,
    DuckVolume,
    KeepPlaying,
    Count,
};

enum class EditorFxPreset : std::uint8_t {
    Original = 0,
    SlowReverb,
    Nightcore,
    BassBoost,
    Lofi,
    Underwater,
    ConcertHall,
    Custom,
    Count,
};

struct EditorFxConfig {
    bool enabled = false;
    EditorFxPreset preset = EditorFxPreset::Original;
    float speed = 1.f;       // 0.50 .. 1.50
    float bassDb = 0.f;      // -12 .. +10
    float trebleDb = 0.f;    // -12 .. +10
    float lowpassHz = 22000.f; // 500 .. 22000
    float reverbMix = 0.f;   // 0 .. 100
    float echoMix = 0.f;     // 0 .. 100
};

char const* fxPresetName(EditorFxPreset preset);
EditorFxConfig fxPresetConfig(EditorFxPreset preset);
char const* queueSourceName(EditorQueueSource source);
char const* playtestBehaviorName(PlaytestBehavior behavior);

class EditorMusicPlayer {
public:
    static EditorMusicPlayer& get();

    // Queue & library source
    void refreshQueue();
    bool ensureQueue();
    std::vector<std::string> const& queue() const { return m_queue; }
    std::vector<std::string> upcomingTrackIds(std::size_t maxCount = 12) const;

    EditorQueueSource source() const { return m_source; }
    std::string const& playlistId() const { return m_playlistId; }
    void setSource(EditorQueueSource source, std::string const& playlistId = {});
    void enqueueNext(std::string const& trackId);

    // Transport
    bool play(std::string const& trackId);
    bool playNext();
    bool playPrevious();
    void togglePause();
    void setPaused(bool paused);
    void stop();
    void seekMs(int ms);
    bool playLevelSong(GJGameLevel* level, std::string* outStatus = nullptr);

    // Volume & mute
    float volume() const { return m_volume; }
    void setVolume(float value, bool persist = true);
    bool isMuted() const { return m_muted; }
    void toggleMute();

    // Modes
    bool shuffle() const { return m_shuffle; }
    void toggleShuffle();
    RepeatMode repeat() const { return m_repeat; }
    void cycleRepeat();

    // Track state
    bool isPlaying() const;
    bool isPaused() const { return m_paused; }
    bool hasActiveTrack() const { return !m_trackId.empty(); }
    std::string const& trackId() const { return m_trackId; }
    std::string trackName() const;
    std::string trackArtist() const;
    std::string trackCoverPath() const;
    int trackSongId() const;
    int positionMs() const;
    int lengthMs() const;
    float outputPeak() const;

    // Playtest lifecycle
    void suspend();
    void resumeFromSuspend();
    bool isSuspended() const { return m_suspended; }

    // Audio effects
    EditorFxConfig const& fxConfig() const { return m_fx; }
    void setFxConfig(EditorFxConfig cfg);
    void applyFxPreset(EditorFxPreset preset);
    bool isFxBypassed() const { return m_fxBypassed; }
    void setFxBypassed(bool bypassed);

    // Preferences
    PlaytestBehavior playtestBehavior() const { return m_playtestBehavior; }
    void setPlaytestBehavior(PlaytestBehavior behavior);

    bool autoPlayOnEnter() const { return m_autoPlayOnEnter; }
    void setAutoPlayOnEnter(bool enabled);

    bool showToastOnChange() const { return m_showToastOnChange; }
    void setShowToastOnChange(bool enabled);

    bool animationsEnabled() const { return m_animationsEnabled; }
    void setAnimationsEnabled(bool enabled);

    int panelSkin() const { return m_panelSkin; }
    void setPanelSkin(int skin);

    // Driven by EditorMusicPanel::tick(dt)
    void tick(float dt = 0.05f);

    // Cleanly stops audio and releases FMOD DSP/Group/Sound handles.
    void shutdown();

    using StateListener = std::function<void()>;
    std::size_t addListener(StateListener cb);
    void removeListener(std::size_t token);

private:
    EditorMusicPlayer();
    ~EditorMusicPlayer() = default;
    EditorMusicPlayer(EditorMusicPlayer const&) = delete;
    EditorMusicPlayer& operator=(EditorMusicPlayer const&) = delete;

    bool openFile(std::string const& path);
    void releaseSound();
    bool ensureChannelGroup();
    bool ensureDsps();
    void applyFxToGroup();
    void detachDsps();
    void applyLoopMode();
    void applyVolume();
    bool channelAlive() const;
    std::size_t nextIndex() const;
    void notifyChanged();
    void ensureLibrarySyncStarted();

    FMOD::ChannelGroup* m_group = nullptr;
    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;

    FMOD::DSP* m_eqDsp = nullptr;
    FMOD::DSP* m_lowpassDsp = nullptr;
    FMOD::DSP* m_reverbDsp = nullptr;
    FMOD::DSP* m_echoDsp = nullptr;
    FMOD::DSP* m_meterDsp = nullptr;

    std::vector<std::string> m_queue;
    std::deque<std::string> m_history;
    std::string m_queuedNextId;
    std::string m_trackId;
    std::size_t m_index = 0;

    EditorQueueSource m_source = EditorQueueSource::All;
    std::string m_playlistId;

    float m_volume = 0.6f;
    bool m_muted = false;
    float m_fadeLevel = 1.f;
    float m_fadeTarget = 1.f;
    bool m_pendingPauseAfterFade = false;

    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatMode::All;
    PlaytestBehavior m_playtestBehavior = PlaytestBehavior::PauseAndResume;
    bool m_autoPlayOnEnter = false;
    bool m_showToastOnChange = true;
    bool m_animationsEnabled = true;
    int m_panelSkin = 0;

    EditorFxConfig m_fx;
    bool m_fxBypassed = false;

    bool m_paused = false;
    bool m_suspended = false;
    int m_suspendedPosMs = 0;
    bool m_shuttingDown = false;
    bool m_syncRequested = false;

    std::size_t m_libListenerToken = 0;
    std::vector<std::pair<std::size_t, StateListener>> m_listeners;
    std::size_t m_nextListenerToken = 1;
};

} // namespace paimon::editormusic
