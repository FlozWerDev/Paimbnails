#include "DynamicVolumeManager.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::dynvol {

namespace {

constexpr char const* kKeyEnabled     = "dynamic-volume-enabled";
constexpr char const* kKeyMode        = "dynamic-volume-mode";
constexpr char const* kKeyRamp        = "dynamic-volume-ramp-seconds";
constexpr char const* kKeyCurve       = "dynamic-volume-curve";
constexpr char const* kKeyStrength    = "dynamic-volume-curve-strength";
constexpr char const* kKeyDuck        = "dynamic-volume-initial-duck-db";
constexpr char const* kKeyMaxCut      = "dynamic-volume-max-cut-db";
constexpr char const* kKeyMaxBoost    = "dynamic-volume-max-boost-db";
constexpr char const* kKeyThreshold   = "dynamic-volume-threshold-db";
constexpr char const* kKeyTarget      = "dynamic-volume-target-lufs";
constexpr char const* kKeyAnalysis    = "dynamic-volume-analysis-seconds";
constexpr char const* kKeySmoothing   = "dynamic-volume-smoothing-seconds";
constexpr char const* kKeyInMenus     = "dynamic-volume-in-menus";
constexpr char const* kKeyInGameplay  = "dynamic-volume-in-gameplay";
constexpr char const* kKeyInEditor    = "dynamic-volume-in-editor";
constexpr char const* kKeyReduckSame  = "dynamic-volume-reduck-same-song";

// Keep the fader below FMOD's roughly +10 dB ceiling.
constexpr float kMinGainDb = -60.0f;
constexpr float kMaxGainDb = 9.0f;

// Compare incoming analysis peaks against a decaying peak, not an average.
constexpr float kPeakDecayDbPerSec = 0.6f;

constexpr char const* kSafeDropModule = "paimbnails.safedrop.global";

constexpr float kRampGainAttackSeconds = 0.05f;
constexpr float kSafeDropAttackSeconds = 0.015f;
constexpr float kLimiterReleaseMs       = 60.0f;
constexpr float kLimiterCeilingDb       = -1.0f;

float approach(float current, float target, float dt, float seconds) {
    float const alpha = 1.0f - std::exp(-std::max(0.0f, dt) / std::max(0.01f, seconds));
    return current + (target - current) * alpha;
}

bool moduleOn() {
    return paimon::modules::isEnabled("paimbnails.dynamicvolume.global");
}

bool safeDropOn() {
    return paimon::modules::isEnabled(kSafeDropModule);
}

}

DynamicVolumeManager& DynamicVolumeManager::get() {
    static DynamicVolumeManager instance;
    return instance;
}

bool DynamicVolumeManager::isSafeDropEnabled() const {
    return paimon::modules::isSelfEnabled(kSafeDropModule);
}

void DynamicVolumeManager::setSafeDropEnabled(bool enabled) {
    paimon::modules::setEnabled(kSafeDropModule, enabled);
    if (!enabled) setSafeDropActive(false);
}

void DynamicVolumeManager::loadConfig() {
    auto* mod = Mod::get();
    DynamicVolumeConfig cfg{};

    cfg.enabled = mod->getSavedValue<bool>(kKeyEnabled, cfg.enabled);
    cfg.mode    = modeFromId(mod->getSavedValue<std::string>(kKeyMode, modeId(cfg.mode)));

    cfg.rampSeconds      = mod->getSavedValue<float>(kKeyRamp, cfg.rampSeconds);
    cfg.curve            = curveFromId(mod->getSavedValue<std::string>(kKeyCurve, curveId(cfg.curve)));
    cfg.curveStrength    = mod->getSavedValue<float>(kKeyStrength, cfg.curveStrength);
    cfg.initialDuckDb    = mod->getSavedValue<float>(kKeyDuck, cfg.initialDuckDb);
    cfg.maxCutDb         = mod->getSavedValue<float>(kKeyMaxCut, cfg.maxCutDb);
    cfg.maxBoostDb       = mod->getSavedValue<float>(kKeyMaxBoost, cfg.maxBoostDb);
    cfg.thresholdDb      = mod->getSavedValue<float>(kKeyThreshold, cfg.thresholdDb);
    cfg.targetLufs       = mod->getSavedValue<float>(kKeyTarget, cfg.targetLufs);
    cfg.analysisSeconds  = mod->getSavedValue<float>(kKeyAnalysis, cfg.analysisSeconds);
    cfg.smoothingSeconds = mod->getSavedValue<float>(kKeySmoothing, cfg.smoothingSeconds);

    cfg.inMenus        = mod->getSavedValue<bool>(kKeyInMenus, cfg.inMenus);
    cfg.inGameplay     = mod->getSavedValue<bool>(kKeyInGameplay, cfg.inGameplay);
    cfg.inEditor       = mod->getSavedValue<bool>(kKeyInEditor, cfg.inEditor);
    cfg.reduckSameSong = mod->getSavedValue<bool>(kKeyReduckSame, cfg.reduckSameSong);

    // Adaptive/Fixed ignore the fine knobs; prevent Custom values leaking in.
    applyModeDefaults(cfg);

    cfg.rampSeconds      = std::clamp(cfg.rampSeconds, 1.0f, 60.0f);
    cfg.curveStrength    = std::clamp(cfg.curveStrength, 1.0f, 6.0f);
    cfg.initialDuckDb    = std::clamp(cfg.initialDuckDb, -24.0f, 0.0f);
    cfg.maxCutDb         = std::clamp(cfg.maxCutDb, 0.0f, 30.0f);
    cfg.maxBoostDb       = std::clamp(cfg.maxBoostDb, 0.0f, 12.0f);
    cfg.thresholdDb      = std::clamp(cfg.thresholdDb, 0.0f, 12.0f);
    cfg.targetLufs       = std::clamp(cfg.targetLufs, -30.0f, -6.0f);
    cfg.analysisSeconds  = std::clamp(cfg.analysisSeconds, 0.4f, 5.0f);
    cfg.smoothingSeconds = std::clamp(cfg.smoothingSeconds, 0.05f, 2.0f);

    m_cfg = cfg;
    m_loaded = true;
}

void DynamicVolumeManager::saveConfig(DynamicVolumeConfig const& incoming) {
    DynamicVolumeConfig cfg = incoming;
    applyModeDefaults(cfg);

    auto* mod = Mod::get();
    mod->setSavedValue(kKeyEnabled, cfg.enabled);
    mod->setSavedValue(kKeyMode, std::string(modeId(cfg.mode)));
    mod->setSavedValue(kKeyRamp, cfg.rampSeconds);
    mod->setSavedValue(kKeyCurve, std::string(curveId(cfg.curve)));
    mod->setSavedValue(kKeyStrength, cfg.curveStrength);
    mod->setSavedValue(kKeyDuck, cfg.initialDuckDb);
    mod->setSavedValue(kKeyMaxCut, cfg.maxCutDb);
    mod->setSavedValue(kKeyMaxBoost, cfg.maxBoostDb);
    mod->setSavedValue(kKeyThreshold, cfg.thresholdDb);
    mod->setSavedValue(kKeyTarget, cfg.targetLufs);
    mod->setSavedValue(kKeyAnalysis, cfg.analysisSeconds);
    mod->setSavedValue(kKeySmoothing, cfg.smoothingSeconds);
    mod->setSavedValue(kKeyInMenus, cfg.inMenus);
    mod->setSavedValue(kKeyInGameplay, cfg.inGameplay);
    mod->setSavedValue(kKeyInEditor, cfg.inEditor);
    mod->setSavedValue(kKeyReduckSame, cfg.reduckSameSong);

    bool const wasEnabled = m_cfg.enabled;
    m_cfg = cfg;
    m_loaded = true;

    if (!cfg.enabled && wasEnabled) {
        resetRuntimeState();
        detachDsps();
    }
}

void DynamicVolumeManager::init() {
    if (!m_loaded) loadConfig();
    m_shuttingDown = false;
}

void DynamicVolumeManager::shutdown() {
    m_shuttingDown = true;
    applyGainDb(0.0f);
    detachDsps();
    resetRuntimeState();
}

void DynamicVolumeManager::setPerformancePaused(bool paused) {
    if (m_performancePaused == paused) return;
    m_performancePaused = paused;

    if (paused) {
        setSafeDropActive(false);
        applyGainDb(0.0f);
        detachDsps();
        m_floorDb = 0.0f;
        m_targetDb = 0.0f;
        m_appliedDb = 0.0f;
    }
}

void DynamicVolumeManager::resetRuntimeState() {
    m_songKey.clear();
    m_lastSound         = nullptr;
    m_songClock         = 0.0f;
    m_pollClock         = 0.0f;
    m_suppressPollUntil = 0.0f;
    m_analyzing     = false;
    m_hasSong       = false;
    m_measuredLufs  = kInvalidLufs;
    m_analysisPeak  = kInvalidLufs;
    m_referenceLufs = kInvalidLufs;
    m_settledLufs   = kInvalidLufs;
    m_floorDb       = 0.0f;
    m_targetDb      = 0.0f;
    m_appliedDb     = 0.0f;
    setSafeDropActive(false);
    applyGainDb(0.0f);
}


bool DynamicVolumeManager::ensureDsps() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return false;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return false;

    auto* group = engine->m_backgroundMusicChannel;

    // Re-attach if an audio-device reset replaced the channel group.
    if (m_attachedGroup && m_attachedGroup != group) {
        detachDsps();
    }
    if (m_attachedGroup == group && m_gainDsp && m_meterDsp && m_limiterDsp) {
        ensureOutputOrder();
        return true;
    }

    if (!m_gainDsp) {
        if (engine->m_system->createDSPByType(FMOD_DSP_TYPE_FADER, &m_gainDsp) != FMOD_OK) {
            m_gainDsp = nullptr;
            log::warn("[DynamicVolume] FADER DSP unavailable, feature stays idle");
            return false;
        }
    }
    if (!m_meterDsp) {
        if (engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOUDNESS_METER, &m_meterDsp) != FMOD_OK) {
            m_meterDsp = nullptr;
            log::warn("[DynamicVolume] LOUDNESS_METER DSP unavailable, feature stays idle");
            return false;
        }
    }
    if (!m_limiterDsp) {
        if (engine->m_system->createDSPByType(FMOD_DSP_TYPE_LIMITER, &m_limiterDsp) != FMOD_OK) {
            m_limiterDsp = nullptr;
            log::warn("[DynamicVolume] LIMITER DSP unavailable, feature stays idle");
            return false;
        }
    }

    m_attachedGroup = group;

    // Keep Safe Drop's limiter after meter and gain so it caps the final signal.
    if (group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, m_meterDsp) != FMOD_OK
        || group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, m_gainDsp) != FMOD_OK
        || group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, m_limiterDsp) != FMOD_OK) {
        log::warn("[DynamicVolume] could not attach the output DSP chain");
        detachDsps();
        return false;
    }

    m_meterDsp->setActive(true);
    m_meterDsp->setBypass(false);
    m_meterDsp->setMeteringEnabled(false, true);
    m_meterDsp->setParameterInt(FMOD_DSP_LOUDNESS_METER_STATE,
                                FMOD_DSP_LOUDNESS_METER_STATE_ANALYZING);

    m_gainDsp->setActive(true);
    m_gainDsp->setBypass(false);
    m_gainDsp->setParameterFloat(FMOD_DSP_FADER_GAIN, 0.0f);

    m_limiterDsp->setActive(true);
    m_limiterDsp->setParameterFloat(FMOD_DSP_LIMITER_RELEASETIME, kLimiterReleaseMs);
    m_limiterDsp->setParameterFloat(FMOD_DSP_LIMITER_CEILING, kLimiterCeilingDb);
    m_limiterDsp->setParameterFloat(FMOD_DSP_LIMITER_MAXIMIZERGAIN, 0.0f);
    m_limiterDsp->setParameterBool(FMOD_DSP_LIMITER_MODE, true);
    m_limiterDsp->setBypass(true);

    m_appliedDb = 0.0f;
    m_safeDropActive = false;
    log::info("[DynamicVolume] DSPs attached to the background music channel");
    return true;
}

void DynamicVolumeManager::ensureOutputOrder() {
    if (!m_attachedGroup || !m_gainDsp || !m_meterDsp || !m_limiterDsp) return;

    int limiterIndex = -1;
    int gainIndex = -1;
    int meterIndex = -1;
    if (m_attachedGroup->getDSPIndex(m_limiterDsp, &limiterIndex) != FMOD_OK
        || m_attachedGroup->getDSPIndex(m_gainDsp, &gainIndex) != FMOD_OK
        || m_attachedGroup->getDSPIndex(m_meterDsp, &meterIndex) != FMOD_OK
        || (limiterIndex == 0 && gainIndex == 1 && meterIndex == 2)) {
        return;
    }

    // Restore our chain order if another feature inserted a DSP.
    m_attachedGroup->setDSPIndex(m_meterDsp, FMOD_CHANNELCONTROL_DSP_HEAD);
    m_attachedGroup->setDSPIndex(m_gainDsp, FMOD_CHANNELCONTROL_DSP_HEAD);
    m_attachedGroup->setDSPIndex(m_limiterDsp, FMOD_CHANNELCONTROL_DSP_HEAD);
}

void DynamicVolumeManager::setSafeDropActive(bool active) {
    if (m_safeDropActive == active) return;
    m_safeDropActive = active;
    if (m_limiterDsp) m_limiterDsp->setBypass(!active);
    if (!active) m_safeDrop.reset();
}

void DynamicVolumeManager::detachDsps() {
    if (m_attachedGroup) {
        if (m_gainDsp)  m_attachedGroup->removeDSP(m_gainDsp);
        if (m_meterDsp) m_attachedGroup->removeDSP(m_meterDsp);
        if (m_limiterDsp) m_attachedGroup->removeDSP(m_limiterDsp);
        m_attachedGroup = nullptr;
    }
    if (m_gainDsp) {
        m_gainDsp->release();
        m_gainDsp = nullptr;
    }
    if (m_meterDsp) {
        m_meterDsp->release();
        m_meterDsp = nullptr;
    }
    if (m_limiterDsp) {
        m_limiterDsp->release();
        m_limiterDsp = nullptr;
    }
    m_safeDropActive = false;
    m_safeDrop.reset();
}

void DynamicVolumeManager::resetMeter() {
    if (!m_meterDsp) return;
    m_meterDsp->setParameterInt(FMOD_DSP_LOUDNESS_METER_STATE,
                                FMOD_DSP_LOUDNESS_METER_STATE_RESET_ALL);
    m_meterDsp->setParameterInt(FMOD_DSP_LOUDNESS_METER_STATE,
                                FMOD_DSP_LOUDNESS_METER_STATE_ANALYZING);
}

float DynamicVolumeManager::readGroupGainDb() const {
    if (!m_attachedGroup) return 0.0f;

    float volume = 1.0f;
    if (m_attachedGroup->getVolume(&volume) != FMOD_OK
        || !std::isfinite(volume) || volume <= 0.0001f) {
        return 0.0f;
    }
    return 20.0f * std::log10(volume);
}

float DynamicVolumeManager::readMomentaryLufs() const {
    if (!m_meterDsp) return kInvalidLufs;
    FMOD_DSP_LOUDNESS_METER_INFO_TYPE* info = nullptr;
    if (m_meterDsp->getParameterData(FMOD_DSP_LOUDNESS_METER_INFO,
                                     reinterpret_cast<void**>(&info), nullptr, nullptr, 0) != FMOD_OK) {
        return kInvalidLufs;
    }
    if (!info) return kInvalidLufs;
    return isValidLufs(info->momentaryloudness)
        ? info->momentaryloudness - readGroupGainDb() : kInvalidLufs;
}

float DynamicVolumeManager::readWindowPeakLufs() const {
    if (!m_meterDsp) return kInvalidLufs;
    FMOD_DSP_LOUDNESS_METER_INFO_TYPE* info = nullptr;
    if (m_meterDsp->getParameterData(FMOD_DSP_LOUDNESS_METER_INFO,
                                     reinterpret_cast<void**>(&info), nullptr, nullptr, 0) != FMOD_OK) {
        return kInvalidLufs;
    }
    if (!info) return kInvalidLufs;
    // Peak momentary loudness survives fade-ins; averages would under-duck drops.
    return isValidLufs(info->maxmomentaryloudness)
        ? info->maxmomentaryloudness - readGroupGainDb() : kInvalidLufs;
}

SafeDropLevels DynamicVolumeManager::readOutputLevels() const {
    SafeDropLevels levels;
    if (!m_meterDsp) return levels;

    FMOD_DSP_METERING_INFO output{};
    if (m_meterDsp->getMeteringInfo(nullptr, &output) != FMOD_OK) return levels;

    float rms = 0.0f;
    float peak = 0.0f;
    for (short i = 0; i < output.numchannels; ++i) {
        rms = std::max(rms, output.rmslevel[i]);
        peak = std::max(peak, output.peaklevel[i]);
    }

    float const groupGainDb = readGroupGainDb();
    auto toDb = [groupGainDb](float value) {
        if (!std::isfinite(value) || value <= 0.0f) return kInvalidLufs;
        float const db = 20.0f * std::log10(value) - groupGainDb;
        return isValidLufs(db) ? db : kInvalidLufs;
    };

    levels.rmsDb = toDb(rms);
    levels.peakDb = toDb(peak);
    levels.lufs = readMomentaryLufs();
    return levels;
}

void DynamicVolumeManager::applyGainDb(float db) {
    if (!m_gainDsp) return;
    m_gainDsp->setParameterFloat(FMOD_DSP_FADER_GAIN,
                                 std::clamp(db, kMinGainDb, kMaxGainDb));
}

bool DynamicVolumeManager::contextAllowed() const {
    if (PlayLayer::get()) return m_cfg.inGameplay;

    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (scene && scene->getChildByType<LevelEditorLayer>(0)) return m_cfg.inEditor;

    return m_cfg.inMenus;
}

bool DynamicVolumeManager::pollForSongChange() {
    if (!m_attachedGroup) return false;

    int channels = 0;
    if (m_attachedGroup->getNumChannels(&channels) != FMOD_OK || channels <= 0) {
        m_lastSound = nullptr;
        return false;
    }

    FMOD::Channel* channel = nullptr;
    if (m_attachedGroup->getChannel(0, &channel) != FMOD_OK || !channel) return false;

    bool playing = false;
    if (channel->isPlaying(&playing) != FMOD_OK || !playing) return false;

    FMOD::Sound* sound = nullptr;
    if (channel->getCurrentSound(&sound) != FMOD_OK || !sound) return false;

    if (sound == m_lastSound) return false;

    m_lastSound = sound;

    // Ignore the poll echo of a recent playMusic notification.
    if (m_pollClock < m_suppressPollUntil) return false;

    notifySongChanged({});
    return true;
}

void DynamicVolumeManager::notifySongChanged(std::string const& songKey) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
    if (!m_cfg.enabled || !moduleOn()) return;

    // Give the channel group time to catch up before polling again.
    m_suppressPollUntil = m_pollClock + 0.35f;

    bool const sameSong = m_hasSong && !songKey.empty() && songKey == m_songKey;
    if (sameSong && !m_cfg.reduckSameSong) {
        // A restart keeps the gain already reached by the ramp.
        m_songKey = songKey;
        return;
    }

    // The outgoing settled level becomes the new reference.
    if (isValidLufs(m_settledLufs)) {
        m_referenceLufs = m_settledLufs;
    }

    m_songKey      = songKey;
    m_songClock    = 0.0f;
    m_analyzing    = true;
    m_hasSong      = true;
    m_measuredLufs = kInvalidLufs;
    m_analysisPeak = kInvalidLufs;
    m_settledLufs  = kInvalidLufs;

    // Duck immediately while the meter warms up; without a reference, start at unity.
    bool const haveReference = m_cfg.mode == Mode::Fixed || isValidLufs(m_referenceLufs);
    m_floorDb = haveReference
        ? std::clamp(m_cfg.initialDuckDb, -std::abs(m_cfg.maxCutDb), 0.0f)
        : 0.0f;

    resetMeter();
}

void DynamicVolumeManager::update(float dt) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
    if (m_performancePaused) return;
    if (!m_loaded) loadConfig();

    // Sample the module registry twice per second; a per-frame lookup is expensive.
    if (m_moduleCheckCooldown-- <= 0) {
        m_moduleCheckCooldown = 30;
        m_moduleOnCached = moduleOn();
    }
    bool const on = m_cfg.enabled && m_moduleOnCached;
    if (!on) {
        if (m_attachedGroup) {
            applyGainDb(0.0f);
            detachDsps();
            resetRuntimeState();
        }
        return;
    }

    if (!ensureDsps()) return;

    // Advance before guards so the playMusic suppression window always expires.
    m_pollClock += dt;

    auto* engine = FMODAudioEngine::sharedEngine();
    float const musicVolume = engine ? engine->m_musicVolume : 0.0f;

    // Disabled contexts glide to unity and stop measuring.
    if (musicVolume <= 0.0f || !contextAllowed()) {
        m_targetDb = 0.0f;
        m_appliedDb = approach(m_appliedDb, m_targetDb, dt, m_cfg.smoothingSeconds);
        setSafeDropActive(false);
        applyGainDb(m_appliedDb);
        return;
    }

    setSafeDropActive(safeDropOn());

    pollForSongChange();

    auto trackPeak = [this, dt](float& slot) {
        float const now = readMomentaryLufs();
        if (!isValidLufs(now)) return;
        slot = isValidLufs(slot) ? std::max(now, slot - kPeakDecayDbPerSec * dt) : now;
    };

    if (!m_hasSong) {
        // Seed the first reference from the already-playing track.
        trackPeak(m_settledLufs);
        m_targetDb = 0.0f;
    } else {
        m_songClock += dt;

        if (m_analyzing) {
            float const now = readMomentaryLufs();
            if (isValidLufs(now)) {
                m_analysisPeak = isValidLufs(m_analysisPeak) ? std::max(m_analysisPeak, now) : now;
            }

            if (m_songClock >= m_cfg.analysisSeconds) {
                float const locked = readWindowPeakLufs();
                m_measuredLufs = isValidLufs(locked) ? locked : m_analysisPeak;
                m_settledLufs  = m_measuredLufs;
                m_analyzing    = false;
            } else if (isValidLufs(m_analysisPeak)) {
                // Converge on the measured floor instead of holding the blind duck.
                m_measuredLufs = m_analysisPeak;
            }
        } else {
            // Follow the song so the next track matches its settled loudness.
            trackPeak(m_settledLufs);
            // Fixed keeps correcting; ramping modes lock the floor so the climb ends.
            if (m_cfg.mode == Mode::Fixed && isValidLufs(m_settledLufs)) {
                m_measuredLufs = m_settledLufs;
            }
        }

        // Fixed always uses its target; ramping modes leave the first track unchanged.
        bool const fixed = m_cfg.mode == Mode::Fixed;
        float const reference = fixed ? m_cfg.targetLufs : m_referenceLufs;
        bool const canMatch = (fixed || isValidLufs(m_referenceLufs))
                           && isValidLufs(m_measuredLufs);

        float const matched = canMatch ? matchGainDb(m_cfg, m_measuredLufs, reference) : 0.0f;
        // Ease the floor into place during analysis.
        float const floorRate = m_analyzing ? std::min(1.0f, dt * 4.0f) : std::min(1.0f, dt * 8.0f);
        m_floorDb += (matched - m_floorDb) * floorRate;

        m_targetDb = rampGainDb(m_cfg, m_floorDb, m_songClock);
    }

    bool safeDropCut = false;
    if (m_safeDropActive) {
        float const safeDropDb = m_safeDrop.update(readOutputLevels(), m_cfg, dt);
        safeDropCut = safeDropDb < m_targetDb;
        if (safeDropCut) m_targetDb = safeDropDb;
    }

    float const gainSeconds = m_targetDb < m_appliedDb
        ? (safeDropCut ? kSafeDropAttackSeconds : kRampGainAttackSeconds)
        : m_cfg.smoothingSeconds;
    m_appliedDb = approach(m_appliedDb, m_targetDb, dt, gainSeconds);
    applyGainDb(m_appliedDb);
}

LiveState DynamicVolumeManager::liveState() const {
    LiveState st;
    st.active        = m_attachedGroup != nullptr && m_cfg.enabled && moduleOn();
    st.songLufs      = isValidLufs(m_measuredLufs) ? m_measuredLufs : m_settledLufs;
    st.referenceDb   = (m_cfg.mode == Mode::Fixed) ? m_cfg.targetLufs : m_referenceLufs;
    st.appliedGainDb = m_appliedDb;
    st.floorDb       = m_floorDb;
    st.analyzing     = m_analyzing;
    st.safeDropActive = m_safeDropActive;
    st.safeDropGainDb = m_safeDrop.gainDb();
    st.rampProgress  = (m_cfg.mode == Mode::Fixed || !m_hasSong)
        ? 1.0f
        : curveProgress(m_cfg.curve,
                        std::clamp(m_songClock / std::max(0.01f, m_cfg.rampSeconds), 0.0f, 1.0f),
                        m_cfg.curveStrength);
    return st;
}

}
