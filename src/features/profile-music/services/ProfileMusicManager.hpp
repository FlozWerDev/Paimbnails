#pragma once

#include <Geode/Geode.hpp>
#include <fmod.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <filesystem>
#include <cstdint>

class ProfileMusicManager {
public:
    struct ProfileMusicConfig {
        int songID = 0;
        int startMs = 0;
        int endMs = 20000;
        float volume = 0.7f;
        bool enabled = true;
        std::string songName;
        std::string artistName;
        std::string updatedAt;
        bool isCustom = false;

        bool useVideoAudio = false;
        std::string videoAudioPath;
    };

    using ConfigCallback = geode::CopyableFunction<void(bool success, const ProfileMusicConfig& config)>;
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::string const& localPath)>;
    using WaveformCallback = geode::CopyableFunction<void(bool success, std::vector<float> const& peaks, int durationMs)>;
    using SongInfoCallback = geode::CopyableFunction<void(bool success, std::string const& name, std::string const& artist, int durationMs)>;

    static ProfileMusicManager& get() {
        static auto* instance = new ProfileMusicManager();
        return *instance;
    }

    void getProfileMusicConfig(int accountID, ConfigCallback callback);

    void uploadProfileMusic(int accountID, std::string const& username, const ProfileMusicConfig& config, UploadCallback callback);

    void uploadCustomProfileMusic(int accountID, std::string const& username, std::string const& filePath, const ProfileMusicConfig& config, UploadCallback callback);

    bool canUploadCustomMusic() const;

    void deleteProfileMusic(int accountID, std::string const& username, UploadCallback callback);

    void playProfileMusic(int accountID);

    void playProfileMusic(int accountID, ProfileMusicConfig const& config);

    void pauseProfileMusic();

    void resumeProfileMusic();

    void stopProfileMusic();

    bool isPlaying() const { return m_isPlaying; }

    bool isPaused() const { return m_isPaused; }

    bool isFadingOut() const { return m_isFadingOut; }

    bool hasCaveEffect() const { return m_caveEffectActive || m_caveTransitioning; }

    int getCurrentPlayingProfile() const { return m_currentProfileID; }

    float getCurrentAmplitude() const;

    void applyCaveEffect();

    void removeCaveEffect();

    void forceRemoveCaveEffect();

    void forceStop();

    void getWaveformPeaks(int songID, WaveformCallback callback);

    void getSongInfo(int songID, SongInfoCallback callback);

    void getLocalSongInfo(std::string const& filePath, SongInfoCallback callback);

    void getWaveformPeaksForFile(std::string const& filePath, WaveformCallback callback);

    void downloadSongForPreview(int songID, DownloadCallback callback);

    void playPreview(std::string const& filePath, int startMs, int endMs);

    void stopPreview();

    bool isCached(int accountID);

    const ProfileMusicConfig* getCachedConfig(int accountID) const;

    bool tryGetImmediateConfig(int accountID, ProfileMusicConfig& outConfig);

    void injectBundleConfig(int accountID, const ProfileMusicConfig& config);

    std::filesystem::path getCachePath(int accountID);

    void clearCache();

    void invalidateCache(int accountID);

    bool isEnabled() const;

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

    bool m_isPlaying = false;
    bool m_isPaused = false;
    // Canal concreto que pausamos, para no tocar el resto de la musica.
    FMOD::Channel* m_pausedChannel = nullptr;
    int m_currentProfileID = 0;
    uint32_t m_profileSessionToken = 0;
    std::string m_currentAudioPath;
    PlaybackKind m_playbackKind = PlaybackKind::None;

    int m_pendingStartMs = 0;
    int m_pendingEndMs = 0;
    bool m_pendingLoop = true;

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

    std::map<int, ProfileMusicConfig> m_configCache;
    static constexpr size_t MAX_CONFIG_CACHE_SIZE = 256;

    std::filesystem::path getCacheDir();

    std::filesystem::path getMetaPath(int accountID);

    void saveMetaFile(int accountID, ProfileMusicConfig const& config);

    bool isCacheValid(int accountID, ProfileMusicConfig const& config);

    // `version` (config updatedAt) is appended as a cache-buster so a changed clip is fetched fresh, not a stale CDN/edge copy.
    void downloadMusicFragment(int accountID, std::string const& version, DownloadCallback callback);

    std::vector<float> analyzeWaveform(std::string const& filePath, int numPeaks, int& outDurationMs);

    std::vector<uint8_t> extractAudioFragment(std::string const& filePath, int startMs, int endMs);

    void loadProfileOnMainChannel(const std::string& path, bool loop, int startMs, int endMs, float volume);
    void playAudioFile(std::string const& path, bool loop, int startMs = 0, int endMs = 0);
    void playProfileMusicWithConfig(int accountID, ProfileMusicConfig const& config);
    void stopOwnedAudioPlayback();
    void stopCurrentAudio(bool restoreContext = true);

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


