#include "DeathEffectManager.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <array>
#include <system_error>

using namespace geode::prelude;

namespace paimon::death_effects {

namespace {
constexpr auto kSelectedSoundsKey = "death-effects-selected-sounds";
constexpr auto kPlaybackOrderKey = "death-effects-playback-order";
constexpr auto kAvoidRepeatsKey = "death-effects-avoid-repeats";
constexpr auto kStopOnResetKey = "death-effects-stop-on-reset";
constexpr auto kVolumeKey = "death-effects-volume";
constexpr auto kPitchVariationKey = "death-effects-pitch-variation";
}

DeathEffectManager& DeathEffectManager::get() {
    static DeathEffectManager instance;
    return instance;
}

DeathEffectManager::DeathEffectManager()
  : m_rng(std::random_device{}()) {}

std::filesystem::path DeathEffectManager::libraryDir() const {
    return Mod::get()->getConfigDir() / "death-effects";
}

std::string DeathEffectManager::pathKey(std::filesystem::path const& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    auto normalized = ec ? path.lexically_normal() : absolute.lexically_normal();
    auto key = geode::utils::string::pathToString(normalized);
#ifdef GEODE_IS_WINDOWS
    key = geode::utils::string::toLower(key);
#endif
    return key;
}

bool DeathEffectManager::isAudioFile(std::filesystem::path const& path) {
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(path.extension())
    );
    static constexpr std::array<std::string_view, 7> supported = {
        ".mp3", ".ogg", ".wav", ".flac", ".oga", ".m4a", ".opus"
    };
    return std::find(supported.begin(), supported.end(), ext) != supported.end();
}

std::filesystem::path DeathEffectManager::uniqueDestination(
    std::filesystem::path const& directory,
    std::filesystem::path const& filename
) {
    auto candidate = directory / filename.filename();
    if (!std::filesystem::exists(candidate)) return candidate;

    auto stem = geode::utils::string::pathToString(filename.stem());
    auto ext = geode::utils::string::pathToString(filename.extension());
    for (int suffix = 2;; ++suffix) {
        candidate = directory / fmt::format("{} ({}){}", stem, suffix, ext);
        if (!std::filesystem::exists(candidate)) return candidate;
    }
}

void DeathEffectManager::ensureLoaded() {
    if (m_loaded) return;
    m_loaded = true;

    m_selectedPaths = Mod::get()->getSavedValue<std::vector<std::string>>(
        kSelectedSoundsKey, {}
    );
    std::sort(m_selectedPaths.begin(), m_selectedPaths.end());
    m_selectedPaths.erase(
        std::unique(m_selectedPaths.begin(), m_selectedPaths.end()),
        m_selectedPaths.end()
    );
    reloadPool();
}

bool DeathEffectManager::readSoundInfo(
    std::filesystem::path const& path,
    unsigned int* durationMs
) const {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    FMOD::Sound* sound = nullptr;
    auto pathString = geode::utils::string::pathToString(path);
    auto result = engine->m_system->createSound(
        pathString.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME, nullptr, &sound
    );
    if (result != FMOD_OK || !sound) return false;

    if (durationMs) {
        *durationMs = 0;
        sound->getLength(durationMs, FMOD_TIMEUNIT_MS);
    }
    sound->release();
    return true;
}

std::vector<SoundInfo> DeathEffectManager::scanLibrary() {
    ensureLoaded();

    std::vector<SoundInfo> sounds;
    std::error_code ec;
    std::filesystem::create_directories(libraryDir(), ec);
    if (ec) return sounds;

    for (auto const& entry : std::filesystem::directory_iterator(
             libraryDir(), std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        std::error_code typeError;
        if (!entry.is_regular_file(typeError) || typeError || !isAudioFile(entry.path())) {
            continue;
        }

        SoundInfo info;
        info.path = entry.path();
        info.name = geode::utils::string::pathToString(entry.path().stem());
        info.selected = isSelected(entry.path());
        info.playable = readSoundInfo(entry.path(), &info.durationMs);
        sounds.push_back(std::move(info));
    }

    std::sort(sounds.begin(), sounds.end(), [](SoundInfo const& left, SoundInfo const& right) {
        return geode::utils::string::toLower(left.name) <
               geode::utils::string::toLower(right.name);
    });
    return sounds;
}

Result<std::filesystem::path> DeathEffectManager::importSound(
    std::filesystem::path const& source
) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        return Err("The selected file no longer exists.");
    }
    if (!isAudioFile(source)) {
        return Err("Unsupported audio format.");
    }
    if (!readSoundInfo(source)) {
        return Err("FMOD could not read this audio file.");
    }

    auto directory = libraryDir();
    std::filesystem::create_directories(directory, ec);
    if (ec) return Err("Could not create the death-effects folder.");

    if (pathKey(source.parent_path()) == pathKey(directory)) {
        return Ok(source);
    }

    auto destination = uniqueDestination(directory, source.filename());
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    if (ec) return Err("Could not copy the audio file: {}", ec.message());
    return Ok(destination);
}

FolderImportResult DeathEffectManager::importFolder(
    std::filesystem::path const& folder
) {
    FolderImportResult result;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec) || ec) return result;

    std::vector<std::filesystem::path> files;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(
             folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        std::error_code typeError;
        if (!entry.is_regular_file(typeError) || typeError || !isAudioFile(entry.path())) {
            continue;
        }

        files.push_back(entry.path());
    }

    for (auto const& file : files) {
        auto imported = importSound(file);
        if (imported) {
            result.imported.push_back(imported.unwrap());
        } else {
            ++result.skipped;
        }
    }
    return result;
}

bool DeathEffectManager::removeSound(std::filesystem::path const& path) {
    ensureLoaded();
    if (pathKey(path.parent_path()) != pathKey(libraryDir())) return false;

    setSoundSelected(path, false);
    stopPreview();

    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
}

bool DeathEffectManager::isSelected(std::filesystem::path const& path) {
    ensureLoaded();
    auto key = pathKey(path);
    return std::find(m_selectedPaths.begin(), m_selectedPaths.end(), key) !=
           m_selectedPaths.end();
}

void DeathEffectManager::saveSelection() {
    Mod::get()->setSavedValue(kSelectedSoundsKey, m_selectedPaths);
}

void DeathEffectManager::setSoundSelected(
    std::filesystem::path const& path,
    bool selected
) {
    ensureLoaded();
    auto key = pathKey(path);
    auto found = std::find(m_selectedPaths.begin(), m_selectedPaths.end(), key);

    if (selected && found == m_selectedPaths.end()) {
        m_selectedPaths.push_back(std::move(key));
    } else if (!selected && found != m_selectedPaths.end()) {
        m_selectedPaths.erase(found);
    } else {
        return;
    }

    std::sort(m_selectedPaths.begin(), m_selectedPaths.end());
    saveSelection();
    reloadPool();
}

void DeathEffectManager::addSelected(
    std::vector<std::filesystem::path> const& paths
) {
    ensureLoaded();
    bool changed = false;
    for (auto const& path : paths) {
        auto key = pathKey(path);
        if (std::find(m_selectedPaths.begin(), m_selectedPaths.end(), key) ==
            m_selectedPaths.end()) {
            m_selectedPaths.push_back(std::move(key));
            changed = true;
        }
    }
    if (!changed) return;

    std::sort(m_selectedPaths.begin(), m_selectedPaths.end());
    saveSelection();
    reloadPool();
}

void DeathEffectManager::useOriginal() {
    ensureLoaded();
    if (m_selectedPaths.empty()) return;
    m_selectedPaths.clear();
    saveSelection();
    reloadPool();
}

std::size_t DeathEffectManager::selectedCount() {
    ensureLoaded();
    return m_selectedPaths.size();
}

std::size_t DeathEffectManager::readyCount() {
    ensureLoaded();
    return m_pool.size();
}

PlaybackOrder DeathEffectManager::order() const {
    return Mod::get()->getSavedValue<std::string>(kPlaybackOrderKey, "random") ==
                   "sequential"
        ? PlaybackOrder::Sequential
        : PlaybackOrder::Random;
}

void DeathEffectManager::setOrder(PlaybackOrder value) {
    Mod::get()->setSavedValue<std::string>(
        kPlaybackOrderKey,
        value == PlaybackOrder::Sequential ? "sequential" : "random"
    );
    m_nextIndex = 0;
    m_lastIndex.reset();
}

bool DeathEffectManager::avoidRepeats() const {
    return Mod::get()->getSavedValue<bool>(kAvoidRepeatsKey, true);
}

void DeathEffectManager::setAvoidRepeats(bool enabled) {
    Mod::get()->setSavedValue(kAvoidRepeatsKey, enabled);
}

bool DeathEffectManager::stopOnReset() const {
    return Mod::get()->getSavedValue<bool>(kStopOnResetKey, true);
}

void DeathEffectManager::setStopOnReset(bool enabled) {
    Mod::get()->setSavedValue(kStopOnResetKey, enabled);
}

float DeathEffectManager::volume() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kVolumeKey, 1.0)),
        0.f,
        2.f
    );
}

void DeathEffectManager::setVolume(float value) {
    Mod::get()->setSavedValue(kVolumeKey, static_cast<double>(std::clamp(value, 0.f, 2.f)));
}

float DeathEffectManager::pitchVariation() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kPitchVariationKey, 0.0)),
        0.f,
        0.5f
    );
}

void DeathEffectManager::setPitchVariation(float value) {
    Mod::get()->setSavedValue(
        kPitchVariationKey,
        static_cast<double>(std::clamp(value, 0.f, 0.5f))
    );
}

void DeathEffectManager::releasePool() {
    for (auto* sound : m_pool) {
        if (sound) sound->release();
    }
    m_pool.clear();
}

void DeathEffectManager::reloadPool() {
    stopActiveDeath();
    releasePool();
    m_nextIndex = 0;
    m_lastIndex.reset();

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    std::vector<std::string> existing;
    for (auto const& selected : m_selectedPaths) {
        std::filesystem::path path(selected);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec || !isAudioFile(path)) {
            continue;
        }

        existing.push_back(selected);
        FMOD::Sound* sound = nullptr;
        auto pathString = geode::utils::string::pathToString(path);
        auto result = engine->m_system->createSound(
            pathString.c_str(), FMOD_DEFAULT, nullptr, &sound
        );
        if (result == FMOD_OK && sound) {
            m_pool.push_back(sound);
        } else {
            log::warn(
                "[DeathEffects] Could not load '{}' (FMOD {})",
                pathString,
                static_cast<int>(result)
            );
        }
    }

    if (existing != m_selectedPaths) {
        m_selectedPaths = std::move(existing);
        saveSelection();
    }
}

std::size_t DeathEffectManager::chooseSound() {
    if (m_pool.size() <= 1) return 0;

    if (order() == PlaybackOrder::Sequential) {
        auto selected = m_nextIndex % m_pool.size();
        ++m_nextIndex;
        m_lastIndex = selected;
        return selected;
    }

    std::uniform_int_distribution<std::size_t> distribution(0, m_pool.size() - 1);
    auto selected = distribution(m_rng);
    if (avoidRepeats() && m_lastIndex && selected == *m_lastIndex) {
        std::uniform_int_distribution<std::size_t> alternate(0, m_pool.size() - 2);
        selected = alternate(m_rng);
        if (selected >= *m_lastIndex) ++selected;
    }
    m_lastIndex = selected;
    return selected;
}

bool DeathEffectManager::playDeath(
    FMODAudioEngine* engine,
    float speed,
    float sourceVolume
) {
    ensureLoaded();
    if (!engine || !engine->m_system || m_pool.empty()) return false;

    stopActiveDeath();
    auto* sound = m_pool[chooseSound()];
    if (!sound) return false;

    auto variation = pitchVariation();
    std::uniform_real_distribution<float> pitchOffset(-variation, variation);
    auto pitch = std::max(0.05f, speed * (1.f + pitchOffset(m_rng)));

    FMOD::Channel* channel = nullptr;
    auto result = engine->m_system->playSound(sound, nullptr, true, &channel);
    if (result != FMOD_OK || !channel) return false;

    if (channel->setVolume(sourceVolume * volume() * engine->getEffectsVolume()) != FMOD_OK ||
        channel->setPitch(pitch) != FMOD_OK ||
        channel->setPaused(false) != FMOD_OK) {
        channel->stop();
        return false;
    }
    m_deathChannel = channel;
    m_activeDeathSound = sound;
    return true;
}

bool DeathEffectManager::preview(std::filesystem::path const& path) {
    stopPreview();

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    auto pathString = geode::utils::string::pathToString(path);
    auto result = engine->m_system->createSound(
        pathString.c_str(), FMOD_DEFAULT, nullptr, &m_previewSound
    );
    if (result != FMOD_OK || !m_previewSound) {
        m_previewSound = nullptr;
        return false;
    }

    result = engine->m_system->playSound(
        m_previewSound, nullptr, true, &m_previewChannel
    );
    if (result != FMOD_OK || !m_previewChannel) {
        m_previewSound->release();
        m_previewSound = nullptr;
        m_previewChannel = nullptr;
        return false;
    }

    if (m_previewChannel->setVolume(volume() * engine->getEffectsVolume()) != FMOD_OK ||
        m_previewChannel->setPaused(false) != FMOD_OK) {
        stopPreview();
        return false;
    }
    return true;
}

void DeathEffectManager::stopPreview() {
    if (m_previewChannel) {
        FMOD::Sound* currentSound = nullptr;
        if (m_previewChannel->getCurrentSound(&currentSound) == FMOD_OK &&
            currentSound == m_previewSound) {
            m_previewChannel->stop();
        }
        m_previewChannel = nullptr;
    }
    if (m_previewSound) {
        m_previewSound->release();
        m_previewSound = nullptr;
    }
}

void DeathEffectManager::stopActiveDeath() {
    if (!m_deathChannel) return;

    FMOD::Sound* currentSound = nullptr;
    bool playing = false;
    if (m_deathChannel->getCurrentSound(&currentSound) == FMOD_OK &&
        currentSound == m_activeDeathSound &&
        m_deathChannel->isPlaying(&playing) == FMOD_OK && playing) {
        m_deathChannel->stop();
    }
    m_deathChannel = nullptr;
    m_activeDeathSound = nullptr;
}

void DeathEffectManager::handleLevelReset() {
    if (stopOnReset()) stopActiveDeath();
}

void DeathEffectManager::handleLevelExit() {
    stopPreview();
    stopActiveDeath();
}

} // namespace paimon::death_effects
