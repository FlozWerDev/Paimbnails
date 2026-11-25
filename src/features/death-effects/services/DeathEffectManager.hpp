#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace paimon::death_effects {

enum class PlaybackOrder {
    Random,
    Sequential,
    ShuffleBag,
};

enum class DeathDspEffect : int {
    None = 0,
    Reverb,
    Echo,
    Distortion,
    LowPass,
    HighPass,
    Chorus,
    Flange,
};

struct SoundCredit {
    std::string title;
    std::string creator;
    std::string creatorUrl;
    std::string license;
    std::string licenseUrl;
    std::string sourceUrl;
    std::string attribution;
};

struct SoundInfo {
    std::filesystem::path path;
    std::string name;
    unsigned int durationMs = 0;
    bool selected = false;
    bool isClutch = false;
    bool playable = false;
    bool hasCredit = false;
    SoundCredit credit;
};

struct FolderImportResult {
    std::vector<std::filesystem::path> imported;
    std::size_t skipped = 0;
};

class DeathEffectManager {
public:
    static DeathEffectManager& get();

    std::filesystem::path libraryDir() const;
    std::vector<SoundInfo> scanLibrary();

    geode::Result<std::filesystem::path> importSound(std::filesystem::path const& source);
    FolderImportResult importFolder(std::filesystem::path const& folder);
    void importFolderAsync(std::filesystem::path const& folder,
                           std::function<void(FolderImportResult)> done);
    geode::Result<std::filesystem::path> importDownloaded(
        std::string const& displayName,
        std::string const& extension,
        std::vector<std::uint8_t> const& bytes,
        SoundCredit const& credit
    );
    bool removeSound(std::filesystem::path const& path);

    std::optional<SoundCredit> creditFor(std::filesystem::path const& path);
    std::vector<std::pair<std::string, SoundCredit>> allCredits();

    bool isSelected(std::filesystem::path const& path);
    void setSoundSelected(std::filesystem::path const& path, bool selected);
    void addSelected(std::vector<std::filesystem::path> const& paths);
    void useOriginal();

    // High-% / Clutch special sound
    bool isClutchSound(std::filesystem::path const& path);
    void setClutchSound(std::filesystem::path const& path); // passes empty to clear
    std::string clutchSoundName() const;
    bool clutchEnabled() const;
    void setClutchEnabled(bool enabled);
    float clutchThreshold() const; // 50..99
    void setClutchThreshold(float percent);
    bool clutchOnlyNewBest() const;
    void setClutchOnlyNewBest(bool enabled);

    std::size_t selectedCount();
    std::size_t readyCount();

    PlaybackOrder order() const;
    void setOrder(PlaybackOrder order);
    bool avoidRepeats() const;
    void setAvoidRepeats(bool enabled);
    bool stopOnReset() const;
    void setStopOnReset(bool enabled);
    bool allowOverlap() const;
    void setAllowOverlap(bool enabled);
    bool layerWithVanilla() const;
    void setLayerWithVanilla(bool enabled);

    float volume() const;
    void setVolume(float value);
    float pitchVariation() const;
    void setPitchVariation(float value);
    float speed() const;
    void setSpeed(float value);
    float pan() const;
    void setPan(float value);

    // DSP Audio FX & Duration trim
    DeathDspEffect dspEffect() const;
    void setDspEffect(DeathDspEffect effect);
    float dspIntensity() const;
    void setDspIntensity(float mix);
    float maxDurationSec() const; // 0 = Off
    void setMaxDurationSec(float seconds);

    bool playDeath(FMODAudioEngine* engine, float speed, float sourceVolume);
    bool playDeath(FMODAudioEngine* engine, float speed, float sourceVolume,
                   float deathPercent, bool isNewBest);
    bool preview(std::filesystem::path const& path);
    bool previewFirstSelected();
    void stopPreview();
    void stopActiveDeath();
    void handleLevelReset();
    void handleLevelExit();

    void tickDurationTrim(float dt);

private:
    DeathEffectManager();
    ~DeathEffectManager();

    void ensureLoaded();
    void reloadPool();
    void releasePool();
    void saveSelection();
    void rebuildShuffleBag();
    std::size_t chooseSound();
    bool readSoundInfo(std::filesystem::path const& path, unsigned int* durationMs = nullptr) const;

    bool ensureChannelGroup(FMODAudioEngine* engine);
    void syncDspOnGroup();
    void releaseChannelGroup();
    void ensureTrimTicker(bool active);

    static bool isAudioFile(std::filesystem::path const& path);
    static std::string pathKey(std::filesystem::path const& path);
    static std::filesystem::path uniqueDestination(
        std::filesystem::path const& directory,
        std::filesystem::path const& filename
    );

    bool m_loaded = false;
    bool m_creditsLoaded = false;
    std::vector<std::pair<std::string, SoundCredit>> m_credits;
    void ensureCreditsLoaded();
    void saveCredits();
    void setCredit(std::filesystem::path const& path, SoundCredit const& credit);
    void eraseCredit(std::filesystem::path const& path);

    std::vector<std::string> m_selectedPaths;
    std::vector<FMOD::Sound*> m_pool;
    std::vector<std::size_t> m_shuffleBag;
    std::size_t m_nextIndex = 0;
    std::optional<std::size_t> m_lastIndex;
    std::mt19937 m_rng;

    std::string m_clutchPath;
    FMOD::Sound* m_clutchSound = nullptr;

    FMOD::ChannelGroup* m_sfxGroup = nullptr;
    FMOD::DSP* m_groupDsp = nullptr;
    DeathDspEffect m_attachedDspType = DeathDspEffect::None;

    struct ActiveChannel {
        FMOD::Channel* channel = nullptr;
        FMOD::Sound* sound = nullptr;
        float elapsed = 0.f;
        float baseVolume = 1.f;
    };
    std::vector<ActiveChannel> m_activeChannels;

    FMOD::Sound* m_previewSound = nullptr;
    FMOD::Channel* m_previewChannel = nullptr;
    float m_previewElapsed = 0.f;
    cocos2d::CCNode* m_trimNode = nullptr;
};

} // namespace paimon::death_effects
