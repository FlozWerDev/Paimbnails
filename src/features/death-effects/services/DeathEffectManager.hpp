#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace paimon::death_effects {

enum class PlaybackOrder {
    Random,
    Sequential,
};

struct SoundInfo {
    std::filesystem::path path;
    std::string name;
    unsigned int durationMs = 0;
    bool selected = false;
    bool playable = false;
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
    bool removeSound(std::filesystem::path const& path);

    bool isSelected(std::filesystem::path const& path);
    void setSoundSelected(std::filesystem::path const& path, bool selected);
    void addSelected(std::vector<std::filesystem::path> const& paths);
    void useOriginal();

    std::size_t selectedCount();
    std::size_t readyCount();

    PlaybackOrder order() const;
    void setOrder(PlaybackOrder order);
    bool avoidRepeats() const;
    void setAvoidRepeats(bool enabled);
    bool stopOnReset() const;
    void setStopOnReset(bool enabled);
    float volume() const;
    void setVolume(float value);
    float pitchVariation() const;
    void setPitchVariation(float value);

    bool playDeath(FMODAudioEngine* engine, float speed, float sourceVolume);
    bool preview(std::filesystem::path const& path);
    void stopPreview();
    void stopActiveDeath();
    void handleLevelReset();
    void handleLevelExit();

private:
    DeathEffectManager();

    void ensureLoaded();
    void reloadPool();
    void releasePool();
    void saveSelection();
    std::size_t chooseSound();
    bool readSoundInfo(std::filesystem::path const& path, unsigned int* durationMs = nullptr) const;

    static bool isAudioFile(std::filesystem::path const& path);
    static std::string pathKey(std::filesystem::path const& path);
    static std::filesystem::path uniqueDestination(
        std::filesystem::path const& directory,
        std::filesystem::path const& filename
    );

    bool m_loaded = false;
    std::vector<std::string> m_selectedPaths;
    std::vector<FMOD::Sound*> m_pool;
    std::size_t m_nextIndex = 0;
    std::optional<std::size_t> m_lastIndex;
    std::mt19937 m_rng;

    FMOD::Channel* m_deathChannel = nullptr;
    FMOD::Sound* m_activeDeathSound = nullptr;
    FMOD::Sound* m_previewSound = nullptr;
    FMOD::Channel* m_previewChannel = nullptr;
};

} // namespace paimon::death_effects
