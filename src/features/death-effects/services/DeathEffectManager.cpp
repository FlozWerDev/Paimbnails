#include "../../../utils/FilesystemPath.hpp"
#include "DeathEffectManager.hpp"
#include "DeathSfxFormat.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <system_error>

using namespace geode::prelude;

namespace paimon::death_effects {

namespace {
constexpr auto kSelectedSoundsKey = "death-effects-selected-sounds";
constexpr auto kClutchSoundKey = "death-effects-clutch-sound";
constexpr auto kClutchEnabledKey = "death-effects-clutch-enabled";
constexpr auto kClutchThresholdKey = "death-effects-clutch-threshold";
constexpr auto kClutchNewBestKey = "death-effects-clutch-new-best";
constexpr auto kPlaybackOrderKey = "death-effects-playback-order";
constexpr auto kAvoidRepeatsKey = "death-effects-avoid-repeats";
constexpr auto kStopOnResetKey = "death-effects-stop-on-reset";
constexpr auto kAllowOverlapKey = "death-effects-allow-overlap";
constexpr auto kLayerVanillaKey = "death-effects-layer-vanilla";
constexpr auto kVolumeKey = "death-effects-volume";
constexpr auto kPitchVariationKey = "death-effects-pitch-variation";
constexpr auto kSpeedKey = "death-effects-speed";
constexpr auto kPanKey = "death-effects-pan";
constexpr auto kDspEffectKey = "death-effects-dsp-type";
constexpr auto kDspMixKey = "death-effects-dsp-mix";
constexpr auto kMaxDurationKey = "death-effects-max-duration";

using paimon::death_effects::sanitizeFileStem;

std::string creditFileKey(std::filesystem::path const& path) {
    return paimon::paths::toUtf8(path.filename());
}

struct ImportBatch {
    std::vector<std::filesystem::path> paths;
    std::size_t next = 0;
    std::size_t skipped = 0;
    FolderImportResult result;
    std::function<void(FolderImportResult)> done;
};

void processImportBatch(std::shared_ptr<ImportBatch> batch) {
    if (!batch || paimon::isRuntimeShuttingDown()) return;
    constexpr std::size_t kPerTick = 8;
    std::size_t processed = 0;
    while (batch->next < batch->paths.size() && processed < kPerTick) {
        auto imported = DeathEffectManager::get().importSound(batch->paths[batch->next++]);
        if (imported) batch->result.imported.push_back(imported.unwrap());
        else ++batch->result.skipped;
        ++processed;
    }
    if (batch->next < batch->paths.size()) {
        paimon::scheduleMainThreadDelay(0.f, [batch] {
            processImportBatch(batch);
        });
        return;
    }
    batch->result.skipped += batch->skipped;
    if (batch->done) batch->done(std::move(batch->result));
}

class DeathSfxTrimNode : public cocos2d::CCNode {
public:
    static DeathSfxTrimNode* create() {
        auto* node = new DeathSfxTrimNode();
        if (node && node->init()) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    void start() {
        auto* sched = cocos2d::CCDirector::get()->getScheduler();
        sched->unscheduleSelector(schedule_selector(DeathSfxTrimNode::onTick), this);
        sched->scheduleSelector(
            schedule_selector(DeathSfxTrimNode::onTick),
            this, 0.f, kCCRepeatForever, 0.f, false
        );
    }

    void stop() {
        auto* sched = cocos2d::CCDirector::get()->getScheduler();
        sched->unscheduleSelector(schedule_selector(DeathSfxTrimNode::onTick), this);
    }

private:
    void onTick(float dt) {
        if (paimon::isRuntimeShuttingDown()) {
            stop();
            return;
        }
        DeathEffectManager::get().tickDurationTrim(dt);
    }
};

} // namespace

DeathEffectManager& DeathEffectManager::get() {
    static DeathEffectManager instance;
    return instance;
}

DeathEffectManager::DeathEffectManager()
    : m_rng(std::random_device{}()) {}

DeathEffectManager::~DeathEffectManager() {
    if (paimon::isRuntimeShuttingDown()) {
        m_trimNode = nullptr;
        m_sfxGroup = nullptr;
        m_groupDsp = nullptr;
        return;
    }
    stopPreview();
    stopActiveDeath();
    releasePool();
    releaseChannelGroup();
    if (m_trimNode) {
        static_cast<DeathSfxTrimNode*>(m_trimNode)->stop();
        m_trimNode->release();
        m_trimNode = nullptr;
    }
}

std::filesystem::path DeathEffectManager::libraryDir() const {
    return paimon::paths::toStdPath(Mod::get()->getConfigDir()) / "death-effects";
}

std::string DeathEffectManager::pathKey(std::filesystem::path const& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    auto normalized = ec ? path.lexically_normal() : absolute.lexically_normal();
    auto key = paimon::paths::toUtf8(normalized);
#ifdef GEODE_IS_WINDOWS
    key = geode::utils::string::toLower(key);
#endif
    return key;
}

bool DeathEffectManager::isAudioFile(std::filesystem::path const& path) {
    auto ext = geode::utils::string::toLower(
        paimon::paths::toUtf8(path.extension())
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
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec) || ec) return candidate;

    auto stem = paimon::paths::toUtf8(filename.stem());
    auto ext = paimon::paths::toUtf8(filename.extension());
    for (int suffix = 2;; ++suffix) {
        candidate = directory / paimon::assets::pathFromUtf8(
            fmt::format("{} ({}){}", stem, suffix, ext));
        ec.clear();
        if (!std::filesystem::exists(candidate, ec) || ec) return candidate;
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
    m_clutchPath = Mod::get()->getSavedValue<std::string>(kClutchSoundKey, "");
    reloadPool();
}

bool DeathEffectManager::readSoundInfo(
    std::filesystem::path const& path,
    unsigned int* durationMs
) const {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    FMOD::Sound* sound = nullptr;
    auto pathString = paimon::paths::toUtf8(path);
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
        info.name = paimon::paths::toUtf8(entry.path().stem());
        info.selected = isSelected(entry.path());
        info.isClutch = isClutchSound(entry.path());
        info.playable = readSoundInfo(entry.path(), &info.durationMs);
        if (auto credit = creditFor(entry.path())) {
            info.hasCredit = true;
            info.credit = std::move(*credit);
        }
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
        if (imported) result.imported.push_back(imported.unwrap());
        else ++result.skipped;
    }
    return result;
}

void DeathEffectManager::importFolderAsync(
    std::filesystem::path const& folder,
    std::function<void(FolderImportResult)> done
) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec) || ec) {
        if (done) done({});
        return;
    }

    auto const library = libraryDir();
    paimon::ThreadTracker::get().spawn(
        [folder, library, done = std::move(done)]() mutable {
            geode::utils::thread::setName("PaimonDeathSfxImport");
            std::vector<std::filesystem::path> prepared;
            std::size_t skipped = 0;
            std::error_code walkError;
            for (auto const& entry : std::filesystem::recursive_directory_iterator(
                     folder, std::filesystem::directory_options::skip_permission_denied, walkError)) {
                if (walkError) {
                    walkError.clear();
                    continue;
                }
                std::error_code typeError;
                if (!entry.is_regular_file(typeError) || typeError || !isAudioFile(entry.path())) {
                    continue;
                }
                if (pathKey(entry.path().parent_path()) == pathKey(library)) {
                    prepared.push_back(entry.path());
                    continue;
                }
                auto destination = uniqueDestination(library, entry.path().filename());
                std::error_code copyError;
                std::filesystem::copy_file(
                    entry.path(), destination, std::filesystem::copy_options::none, copyError);
                if (copyError) {
                    ++skipped;
                    continue;
                }
                prepared.push_back(std::move(destination));
            }

            Loader::get()->queueInMainThread(
                [prepared = std::move(prepared), skipped,
                 done = std::move(done)]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto batch = std::make_shared<ImportBatch>();
                    batch->paths = std::move(prepared);
                    batch->skipped = skipped;
                    batch->done = std::move(done);
                    processImportBatch(batch);
                });
        });
}

bool DeathEffectManager::removeSound(std::filesystem::path const& path) {
    ensureLoaded();
    if (pathKey(path.parent_path()) != pathKey(libraryDir())) return false;

    if (isClutchSound(path)) setClutchSound({});
    setSoundSelected(path, false);
    stopPreview();
    eraseCredit(path);

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

bool DeathEffectManager::isClutchSound(std::filesystem::path const& path) {
    ensureLoaded();
    if (m_clutchPath.empty()) return false;
    return pathKey(path) == m_clutchPath;
}

void DeathEffectManager::setClutchSound(std::filesystem::path const& path) {
    ensureLoaded();
    std::string nextKey = path.empty() ? std::string{} : pathKey(path);
    if (nextKey == m_clutchPath) return;
    m_clutchPath = std::move(nextKey);
    Mod::get()->setSavedValue(kClutchSoundKey, m_clutchPath);
    reloadPool();
}

std::string DeathEffectManager::clutchSoundName() const {
    if (m_clutchPath.empty()) return "(none)";
    return paimon::paths::toUtf8(std::filesystem::path(m_clutchPath).stem());
}

bool DeathEffectManager::clutchEnabled() const {
    return Mod::get()->getSavedValue<bool>(kClutchEnabledKey, true);
}
void DeathEffectManager::setClutchEnabled(bool enabled) {
    Mod::get()->setSavedValue(kClutchEnabledKey, enabled);
}
float DeathEffectManager::clutchThreshold() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kClutchThresholdKey, 80.0)),
        50.f, 99.f);
}
void DeathEffectManager::setClutchThreshold(float percent) {
    Mod::get()->setSavedValue(kClutchThresholdKey, static_cast<double>(std::clamp(percent, 50.f, 99.f)));
}
bool DeathEffectManager::clutchOnlyNewBest() const {
    return Mod::get()->getSavedValue<bool>(kClutchNewBestKey, false);
}
void DeathEffectManager::setClutchOnlyNewBest(bool enabled) {
    Mod::get()->setSavedValue(kClutchNewBestKey, enabled);
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
    auto val = Mod::get()->getSavedValue<std::string>(kPlaybackOrderKey, "shuffle");
    if (val == "sequential") return PlaybackOrder::Sequential;
    if (val == "random") return PlaybackOrder::Random;
    return PlaybackOrder::ShuffleBag;
}

void DeathEffectManager::setOrder(PlaybackOrder value) {
    char const* name = "shuffle";
    if (value == PlaybackOrder::Sequential) name = "sequential";
    else if (value == PlaybackOrder::Random) name = "random";
    Mod::get()->setSavedValue<std::string>(kPlaybackOrderKey, name);
    m_nextIndex = 0;
    m_lastIndex.reset();
    rebuildShuffleBag();
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

bool DeathEffectManager::allowOverlap() const {
    return Mod::get()->getSavedValue<bool>(kAllowOverlapKey, false);
}
void DeathEffectManager::setAllowOverlap(bool enabled) {
    Mod::get()->setSavedValue(kAllowOverlapKey, enabled);
}

bool DeathEffectManager::layerWithVanilla() const {
    return Mod::get()->getSavedValue<bool>(kLayerVanillaKey, false);
}
void DeathEffectManager::setLayerWithVanilla(bool enabled) {
    Mod::get()->setSavedValue(kLayerVanillaKey, enabled);
}

float DeathEffectManager::volume() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kVolumeKey, 1.0)),
        0.f, 2.f);
}
void DeathEffectManager::setVolume(float value) {
    Mod::get()->setSavedValue(kVolumeKey, static_cast<double>(std::clamp(value, 0.f, 2.f)));
}

float DeathEffectManager::pitchVariation() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kPitchVariationKey, 0.0)),
        0.f, 0.5f);
}
void DeathEffectManager::setPitchVariation(float value) {
    Mod::get()->setSavedValue(kPitchVariationKey, static_cast<double>(std::clamp(value, 0.f, 0.5f)));
}

float DeathEffectManager::speed() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kSpeedKey, 1.0)),
        0.25f, 2.f);
}
void DeathEffectManager::setSpeed(float value) {
    Mod::get()->setSavedValue(kSpeedKey, static_cast<double>(std::clamp(value, 0.25f, 2.f)));
}

float DeathEffectManager::pan() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kPanKey, 0.0)),
        -1.f, 1.f);
}
void DeathEffectManager::setPan(float value) {
    Mod::get()->setSavedValue(kPanKey, static_cast<double>(std::clamp(value, -1.f, 1.f)));
}

DeathDspEffect DeathEffectManager::dspEffect() const {
    int v = Mod::get()->getSavedValue<int>(kDspEffectKey, 0);
    return static_cast<DeathDspEffect>(std::clamp(v, 0, 7));
}
void DeathEffectManager::setDspEffect(DeathDspEffect effect) {
    Mod::get()->setSavedValue(kDspEffectKey, static_cast<int>(effect));
    syncDspOnGroup();
}

float DeathEffectManager::dspIntensity() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kDspMixKey, 0.65)),
        0.f, 1.f);
}
void DeathEffectManager::setDspIntensity(float mix) {
    Mod::get()->setSavedValue(kDspMixKey, static_cast<double>(std::clamp(mix, 0.f, 1.f)));
    syncDspOnGroup();
}

float DeathEffectManager::maxDurationSec() const {
    return std::clamp(
        static_cast<float>(Mod::get()->getSavedValue<double>(kMaxDurationKey, 0.0)),
        0.f, 10.f);
}
void DeathEffectManager::setMaxDurationSec(float seconds) {
    Mod::get()->setSavedValue(kMaxDurationKey, static_cast<double>(std::clamp(seconds, 0.f, 10.f)));
}

bool DeathEffectManager::ensureChannelGroup(FMODAudioEngine* engine) {
    if (!engine || !engine->m_system) return false;
    if (!m_sfxGroup) {
        if (engine->m_system->createChannelGroup("PaimonDeathSFX", &m_sfxGroup) != FMOD_OK || !m_sfxGroup) {
            m_sfxGroup = nullptr;
            return false;
        }
    }
    syncDspOnGroup();
    return true;
}

void DeathEffectManager::syncDspOnGroup() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !m_sfxGroup) return;

    DeathDspEffect const desired = dspEffect();
    float const mix = dspIntensity();

    if (m_groupDsp && m_attachedDspType != desired) {
        m_sfxGroup->removeDSP(m_groupDsp);
        m_groupDsp->release();
        m_groupDsp = nullptr;
        m_attachedDspType = DeathDspEffect::None;
    }

    if (desired == DeathDspEffect::None) return;

    if (!m_groupDsp) {
        FMOD_DSP_TYPE fmodType = FMOD_DSP_TYPE_UNKNOWN;
        switch (desired) {
            case DeathDspEffect::Reverb:     fmodType = FMOD_DSP_TYPE_SFXREVERB; break;
            case DeathDspEffect::Echo:       fmodType = FMOD_DSP_TYPE_ECHO; break;
            case DeathDspEffect::Distortion: fmodType = FMOD_DSP_TYPE_DISTORTION; break;
            case DeathDspEffect::LowPass:    fmodType = FMOD_DSP_TYPE_LOWPASS; break;
            case DeathDspEffect::HighPass:   fmodType = FMOD_DSP_TYPE_HIGHPASS; break;
            case DeathDspEffect::Chorus:     fmodType = FMOD_DSP_TYPE_CHORUS; break;
            case DeathDspEffect::Flange:     fmodType = FMOD_DSP_TYPE_FLANGE; break;
            case DeathDspEffect::None:       return;
        }
        if (engine->m_system->createDSPByType(fmodType, &m_groupDsp) != FMOD_OK || !m_groupDsp) {
            m_groupDsp = nullptr;
            return;
        }
        if (m_sfxGroup->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, m_groupDsp) != FMOD_OK) {
            m_groupDsp->release();
            m_groupDsp = nullptr;
            return;
        }
        m_groupDsp->setActive(true);
        m_attachedDspType = desired;
    }

    switch (desired) {
        case DeathDspEffect::Reverb:
            m_groupDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 1200.f + mix * 2800.f);
            m_groupDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, -2.f * mix);
            m_groupDsp->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, -22.f + mix * 22.f);
            break;
        case DeathDspEffect::Echo:
            m_groupDsp->setParameterFloat(FMOD_DSP_ECHO_DELAY, 140.f + mix * 160.f);
            m_groupDsp->setParameterFloat(FMOD_DSP_ECHO_FEEDBACK, 20.f + mix * 45.f);
            m_groupDsp->setParameterFloat(FMOD_DSP_ECHO_DRYLEVEL, 0.f);
            m_groupDsp->setParameterFloat(FMOD_DSP_ECHO_WETLEVEL, -16.f + mix * 16.f);
            break;
        case DeathDspEffect::Distortion:
            m_groupDsp->setParameterFloat(FMOD_DSP_DISTORTION_LEVEL, 0.25f + mix * 0.65f);
            break;
        case DeathDspEffect::LowPass: {
            float cutoff = 18000.f * std::pow(0.03f, mix);
            m_groupDsp->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, std::clamp(cutoff, 300.f, 20000.f));
            m_groupDsp->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 1.f + mix * 1.5f);
            break;
        }
        case DeathDspEffect::HighPass: {
            float cutoff = 80.f + mix * 1800.f;
            m_groupDsp->setParameterFloat(FMOD_DSP_HIGHPASS_CUTOFF, cutoff);
            m_groupDsp->setParameterFloat(FMOD_DSP_HIGHPASS_RESONANCE, 1.f + mix * 1.2f);
            break;
        }
        case DeathDspEffect::Chorus:
            m_groupDsp->setParameterFloat(FMOD_DSP_CHORUS_MIX, 20.f + mix * 70.f);
            m_groupDsp->setParameterFloat(FMOD_DSP_CHORUS_RATE, 0.8f + mix * 2.5f);
            m_groupDsp->setParameterFloat(FMOD_DSP_CHORUS_DEPTH, 3.f + mix * 12.f);
            break;
        case DeathDspEffect::Flange:
            m_groupDsp->setParameterFloat(FMOD_DSP_FLANGE_MIX, 20.f + mix * 75.f);
            m_groupDsp->setParameterFloat(FMOD_DSP_FLANGE_DEPTH, 0.5f + mix * 0.5f);
            m_groupDsp->setParameterFloat(FMOD_DSP_FLANGE_RATE, 0.4f + mix * 2.5f);
            break;
        case DeathDspEffect::None:
            break;
    }
}

void DeathEffectManager::releaseChannelGroup() {
    if (m_sfxGroup && m_groupDsp) {
        m_sfxGroup->removeDSP(m_groupDsp);
    }
    if (m_groupDsp) {
        m_groupDsp->release();
        m_groupDsp = nullptr;
    }
    if (m_sfxGroup) {
        m_sfxGroup->release();
        m_sfxGroup = nullptr;
    }
    m_attachedDspType = DeathDspEffect::None;
}

void DeathEffectManager::ensureCreditsLoaded() {
    if (m_creditsLoaded) return;
    m_creditsLoaded = true;

    auto path = libraryDir() / "credits.json";
    auto raw = geode::utils::file::readString(paimon::paths::toSdkPath(path));
    if (!raw) return;
    auto parsed = matjson::parse(raw.unwrap());
    if (!parsed.isOk() || !parsed.unwrap().isObject()) return;

    for (auto const& [file, value] : parsed.unwrap()) {
        if (!value.isObject()) continue;
        SoundCredit credit;
        credit.title = value["title"].asString().unwrapOr("");
        credit.creator = value["creator"].asString().unwrapOr("");
        credit.creatorUrl = value["creatorUrl"].asString().unwrapOr("");
        credit.license = value["license"].asString().unwrapOr("");
        credit.licenseUrl = value["licenseUrl"].asString().unwrapOr("");
        credit.sourceUrl = value["sourceUrl"].asString().unwrapOr("");
        credit.attribution = value["attribution"].asString().unwrapOr("");
        m_credits.emplace_back(file, std::move(credit));
    }
}

void DeathEffectManager::saveCredits() {
    ensureCreditsLoaded();
    matjson::Value root = matjson::makeObject({});
    for (auto const& [file, credit] : m_credits) {
        matjson::Value entry = matjson::makeObject({});
        entry["title"] = credit.title;
        entry["creator"] = credit.creator;
        entry["creatorUrl"] = credit.creatorUrl;
        entry["license"] = credit.license;
        entry["licenseUrl"] = credit.licenseUrl;
        entry["sourceUrl"] = credit.sourceUrl;
        entry["attribution"] = credit.attribution;
        root[file] = std::move(entry);
    }
    auto path = libraryDir() / "credits.json";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    (void)geode::utils::file::writeString(paimon::paths::toSdkPath(path), root.dump(matjson::NO_INDENTATION));
}

void DeathEffectManager::setCredit(std::filesystem::path const& path, SoundCredit const& credit) {
    ensureCreditsLoaded();
    auto key = creditFileKey(path);
    for (auto& [file, stored] : m_credits) {
        if (file == key) {
            stored = credit;
            saveCredits();
            return;
        }
    }
    m_credits.emplace_back(std::move(key), credit);
    saveCredits();
}

void DeathEffectManager::eraseCredit(std::filesystem::path const& path) {
    ensureCreditsLoaded();
    auto key = creditFileKey(path);
    auto before = m_credits.size();
    std::erase_if(m_credits, [&key](auto const& item) { return item.first == key; });
    if (m_credits.size() != before) saveCredits();
}

std::optional<SoundCredit> DeathEffectManager::creditFor(std::filesystem::path const& path) {
    ensureCreditsLoaded();
    auto key = creditFileKey(path);
    for (auto const& [file, credit] : m_credits) {
        if (file == key) return credit;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, SoundCredit>> DeathEffectManager::allCredits() {
    ensureCreditsLoaded();
    return m_credits;
}

Result<std::filesystem::path> DeathEffectManager::importDownloaded(
    std::string const& displayName,
    std::string const& extension,
    std::vector<std::uint8_t> const& bytes,
    SoundCredit const& credit
) {
    if (bytes.empty()) return Err("The download was empty.");
    if (credit.license.empty() || credit.sourceUrl.empty()) {
        return Err("This sound has no license information and cannot be imported.");
    }

    auto ext = geode::utils::string::toLower(extension);
    if (!ext.empty() && ext.front() != '.') ext = "." + ext;
    if (!isAudioFile(std::filesystem::path("sound" + ext))) {
        return Err("Unsupported audio format.");
    }

    auto directory = libraryDir();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return Err("Could not create the death-effects folder.");

    auto destination = uniqueDestination(
        directory, paimon::assets::pathFromUtf8(sanitizeFileStem(displayName) + ext));
    if (!geode::utils::file::writeBinary(paimon::paths::toSdkPath(destination), geode::ByteVector(bytes.begin(), bytes.end()))) {
        return Err("Could not save the downloaded sound.");
    }
    if (!readSoundInfo(destination)) {
        std::filesystem::remove(destination, ec);
        return Err("FMOD could not read this audio file.");
    }

    setCredit(destination, credit);
    return Ok(destination);
}

void DeathEffectManager::releasePool() {
    for (auto* sound : m_pool) {
        if (sound) sound->release();
    }
    m_pool.clear();
    m_shuffleBag.clear();
    if (m_clutchSound) {
        m_clutchSound->release();
        m_clutchSound = nullptr;
    }
}

void DeathEffectManager::rebuildShuffleBag() {
    m_shuffleBag.resize(m_pool.size());
    std::iota(m_shuffleBag.begin(), m_shuffleBag.end(), 0);
    std::shuffle(m_shuffleBag.begin(), m_shuffleBag.end(), m_rng);
    if (m_shuffleBag.size() > 1 && m_lastIndex && m_shuffleBag.back() == *m_lastIndex) {
        std::swap(m_shuffleBag.front(), m_shuffleBag.back());
    }
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
        auto pathString = paimon::paths::toUtf8(path);
        auto result = engine->m_system->createSound(
            pathString.c_str(), FMOD_DEFAULT, nullptr, &sound
        );
        if (result == FMOD_OK && sound) {
            m_pool.push_back(sound);
        }
    }

    if (existing != m_selectedPaths) {
        m_selectedPaths = std::move(existing);
        saveSelection();
    }

    if (!m_clutchPath.empty()) {
        std::filesystem::path cpath(m_clutchPath);
        std::error_code ec;
        if (std::filesystem::is_regular_file(cpath, ec) && isAudioFile(cpath)) {
            auto cstr = paimon::paths::toUtf8(cpath);
            if (engine->m_system->createSound(cstr.c_str(), FMOD_DEFAULT, nullptr, &m_clutchSound) != FMOD_OK) {
                m_clutchSound = nullptr;
            }
        } else {
            m_clutchPath.clear();
            Mod::get()->setSavedValue(kClutchSoundKey, m_clutchPath);
        }
    }

    rebuildShuffleBag();
}

std::size_t DeathEffectManager::chooseSound() {
    if (m_pool.size() <= 1) return 0;

    PlaybackOrder const ord = order();
    if (ord == PlaybackOrder::Sequential) {
        auto selected = m_nextIndex % m_pool.size();
        ++m_nextIndex;
        m_lastIndex = selected;
        return selected;
    }

    if (ord == PlaybackOrder::ShuffleBag) {
        if (m_shuffleBag.empty()) rebuildShuffleBag();
        std::size_t selected = m_shuffleBag.back();
        m_shuffleBag.pop_back();
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

void DeathEffectManager::ensureTrimTicker(bool active) {
    if (active) {
        if (!m_trimNode) {
            m_trimNode = DeathSfxTrimNode::create();
            if (m_trimNode) m_trimNode->retain();
        }
        if (m_trimNode) static_cast<DeathSfxTrimNode*>(m_trimNode)->start();
    } else if (m_trimNode) {
        static_cast<DeathSfxTrimNode*>(m_trimNode)->stop();
    }
}

void DeathEffectManager::tickDurationTrim(float dt) {
    float const cap = maxDurationSec();
    constexpr float kFadeOutWindow = 0.16f;

    auto it = m_activeChannels.begin();
    while (it != m_activeChannels.end()) {
        if (!it->channel) {
            it = m_activeChannels.erase(it);
            continue;
        }
        FMOD::Sound* cur = nullptr;
        bool playing = false;
        if (it->channel->getCurrentSound(&cur) != FMOD_OK || cur != it->sound ||
            it->channel->isPlaying(&playing) != FMOD_OK || !playing) {
            it = m_activeChannels.erase(it);
            continue;
        }
        it->elapsed += dt;
        if (cap > 0.f) {
            if (it->elapsed >= cap) {
                it->channel->stop();
                it = m_activeChannels.erase(it);
                continue;
            }
            if (it->elapsed >= std::max(0.f, cap - kFadeOutWindow)) {
                float rem = std::clamp((cap - it->elapsed) / kFadeOutWindow, 0.f, 1.f);
                it->channel->setVolume(it->baseVolume * rem);
            }
        }
        ++it;
    }

    if (m_previewChannel && m_previewSound) {
        FMOD::Sound* cur = nullptr;
        bool playing = false;
        if (m_previewChannel->getCurrentSound(&cur) == FMOD_OK && cur == m_previewSound &&
            m_previewChannel->isPlaying(&playing) == FMOD_OK && playing) {
            m_previewElapsed += dt;
            if (cap > 0.f && m_previewElapsed >= cap) {
                stopPreview();
            } else if (cap > 0.f && m_previewElapsed >= std::max(0.f, cap - kFadeOutWindow)) {
                float rem = std::clamp((cap - m_previewElapsed) / kFadeOutWindow, 0.f, 1.f);
                auto* engine = FMODAudioEngine::sharedEngine();
                float baseV = volume() * (engine ? engine->getEffectsVolume() : 1.f);
                m_previewChannel->setVolume(baseV * rem);
            }
        } else {
            stopPreview();
        }
    }

    if (m_activeChannels.empty() && !m_previewChannel) {
        ensureTrimTicker(false);
    }
}

bool DeathEffectManager::playDeath(FMODAudioEngine* engine, float speed, float sourceVolume) {
    return playDeath(engine, speed, sourceVolume, -1.f, false);
}

bool DeathEffectManager::playDeath(
    FMODAudioEngine* engine,
    float speed,
    float sourceVolume,
    float deathPercent,
    bool isNewBest
) {
    ensureLoaded();
    if (!engine || !engine->m_system) return false;

    FMOD::Sound* sound = nullptr;
    bool const clutchHit = clutchEnabled() && m_clutchSound &&
        deathPercent >= clutchThreshold() &&
        (!clutchOnlyNewBest() || isNewBest);
    if (clutchHit) {
        sound = m_clutchSound;
    } else if (!m_pool.empty()) {
        sound = m_pool[chooseSound()];
    }
    if (!sound) return false;

    if (!allowOverlap()) {
        stopActiveDeath();
    } else if (m_activeChannels.size() >= 6) {
        if (m_activeChannels.front().channel) m_activeChannels.front().channel->stop();
        m_activeChannels.erase(m_activeChannels.begin());
    }

    ensureChannelGroup(engine);

    auto variation = pitchVariation();
    std::uniform_real_distribution<float> pitchOffset(-variation, variation);
    auto pitch = std::max(0.05f, speed * this->speed() * (1.f + pitchOffset(m_rng)));
    float const playVol = sourceVolume * volume() * engine->getEffectsVolume();

    FMOD::Channel* channel = nullptr;
    auto result = engine->m_system->playSound(sound, m_sfxGroup, true, &channel);
    if (result != FMOD_OK || !channel) return false;

    if (channel->setVolume(playVol) != FMOD_OK ||
        channel->setPitch(pitch) != FMOD_OK ||
        channel->setPan(pan()) != FMOD_OK ||
        channel->setPaused(false) != FMOD_OK) {
        channel->stop();
        return false;
    }

    m_activeChannels.push_back(ActiveChannel{channel, sound, 0.f, playVol});
    if (maxDurationSec() > 0.f) {
        ensureTrimTicker(true);
    }
    return true;
}

bool DeathEffectManager::preview(std::filesystem::path const& path) {
    stopPreview();

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;

    ensureChannelGroup(engine);

    auto pathString = paimon::paths::toUtf8(path);
    auto result = engine->m_system->createSound(
        pathString.c_str(), FMOD_DEFAULT, nullptr, &m_previewSound
    );
    if (result != FMOD_OK || !m_previewSound) {
        m_previewSound = nullptr;
        return false;
    }

    result = engine->m_system->playSound(
        m_previewSound, m_sfxGroup, true, &m_previewChannel
    );
    if (result != FMOD_OK || !m_previewChannel) {
        m_previewSound->release();
        m_previewSound = nullptr;
        m_previewChannel = nullptr;
        return false;
    }

    m_previewElapsed = 0.f;
    if (m_previewChannel->setVolume(volume() * engine->getEffectsVolume()) != FMOD_OK ||
        m_previewChannel->setPitch(std::max(0.05f, speed())) != FMOD_OK ||
        m_previewChannel->setPan(pan()) != FMOD_OK ||
        m_previewChannel->setPaused(false) != FMOD_OK) {
        stopPreview();
        return false;
    }
    if (maxDurationSec() > 0.f) {
        ensureTrimTicker(true);
    }
    return true;
}

bool DeathEffectManager::previewFirstSelected() {
    ensureLoaded();
    if (!m_selectedPaths.empty()) {
        return preview(paimon::assets::pathFromUtf8(m_selectedPaths.front()));
    }
    if (!m_clutchPath.empty()) {
        return preview(paimon::assets::pathFromUtf8(m_clutchPath));
    }
    return false;
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
    m_previewElapsed = 0.f;
}

void DeathEffectManager::stopActiveDeath() {
    for (auto const& ch : m_activeChannels) {
        if (!ch.channel) continue;
        FMOD::Sound* currentSound = nullptr;
        bool playing = false;
        if (ch.channel->getCurrentSound(&currentSound) == FMOD_OK &&
            currentSound == ch.sound &&
            ch.channel->isPlaying(&playing) == FMOD_OK && playing) {
            ch.channel->stop();
        }
    }
    m_activeChannels.clear();
}

void DeathEffectManager::handleLevelReset() {
    if (stopOnReset()) stopActiveDeath();
}

void DeathEffectManager::handleLevelExit() {
    stopPreview();
    stopActiveDeath();
}

} // namespace paimon::death_effects
