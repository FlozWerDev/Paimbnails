#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <filesystem>
#include <cstdint>

/**
 * ProfileMusicManager - per-profile custom music.
 * Downloads, caches and plays Newgrounds audio fragments.
 */
class ProfileMusicManager {
public:
    // Profile music config.
    struct ProfileMusicConfig {
        int songID = 0;           // Newgrounds song ID
        int startMs = 0;
        int endMs = 20000;        // max 20s
        float volume = 0.7f;
        bool enabled = true;
        std::string songName;
        std::string artistName;
        std::string updatedAt;    // for cache validation
        bool isCustom = false;    // file-uploaded song

        // When true, use the profile-background video's audio instead of songID/isCustom.
        // Injected from ProfileConfig; not sent over the network.
        bool useVideoAudio = false;
        // Cached .mp4 path; the manager resolves it itself if empty.
        std::string videoAudioPath;
    };

    // Callbacks — Geode v5: CopyableFunction
    using ConfigCallback = geode::CopyableFunction<void(bool success, const ProfileMusicConfig& config)>;
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::string const& localPath)>;
    using WaveformCallback = geode::CopyableFunction<void(bool success, std::vector<float> const& peaks, int durationMs)>;
    using SongInfoCallback = geode::CopyableFunction<void(bool success, std::string const& name, std::string const& artist, int durationMs)>;

    static ProfileMusicManager& get() {
        // Keep the singleton alive to avoid atexit races with detached workers.
        static auto* instance = new ProfileMusicManager();
        return *instance;
    }

    // === CONFIG ===

    /** Fetch a profile's music config from the server. */
    void getProfileMusicConfig(int accountID, ConfigCallback callback);

    /** Upload the profile music config; server downloads and trims the audio. */
    void uploadProfileMusic(int accountID, std::string const& username, const ProfileMusicConfig& config, UploadCallback callback);

    /** Upload a custom song from a local file. Requires Moderator/VIP/whitelist. */
    void uploadCustomProfileMusic(int accountID, std::string const& username, std::string const& filePath, const ProfileMusicConfig& config, UploadCallback callback);

    /** Whether the current user may upload custom music. */
    bool canUploadCustomMusic() const;

    /** Delete the profile music. */
    void deleteProfileMusic(int accountID, std::string const& username, UploadCallback callback);

    // === PLAYBACK ===

    /** Play a user's profile music, downloading the fragment if not cached. */
    void playProfileMusic(int accountID);

    /** Play profile music with a pre-fetched config. */
    void playProfileMusic(int accountID, ProfileMusicConfig const& config);

    /** Pause the current profile music. */
    void pauseProfileMusic();

    /** Resume paused music. */
    void resumeProfileMusic();

    /** Fully stop the profile music. */
    void stopProfileMusic();

    /** Whether music is playing. */
    bool isPlaying() const { return m_isPlaying; }

    /** Whether music is paused. */
    bool isPaused() const { return m_isPaused; }

    /** Whether a fade-out is in progress. */
    bool isFadingOut() const { return m_isFadingOut; }

    /** Whether the cave effect is active or transitioning. */
    bool hasCaveEffect() const { return m_caveEffectActive || m_caveTransitioning; }

    /** Account ID of the currently playing profile. */
    int getCurrentPlayingProfile() const { return m_currentProfileID; }

    /** Current amplitude of the profile music channel (0.0-1.0), for visual effects. */
    float getCurrentAmplitude() const;

    /** Apply a "cave" effect (lowpass + slower pitch) with a smooth DSP transition; used when opening comments. */
    void applyCaveEffect();

    /** Remove the cave effect and restore normal playback with a smooth transition. */
    void removeCaveEffect();

    /** Force-remove the cave effect immediately (no transition). */
    void forceRemoveCaveEffect();

    /** Force an immediate stop of all playback, ignoring any fade-out and clearing state. */
    void forceStop();

    // === WAVEFORM / VISUALIZATION ===

    /** Get waveform peaks for a Newgrounds song (downloads it temporarily to analyze). */
    void getWaveformPeaks(int songID, WaveformCallback callback);

    /** Get Newgrounds song info (name, artist, duration). */
    void getSongInfo(int songID, SongInfoCallback callback);

    /** Get local audio-file info (duration). */
    void getLocalSongInfo(std::string const& filePath, SongInfoCallback callback);

    /** Get waveform peaks for a local file. */
    void getWaveformPeaksForFile(std::string const& filePath, WaveformCallback callback);

    /** Download a Newgrounds song for preview. */
    void downloadSongForPreview(int songID, DownloadCallback callback);

    /** Play a song preview from a specific point. */
    void playPreview(std::string const& filePath, int startMs, int endMs);

    /** Stop the preview. */
    void stopPreview();

    // === CACHE ===

    /** Whether a profile's music fragment is cached. */
    bool isCached(int accountID);

    /** RAM-cached config for an accountID, or nullptr; enables optimistic playback. */
    const ProfileMusicConfig* getCachedConfig(int accountID) const;

    bool tryGetImmediateConfig(int accountID, ProfileMusicConfig& outConfig);

    // Inject bundle config to avoid a per-account request
    void injectBundleConfig(int accountID, const ProfileMusicConfig& config);

    /** Path to the cached file. */
    std::filesystem::path getCachePath(int accountID);

    /** Clear the profile music cache. */
    void clearCache();

    /** Invalidate cache (RAM + disk) for an accountID, forcing re-download next time. */
    void invalidateCache(int accountID);

    // === SETTINGS ===

    /** Whether profile music is globally enabled. */
    bool isEnabled() const;

    /** Game music volume (applied to profile music). */
    float getGlobalVolume() const;

private:
    enum class PlaybackKind {
        None,
        Profile,
        Preview,
    };

    ProfileMusicManager();
    ~ProfileMusicManager() {
        m_lifetimeToken->store(false, std::memory_order_release);
    }

    ProfileMusicManager(const ProfileMusicManager&) = delete;
    ProfileMusicManager& operator=(const ProfileMusicManager&) = delete;

    // Playback state.
    bool m_isPlaying = false;
    bool m_isPaused = false;
    int m_currentProfileID = 0;
    uint32_t m_profileSessionToken = 0;
    std::string m_currentAudioPath;
    PlaybackKind m_playbackKind = PlaybackKind::None;

    // Pending params for load after dip fade.
    int m_pendingStartMs = 0;
    int m_pendingEndMs = 0;
    bool m_pendingLoop = true;

    // Dip fade (main channel only).
    static constexpr int FADE_STEPS = 20;
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    uint32_t m_fadeGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_lifetimeToken = std::make_shared<std::atomic<bool>>(true);
    float m_bgVolumeBeforeFade = 1.0f;
    unsigned int m_savedBgPosMs = 0;

    bool isCrossfadeEnabled() const;
    float getFadeDurationMs() const;

    void fadeInProfileMusic(float targetVolume);
    void fadeOutAndStop();
    void executeDipFadeOut(int step, int totalSteps, float volFrom, float volTo, bool restoreAfter, uint32_t generation);
    void executeDipFadeIn(int step, int totalSteps, float volFrom, float volTo, uint32_t generation);

    // Config cache.
    std::map<int, ProfileMusicConfig> m_configCache;
    static constexpr size_t MAX_CONFIG_CACHE_SIZE = 256;

    // Cache directory path.
    std::filesystem::path getCacheDir();

    // Path of the .meta file for a cached audio.
    std::filesystem::path getMetaPath(int accountID);

    // Save config metadata next to the cached file to detect changes.
    void saveMetaFile(int accountID, ProfileMusicConfig const& config);

    // Check whether the cached file matches the current server config.
    bool isCacheValid(int accountID, ProfileMusicConfig const& config);

    // Download the audio fragment from the server.
    void downloadMusicFragment(int accountID, DownloadCallback callback);

    // Analyze a file's waveform and return duration.
    std::vector<float> analyzeWaveform(std::string const& filePath, int numPeaks, int& outDurationMs);

    // Extract an audio fragment as WAV.
    std::vector<uint8_t> extractAudioFragment(std::string const& filePath, int startMs, int endMs);

    // Helpers
    void loadProfileOnMainChannel(const std::string& path, bool loop, int startMs, int endMs, float volume);
    void playAudioFile(std::string const& path, bool loop, int startMs = 0, int endMs = 0);
    void playProfileMusicWithConfig(int accountID, ProfileMusicConfig const& config);
    void stopOwnedAudioPlayback();
    void stopCurrentAudio(bool restoreContext = true);

    // Cave effect (lowpass + pitch).
    FMOD::DSP* m_lowpassDSP = nullptr;
    bool m_caveEffectActive = false;
    bool m_caveTransitioning = false;
    uint32_t m_caveGeneration = 0;
    float m_originalFrequency = 0.0f;
    float m_originalVolume = 0.0f;
    void executeCaveTransitionStep(int step, int totalSteps, float cutoffFrom, float cutoffTo,
                                    float freqFrom, float freqTo, float volFrom, float volTo, bool applying,
                                    uint32_t generation);
};


