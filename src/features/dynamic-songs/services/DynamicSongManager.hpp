#pragma once
#include <Geode/Geode.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

// Layers where dynamic songs may play.
enum class DynSongLayer {
    None,           // should not play
    LevelSelect,    // official level selector
    LevelInfo,      // level info (online/custom)
};

// State machine states.
enum class DynState {
    Idle,       // no dynamic song active, menu (or nothing) playing
    FadingIn,   // song loaded, volume ramping 0 -> target
    Playing,    // song at full volume, stable state
    FadingOut,  // volume lowering; m_postFadeAction decides what happens at 0
    Suspended,  // paused for external audio (profile music)
};

class DynSongFadeNode; // definido en .cpp

class DynamicSongManager {
public:
    ~DynamicSongManager();
    static DynamicSongManager* get();

    bool isStreamingPreviewPending() const { return m_streamingPreviewPending || m_previewAwaitingSongInfo; }
    bool isActive() const { return m_state != DynState::Idle || isStreamingPreviewPending() || m_awaitingDownloadOnly; }
    bool isFading() const { return m_state == DynState::FadingIn || m_state == DynState::FadingOut; }
    bool isInValidLayer() const { return m_currentLayer != DynSongLayer::None; }
    DynSongLayer getCurrentLayer() const { return m_currentLayer; }
    DynState getState() const { return m_state; }
    int getCurrentPlayingLevelID() const { return m_currentPlayingLevelID; }
    bool hasSuspendedPlayback() const { return m_state == DynState::Suspended; }

    void enterLayer(DynSongLayer layer);
    void exitLayer(DynSongLayer layer);

    void playSong(GJGameLevel* level);
    void stopSong();
    void fadeOutForLevelStart();
    void forceKill();

    void suspendPlaybackForExternalAudio();
    void resumeSuspendedPlayback();

    bool isStreamingPreview() const { return m_streamingPreview; }
    void stopStreamingPreview();
    void checkPreviewSwap();

    float getDynamicVolume() const;
    void setDynamicVolume(float vol);

    bool verifyPlayback();
    void onPlaybackHijacked();

    static inline bool s_selfPlayMusic = false;

    // Callback from the fade node (public because DynSongFadeNode calls it)
    void onFadeComplete();

private:
    DynState m_state = DynState::Idle;
    DynSongLayer m_currentLayer = DynSongLayer::None;

    std::string m_activeSongPath;
    int m_currentPlayingLevelID = 0;
    unsigned int m_savedMenuPos = 0;
    unsigned int m_savedDynamicPosMs = 0;

    DynSongFadeNode* m_fadeNode = nullptr;
    enum class PostFadeAction { None, PlayPending, RestoreMenu, Cleanup };
    PostFadeAction m_postFadeAction = PostFadeAction::None;
    std::string m_pendingSongPath;

    // Timing guards
    std::chrono::steady_clock::time_point m_lastFadeCompleteTime{};
    std::chrono::steady_clock::time_point m_lastPlaySongTime{};

    // Song rotation per level
    std::unordered_map<int, std::vector<std::string>> m_songRotationCache;
    static constexpr size_t MAX_ROTATION_CACHE_LEVELS = 256;

    float getFadeDurationSec() const;
    void playOnMainChannel(const std::string& songPath, float startVolume);
    void loadMenuTrack(float startVolume);
    void applyRandomSeek(FMOD::Channel* existingCh = nullptr);

    void fadeVolume(float from, float to, float durationSec, PostFadeAction action);
    void cancelFade();

    std::vector<std::string> getAllSongPaths(GJGameLevel* level);
    std::string getNextRotationSong(GJGameLevel* level);

    // Streaming preview
    bool m_previewAwaitingSongInfo = false;
    bool m_streamingPreviewPending = false;
    bool m_streamingPreview = false;
    // Set when the streaming preview is disabled but a song is still
    // downloading: we keep polling so it auto-plays once it's local.
    bool m_awaitingDownloadOnly = false;
    FMOD::Sound* m_previewStreamSound = nullptr;
    FMOD::Channel* m_previewChannel = nullptr;
    int m_previewSongID = 0;
    cocos2d::CCNode* m_streamPollNode = nullptr;
    std::chrono::steady_clock::time_point m_previewRequestStartTime{};
    std::chrono::steady_clock::time_point m_previewPlayAttemptSince{};

    void startStreamingPreview(GJGameLevel* level);
};