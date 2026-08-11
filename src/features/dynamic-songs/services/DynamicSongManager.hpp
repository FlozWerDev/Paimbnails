#pragma once
#include <Geode/Geode.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

enum class DynSongLayer {
    None,        // No dynamic song.
    LevelSelect, // Official level selector.
    LevelInfo,   // Level info (online/custom).
};

enum class DynState {
    Idle,      // No dynamic song active.
    FadingIn,  // Volume ramps to target.
    Playing,   // Stable full-volume state.
    FadingOut, // Volume falls toward the post-fade action.
    Suspended, // Paused for external audio.
    Handoff,   // Muffled while gameplay takes over.
};

class DynSongFadeNode;

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
    bool isHandingOff() const { return m_state == DynState::Handoff; }

    void enterLayer(DynSongLayer layer);
    void exitLayer(DynSongLayer layer);

    void playSong(GJGameLevel* level);
    void stopSong();
    void fadeOutForLevelStart();
    void forceKill();

    // Muffle while the game decides whether the level can start.
    void submergeForLevelStart();
    // Return the channel to gameplay; levelID enables resurfacing on revisit.
    void finishGameplayHandoff(int levelID = 0);
    // Restore the song when the level never started.
    void cancelGameplayHandoff();

    void suspendPlaybackForExternalAudio();
    void resumeSuspendedPlayback();

    bool isStreamingPreview() const { return m_streamingPreview; }
    void stopStreamingPreview();
    void checkPreviewSwap();
    void handoffWatchTick(float dt);

    float getDynamicVolume() const;
    void setDynamicVolume(float vol);

    bool verifyPlayback();
    void onPlaybackHijacked();

    static inline bool s_selfPlayMusic = false;

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

    std::chrono::steady_clock::time_point m_lastFadeCompleteTime{};
    std::chrono::steady_clock::time_point m_lastPlaySongTime{};

    std::unordered_map<int, std::vector<std::string>> m_songRotationCache;
    static constexpr size_t MAX_ROTATION_CACHE_LEVELS = 256;

    std::unordered_map<int, unsigned int> m_resumePositions;
    static constexpr size_t MAX_RESUME_LEVELS = 256;

    DynSongLayer m_handoffLayer = DynSongLayer::None;
    int m_handoffLevelID = 0;
    float m_handoffClock = 0.f;
    cocos2d::CCNode* m_handoffWatchNode = nullptr;
    // Deferred resurfacing request; menu detours must not consume it.
    int m_surfaceLevelID = 0;
    std::chrono::steady_clock::time_point m_surfaceRequestTime{};

    float getFadeDurationSec() const;
    float dynamicTargetVolume() const;
    FMOD::ChannelControl* currentChannelControl() const;
    void playOnMainChannel(const std::string& songPath, float startVolume);
    void loadMenuTrack(float startVolume);
    void applyStartPosition(int levelID, FMOD::Channel* existingCh = nullptr);
    void applyRandomSeek(FMOD::Channel* existingCh = nullptr);
    void rememberPosition();

    void fadeVolume(float from, float to, float durationSec, PostFadeAction action);
    void cancelFade();

    // Shared teardown for forceKill and gameplay handoff.
    void resetToIdle(bool stopOwnSound);
    bool isOurSoundPlaying() const;
    void startHandoffWatch();
    void stopHandoffWatch();

    std::vector<std::string> getAllSongPaths(GJGameLevel* level);
    std::string getNextRotationSong(GJGameLevel* level);

    bool m_previewAwaitingSongInfo = false;
    bool m_streamingPreviewPending = false;
    bool m_streamingPreview = false;
    // Keep polling a disabled preview until its download finishes.
    bool m_awaitingDownloadOnly = false;
    FMOD::Sound* m_previewStreamSound = nullptr;
    FMOD::Channel* m_previewChannel = nullptr;
    int m_previewSongID = 0;
    cocos2d::CCNode* m_streamPollNode = nullptr;
    std::chrono::steady_clock::time_point m_previewRequestStartTime{};
    std::chrono::steady_clock::time_point m_previewPlayAttemptSince{};

    void startStreamingPreview(GJGameLevel* level);
};
