#include "MenuMusicEffects.hpp"

#include "MenuMusicPlayer.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace geode::prelude;

namespace paimon::menumusic {

namespace {

constexpr char const* kKeyEnabled = "module-musiceffects";
constexpr char const* kKeyPreset = "menu-music-effects-preset";
constexpr char const* kKeySpeed = "menu-music-effects-speed";
constexpr char const* kKeyGain = "menu-music-effects-gain-db";
constexpr char const* kKeyPan = "menu-music-effects-pan";
constexpr char const* kKeyBass = "menu-music-effects-bass-db";
constexpr char const* kKeyMid = "menu-music-effects-mid-db";
constexpr char const* kKeyTreble = "menu-music-effects-treble-db";
constexpr char const* kKeyLowpass = "menu-music-effects-lowpass-hz";
constexpr char const* kKeyHighpass = "menu-music-effects-highpass-hz";
constexpr char const* kKeyReverbMix = "menu-music-effects-reverb-mix";
constexpr char const* kKeyReverbDecay = "menu-music-effects-reverb-decay";
constexpr char const* kKeyReverbRoom = "menu-music-effects-reverb-room";
constexpr char const* kKeyReverbHighCut = "menu-music-effects-reverb-highcut";
constexpr char const* kKeyEchoMix = "menu-music-effects-echo-mix";
constexpr char const* kKeyEchoDelay = "menu-music-effects-echo-delay";
constexpr char const* kKeyEchoFeedback = "menu-music-effects-echo-feedback";
constexpr char const* kKeySpatialEnabled = "menu-music-spatial-enabled";
constexpr char const* kKeySpatialPreset = "menu-music-spatial-preset";
constexpr char const* kKeySpatialMotion = "menu-music-spatial-motion";
constexpr char const* kKeySpatialWidth = "menu-music-spatial-width";
constexpr char const* kKeySpatialAngle = "menu-music-spatial-angle";
constexpr char const* kKeySpatialRoom = "menu-music-spatial-room";
constexpr char const* kKeySpatialMotionSpeed = "menu-music-spatial-motion-speed";

bool moduleOn() {
    return paimon::modules::isEnabled("paimbnails.musiceffects.menu");
}

bool spatialModuleOn() {
    return paimon::modules::isEnabled("paimbnails.spatialaudio.menu");
}

float wetLevelDb(float percent) {
    if (percent <= 0.01f) return -80.f;
    return std::clamp(20.f * std::log10(percent / 100.f), -80.f, 0.f);
}

void normalize(MusicEffectsConfig& cfg) {
    cfg.speed = std::clamp(cfg.speed, 0.5f, 1.5f);
    cfg.gainDb = std::clamp(cfg.gainDb, -12.f, 6.f);
    cfg.pan = std::clamp(cfg.pan, -1.f, 1.f);
    cfg.bassDb = std::clamp(cfg.bassDb, -12.f, 10.f);
    cfg.midDb = std::clamp(cfg.midDb, -12.f, 10.f);
    cfg.trebleDb = std::clamp(cfg.trebleDb, -12.f, 10.f);
    cfg.lowpassHz = std::clamp(cfg.lowpassHz, 500.f, 22000.f);
    cfg.highpassHz = std::clamp(cfg.highpassHz, 20.f, 2500.f);
    cfg.reverbMix = std::clamp(cfg.reverbMix, 0.f, 100.f);
    cfg.reverbDecay = std::clamp(cfg.reverbDecay, 0.1f, 10.f);
    cfg.reverbRoom = std::clamp(cfg.reverbRoom, 10.f, 100.f);
    cfg.reverbHighCut = std::clamp(cfg.reverbHighCut, 1000.f, 20000.f);
    cfg.echoMix = std::clamp(cfg.echoMix, 0.f, 100.f);
    cfg.echoDelay = std::clamp(cfg.echoDelay, 10.f, 1500.f);
    cfg.echoFeedback = std::clamp(cfg.echoFeedback, 0.f, 90.f);
    cfg.spatialWidth = std::clamp(cfg.spatialWidth, 0.f, 360.f);
    cfg.spatialAngle = std::clamp(cfg.spatialAngle, -180.f, 180.f);
    cfg.spatialRoom = std::clamp(cfg.spatialRoom, 0.f, 100.f);
    cfg.spatialMotionSpeed = std::clamp(cfg.spatialMotionSpeed, 1.f, 60.f);
}

} // namespace

char const* musicEffectsPresetId(MusicEffectsPreset preset) {
    switch (preset) {
        case MusicEffectsPreset::Custom:      return "custom";
        case MusicEffectsPreset::Original:    return "original";
        case MusicEffectsPreset::SlowReverb:  return "slow-reverb";
        case MusicEffectsPreset::Dreamy:      return "dreamy";
        case MusicEffectsPreset::BassBoost:   return "bass-boost";
        case MusicEffectsPreset::Nightcore:   return "nightcore";
        case MusicEffectsPreset::Underwater:  return "underwater";
        case MusicEffectsPreset::ConcertHall: return "concert-hall";
        case MusicEffectsPreset::Lofi:        return "lofi";
        case MusicEffectsPreset::Count:       break;
    }
    return "custom";
}

MusicEffectsPreset musicEffectsPresetFromId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(MusicEffectsPreset::Count); ++i) {
        auto preset = static_cast<MusicEffectsPreset>(i);
        if (id == musicEffectsPresetId(preset)) return preset;
    }
    return MusicEffectsPreset::Custom;
}

MusicEffectsConfig musicEffectsPresetConfig(MusicEffectsPreset preset) {
    MusicEffectsConfig cfg;
    cfg.preset = preset;
    cfg.enabled = preset != MusicEffectsPreset::Original;

    switch (preset) {
        case MusicEffectsPreset::SlowReverb:
            cfg.speed = 0.78f;
            cfg.gainDb = -1.f;
            cfg.bassDb = 2.f;
            cfg.lowpassHz = 15000.f;
            cfg.reverbMix = 45.f;
            cfg.reverbDecay = 4.5f;
            cfg.reverbRoom = 75.f;
            break;
        case MusicEffectsPreset::Dreamy:
            cfg.speed = 0.9f;
            cfg.lowpassHz = 17000.f;
            cfg.reverbMix = 55.f;
            cfg.reverbDecay = 5.5f;
            cfg.reverbRoom = 85.f;
            cfg.echoMix = 18.f;
            cfg.echoDelay = 360.f;
            cfg.echoFeedback = 32.f;
            break;
        case MusicEffectsPreset::BassBoost:
            cfg.gainDb = -1.5f;
            cfg.bassDb = 7.f;
            cfg.midDb = -1.f;
            break;
        case MusicEffectsPreset::Nightcore:
            cfg.speed = 1.22f;
            cfg.bassDb = -1.f;
            cfg.trebleDb = 3.f;
            cfg.reverbMix = 8.f;
            break;
        case MusicEffectsPreset::Underwater:
            cfg.speed = 0.86f;
            cfg.bassDb = 4.f;
            cfg.lowpassHz = 1200.f;
            cfg.reverbMix = 18.f;
            cfg.reverbDecay = 2.8f;
            break;
        case MusicEffectsPreset::ConcertHall:
            cfg.gainDb = -1.f;
            cfg.reverbMix = 65.f;
            cfg.reverbDecay = 6.5f;
            cfg.reverbRoom = 95.f;
            cfg.echoMix = 10.f;
            cfg.echoDelay = 240.f;
            cfg.echoFeedback = 20.f;
            break;
        case MusicEffectsPreset::Lofi:
            cfg.speed = 0.88f;
            cfg.bassDb = 2.f;
            cfg.trebleDb = -5.f;
            cfg.lowpassHz = 7500.f;
            cfg.highpassHz = 80.f;
            cfg.reverbMix = 15.f;
            break;
        case MusicEffectsPreset::Custom:
            cfg.enabled = true;
            break;
        case MusicEffectsPreset::Original:
        case MusicEffectsPreset::Count:
            break;
    }
    return cfg;
}

char const* spatialPresetId(SpatialPreset preset) {
    switch (preset) {
        case SpatialPreset::Custom:    return "custom";
        case SpatialPreset::Off:       return "off";
        case SpatialPreset::Studio:    return "studio";
        case SpatialPreset::Cinema:    return "cinema";
        case SpatialPreset::Arena:     return "arena";
        case SpatialPreset::Orbit:     return "orbit";
        case SpatialPreset::Dreamwave: return "dreamwave";
        case SpatialPreset::Count:     break;
    }
    return "custom";
}

SpatialPreset spatialPresetFromId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(SpatialPreset::Count); ++i) {
        auto preset = static_cast<SpatialPreset>(i);
        if (id == spatialPresetId(preset)) return preset;
    }
    return SpatialPreset::Custom;
}

char const* spatialMotionId(SpatialMotion motion) {
    switch (motion) {
        case SpatialMotion::Static: return "static";
        case SpatialMotion::Orbit:  return "orbit";
        case SpatialMotion::Sway:   return "sway";
        case SpatialMotion::Count:  break;
    }
    return "static";
}

SpatialMotion spatialMotionFromId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(SpatialMotion::Count); ++i) {
        auto motion = static_cast<SpatialMotion>(i);
        if (id == spatialMotionId(motion)) return motion;
    }
    return SpatialMotion::Static;
}

MenuMusicEffects& MenuMusicEffects::get() {
    static MenuMusicEffects instance;
    return instance;
}

void MenuMusicEffects::loadConfig() {
    auto* mod = Mod::get();
    MusicEffectsConfig cfg;

    cfg.enabled = mod->getSavedValue<bool>(kKeyEnabled, cfg.enabled);
    cfg.preset = musicEffectsPresetFromId(
        mod->getSavedValue<std::string>(kKeyPreset, musicEffectsPresetId(cfg.preset)));
    cfg.speed = mod->getSavedValue<float>(kKeySpeed, cfg.speed);
    cfg.gainDb = mod->getSavedValue<float>(kKeyGain, cfg.gainDb);
    cfg.pan = mod->getSavedValue<float>(kKeyPan, cfg.pan);
    cfg.bassDb = mod->getSavedValue<float>(kKeyBass, cfg.bassDb);
    cfg.midDb = mod->getSavedValue<float>(kKeyMid, cfg.midDb);
    cfg.trebleDb = mod->getSavedValue<float>(kKeyTreble, cfg.trebleDb);
    cfg.lowpassHz = mod->getSavedValue<float>(kKeyLowpass, cfg.lowpassHz);
    cfg.highpassHz = mod->getSavedValue<float>(kKeyHighpass, cfg.highpassHz);
    cfg.reverbMix = mod->getSavedValue<float>(kKeyReverbMix, cfg.reverbMix);
    cfg.reverbDecay = mod->getSavedValue<float>(kKeyReverbDecay, cfg.reverbDecay);
    cfg.reverbRoom = mod->getSavedValue<float>(kKeyReverbRoom, cfg.reverbRoom);
    cfg.reverbHighCut = mod->getSavedValue<float>(kKeyReverbHighCut, cfg.reverbHighCut);
    cfg.echoMix = mod->getSavedValue<float>(kKeyEchoMix, cfg.echoMix);
    cfg.echoDelay = mod->getSavedValue<float>(kKeyEchoDelay, cfg.echoDelay);
    cfg.echoFeedback = mod->getSavedValue<float>(kKeyEchoFeedback, cfg.echoFeedback);
    cfg.spatialEnabled = mod->getSavedValue<bool>(kKeySpatialEnabled, cfg.spatialEnabled);
    cfg.spatialPreset = spatialPresetFromId(
        mod->getSavedValue<std::string>(kKeySpatialPreset, spatialPresetId(cfg.spatialPreset)));
    cfg.spatialMotion = spatialMotionFromId(
        mod->getSavedValue<std::string>(kKeySpatialMotion, spatialMotionId(cfg.spatialMotion)));
    cfg.spatialWidth = mod->getSavedValue<float>(kKeySpatialWidth, cfg.spatialWidth);
    cfg.spatialAngle = mod->getSavedValue<float>(kKeySpatialAngle, cfg.spatialAngle);
    cfg.spatialRoom = mod->getSavedValue<float>(kKeySpatialRoom, cfg.spatialRoom);
    cfg.spatialMotionSpeed = mod->getSavedValue<float>(
        kKeySpatialMotionSpeed, cfg.spatialMotionSpeed);

    normalize(cfg);
    m_cfg = cfg;
    m_loaded = true;
    m_shuttingDown = false;
}

void MenuMusicEffects::saveConfig(MusicEffectsConfig config) {
    normalize(config);

    auto* mod = Mod::get();
    mod->setSavedValue(kKeyEnabled, config.enabled);
    mod->setSavedValue(kKeyPreset, std::string(musicEffectsPresetId(config.preset)));
    mod->setSavedValue(kKeySpeed, config.speed);
    mod->setSavedValue(kKeyGain, config.gainDb);
    mod->setSavedValue(kKeyPan, config.pan);
    mod->setSavedValue(kKeyBass, config.bassDb);
    mod->setSavedValue(kKeyMid, config.midDb);
    mod->setSavedValue(kKeyTreble, config.trebleDb);
    mod->setSavedValue(kKeyLowpass, config.lowpassHz);
    mod->setSavedValue(kKeyHighpass, config.highpassHz);
    mod->setSavedValue(kKeyReverbMix, config.reverbMix);
    mod->setSavedValue(kKeyReverbDecay, config.reverbDecay);
    mod->setSavedValue(kKeyReverbRoom, config.reverbRoom);
    mod->setSavedValue(kKeyReverbHighCut, config.reverbHighCut);
    mod->setSavedValue(kKeyEchoMix, config.echoMix);
    mod->setSavedValue(kKeyEchoDelay, config.echoDelay);
    mod->setSavedValue(kKeyEchoFeedback, config.echoFeedback);
    mod->setSavedValue(kKeySpatialEnabled, config.spatialEnabled);
    mod->setSavedValue(kKeySpatialPreset, std::string(spatialPresetId(config.spatialPreset)));
    mod->setSavedValue(kKeySpatialMotion, std::string(spatialMotionId(config.spatialMotion)));
    mod->setSavedValue(kKeySpatialWidth, config.spatialWidth);
    mod->setSavedValue(kKeySpatialAngle, config.spatialAngle);
    mod->setSavedValue(kKeySpatialRoom, config.spatialRoom);
    mod->setSavedValue(kKeySpatialMotionSpeed, config.spatialMotionSpeed);

    m_cfg = config;
    m_loaded = true;
    update();
}

void MenuMusicEffects::applyPreset(MusicEffectsPreset preset) {
    if (preset == MusicEffectsPreset::Custom) {
        auto cfg = m_cfg;
        cfg.preset = preset;
        saveConfig(cfg);
        return;
    }

    auto cfg = musicEffectsPresetConfig(preset);
    if (preset != MusicEffectsPreset::Original) {
        cfg.spatialEnabled = m_cfg.spatialEnabled;
        cfg.spatialPreset = m_cfg.spatialPreset;
        cfg.spatialMotion = m_cfg.spatialMotion;
        cfg.spatialWidth = m_cfg.spatialWidth;
        cfg.spatialAngle = m_cfg.spatialAngle;
        cfg.spatialRoom = m_cfg.spatialRoom;
        cfg.spatialMotionSpeed = m_cfg.spatialMotionSpeed;
    }
    saveConfig(cfg);
}

void MenuMusicEffects::applySpatialPreset(SpatialPreset preset) {
    auto cfg = m_cfg;
    cfg.spatialPreset = preset;

    switch (preset) {
        case SpatialPreset::Off:
            cfg.spatialEnabled = false;
            cfg.spatialMotion = SpatialMotion::Static;
            cfg.spatialRoom = 0.f;
            break;
        case SpatialPreset::Studio:
            cfg.spatialEnabled = true;
            cfg.spatialMotion = SpatialMotion::Static;
            cfg.spatialWidth = 75.f;
            cfg.spatialAngle = 0.f;
            cfg.spatialRoom = 8.f;
            break;
        case SpatialPreset::Cinema:
            cfg.spatialEnabled = true;
            cfg.spatialMotion = SpatialMotion::Static;
            cfg.spatialWidth = 160.f;
            cfg.spatialAngle = 0.f;
            cfg.spatialRoom = 24.f;
            break;
        case SpatialPreset::Arena:
            cfg.spatialEnabled = true;
            cfg.spatialMotion = SpatialMotion::Static;
            cfg.spatialWidth = 280.f;
            cfg.spatialAngle = 0.f;
            cfg.spatialRoom = 40.f;
            break;
        case SpatialPreset::Orbit:
            cfg.spatialEnabled = true;
            cfg.spatialMotion = SpatialMotion::Orbit;
            cfg.spatialWidth = 90.f;
            cfg.spatialAngle = 0.f;
            cfg.spatialRoom = 18.f;
            cfg.spatialMotionSpeed = 18.f;
            break;
        case SpatialPreset::Dreamwave:
            cfg.spatialEnabled = true;
            cfg.spatialMotion = SpatialMotion::Sway;
            cfg.spatialWidth = 220.f;
            cfg.spatialAngle = 0.f;
            cfg.spatialRoom = 32.f;
            cfg.spatialMotionSpeed = 10.f;
            break;
        case SpatialPreset::Custom:
            cfg.spatialEnabled = true;
            break;
        case SpatialPreset::Count:
            return;
    }

    if (cfg.spatialEnabled) cfg.enabled = true;
    m_spatialPhase = 0.f;
    saveConfig(cfg);
}

void MenuMusicEffects::activateForCurrentTrack() {
    m_menuTrackActive = MenuMusicPlayer::get().isManagingPlayback();
    update();
}

void MenuMusicEffects::onMusicStarted(std::string_view path) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

    auto const& state = MenuMusicPlayer::get().state();
    m_menuTrackActive = !state.currentAudioPath.empty()
        && path == state.currentAudioPath
        && MenuMusicPlayer::get().isManagingPlayback();

    if (m_menuTrackActive) update();
    else detachDsps();
}

bool MenuMusicEffects::ensureDsps() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return false;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return false;

    auto* group = engine->m_backgroundMusicChannel;
    if (m_attachedGroup && m_attachedGroup != group) detachDsps();
    if (m_attachedGroup == group && m_eqDsp && m_lowpassDsp && m_highpassDsp
        && m_reverbDsp && m_echoDsp && m_spatialPanDsp && m_spatialReverbDsp
        && m_gainDsp && m_meterDsp) {
        return true;
    }

    auto create = [&](FMOD_DSP_TYPE type, FMOD::DSP*& out) {
        return out || engine->m_system->createDSPByType(type, &out) == FMOD_OK;
    };
    if (!create(FMOD_DSP_TYPE_THREE_EQ, m_eqDsp)
        || !create(FMOD_DSP_TYPE_LOWPASS, m_lowpassDsp)
        || !create(FMOD_DSP_TYPE_HIGHPASS, m_highpassDsp)
        || !create(FMOD_DSP_TYPE_PAN, m_spatialPanDsp)
        || !create(FMOD_DSP_TYPE_SFXREVERB, m_spatialReverbDsp)
        || !create(FMOD_DSP_TYPE_SFXREVERB, m_reverbDsp)
        || !create(FMOD_DSP_TYPE_ECHO, m_echoDsp)
        || !create(FMOD_DSP_TYPE_FADER, m_gainDsp)
        || !create(FMOD_DSP_TYPE_FADER, m_meterDsp)) {
        log::warn("[MenuMusicEffects] an FMOD effect is unavailable");
        detachDsps();
        return false;
    }

    m_attachedGroup = group;
    FMOD::DSP* chain[] = {
        m_eqDsp, m_lowpassDsp, m_highpassDsp,
        m_spatialPanDsp, m_spatialReverbDsp,
        m_reverbDsp, m_echoDsp, m_gainDsp,
    };
    for (auto* dsp : chain) {
        if (group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, dsp) != FMOD_OK) {
            log::warn("[MenuMusicEffects] could not attach the FMOD effect chain");
            detachDsps();
            return false;
        }
        dsp->setActive(true);
    }
    if (group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, m_meterDsp) != FMOD_OK) {
        log::warn("[MenuMusicEffects] could not attach the output meter");
        detachDsps();
        return false;
    }
    m_meterDsp->setActive(true);
    m_meterDsp->setBypass(false);
    m_meterDsp->setParameterFloat(FMOD_DSP_FADER_GAIN, 0.f);
    m_meterDsp->setMeteringEnabled(false, true);
    return true;
}

void MenuMusicEffects::applyConfig() {
    if (!m_attachedGroup) return;

    m_attachedGroup->setPitch(m_cfg.speed);
    m_attachedGroup->setPan(m_cfg.pan);

    m_eqDsp->setParameterFloat(FMOD_DSP_THREE_EQ_LOWGAIN, m_cfg.bassDb);
    m_eqDsp->setParameterFloat(FMOD_DSP_THREE_EQ_MIDGAIN, m_cfg.midDb);
    m_eqDsp->setParameterFloat(FMOD_DSP_THREE_EQ_HIGHGAIN, m_cfg.trebleDb);
    m_eqDsp->setBypass(std::abs(m_cfg.bassDb) < 0.01f
        && std::abs(m_cfg.midDb) < 0.01f && std::abs(m_cfg.trebleDb) < 0.01f);

    m_lowpassDsp->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, m_cfg.lowpassHz);
    m_lowpassDsp->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 1.f);
    m_lowpassDsp->setBypass(m_cfg.lowpassHz >= 21990.f);

    m_highpassDsp->setParameterFloat(FMOD_DSP_HIGHPASS_CUTOFF, m_cfg.highpassHz);
    m_highpassDsp->setParameterFloat(FMOD_DSP_HIGHPASS_RESONANCE, 1.f);
    m_highpassDsp->setBypass(m_cfg.highpassHz <= 20.01f);

    applySpatialConfig();

    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, m_cfg.reverbDecay * 1000.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DIFFUSION, m_cfg.reverbRoom);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DENSITY, m_cfg.reverbRoom);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_HIGHCUT, m_cfg.reverbHighCut);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, 0.f);
    m_reverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, wetLevelDb(m_cfg.reverbMix));
    m_reverbDsp->setBypass(m_cfg.reverbMix <= 0.01f);

    m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_DELAY, m_cfg.echoDelay);
    m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_FEEDBACK, m_cfg.echoFeedback);
    m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_DRYLEVEL, 0.f);
    m_echoDsp->setParameterFloat(FMOD_DSP_ECHO_WETLEVEL, wetLevelDb(m_cfg.echoMix));
    m_echoDsp->setBypass(m_cfg.echoMix <= 0.01f);

    m_gainDsp->setParameterFloat(FMOD_DSP_FADER_GAIN, m_cfg.gainDb);
    m_gainDsp->setBypass(std::abs(m_cfg.gainDb) < 0.01f);
}

void MenuMusicEffects::applySpatialConfig() {
    if (!m_spatialPanDsp || !m_spatialReverbDsp) return;

    bool const active = spatialModuleOn() && m_cfg.spatialEnabled;
    m_spatialPanDsp->setParameterInt(FMOD_DSP_PAN_MODE, FMOD_DSP_PAN_MODE_SURROUND);
    m_spatialPanDsp->setParameterInt(
        FMOD_DSP_PAN_2D_STEREO_MODE, FMOD_DSP_PAN_2D_STEREO_MODE_DISTRIBUTED);
    applySpatialDirection();
    m_spatialPanDsp->setParameterFloat(FMOD_DSP_PAN_2D_EXTENT, m_cfg.spatialWidth);
    m_spatialPanDsp->setBypass(!active);

    float const room = m_cfg.spatialRoom;
    m_spatialReverbDsp->setParameterFloat(
        FMOD_DSP_SFXREVERB_DECAYTIME, (0.7f + room * 0.035f) * 1000.f);
    m_spatialReverbDsp->setParameterFloat(
        FMOD_DSP_SFXREVERB_DIFFUSION, 45.f + room * 0.5f);
    m_spatialReverbDsp->setParameterFloat(
        FMOD_DSP_SFXREVERB_DENSITY, 45.f + room * 0.5f);
    m_spatialReverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_HIGHCUT, 16000.f);
    m_spatialReverbDsp->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, 0.f);
    m_spatialReverbDsp->setParameterFloat(
        FMOD_DSP_SFXREVERB_WETLEVEL, wetLevelDb(room * 0.45f));
    m_spatialReverbDsp->setBypass(!active || room <= 0.01f);
}

void MenuMusicEffects::applySpatialDirection() {
    if (!m_spatialPanDsp) return;
    m_spatialPanDsp->setParameterFloat(
        FMOD_DSP_PAN_2D_DIRECTION, currentSpatialAngle());
}

void MenuMusicEffects::bypassDsps() {
    if (!m_attachedGroup) return;

    m_attachedGroup->setPitch(1.f);
    m_attachedGroup->setPan(0.f);
    for (auto* dsp : {m_eqDsp, m_lowpassDsp, m_highpassDsp,
            m_spatialPanDsp, m_spatialReverbDsp, m_reverbDsp, m_echoDsp, m_gainDsp}) {
        if (dsp) dsp->setBypass(true);
    }
}

void MenuMusicEffects::update() {
    if (!m_loaded) loadConfig();
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

    m_cfg.enabled = Mod::get()->getSavedValue<bool>(kKeyEnabled, false);
    m_cfg.spatialEnabled = Mod::get()->getSavedValue<bool>(kKeySpatialEnabled, false);
    if (!m_cfg.enabled || !moduleOn() || !m_menuTrackActive
        || !MenuMusicPlayer::get().isManagingPlayback()) {
        detachDsps();
        return;
    }
    if (!ensureDsps()) return;
    if (m_auditionBypassed) bypassDsps();
    else applyConfig();
}

void MenuMusicEffects::tick(float dt) {
    if (!m_loaded) loadConfig();
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

    float const frameDt = std::max(dt, 0.f);
    m_modulePollTime += frameDt;
    if (m_modulePollTime >= 0.25f) {
        m_modulePollTime = std::fmod(m_modulePollTime, 0.25f);
        auto* mod = Mod::get();
        if (!mod) return;

        bool const enabled = mod->getSavedValue<bool>(kKeyEnabled, false);
        bool const spatialEnabled = mod->getSavedValue<bool>(kKeySpatialEnabled, false);
        if (m_cfg.enabled != enabled || m_cfg.spatialEnabled != spatialEnabled) {
            m_cfg.enabled = enabled;
            m_cfg.spatialEnabled = spatialEnabled;
            update();
        }

        bool const shouldRun = m_cfg.enabled && moduleOn() && m_menuTrackActive
            && MenuMusicPlayer::get().isManagingPlayback();
        if (!shouldRun) {
            if (m_attachedGroup) update();
        } else if (!m_auditionBypassed && !m_attachedGroup) {
            update();
        }
    }

    if (m_auditionBypassed || !m_attachedGroup || !m_cfg.enabled
        || !m_menuTrackActive || !spatialModuleOn() || !m_cfg.spatialEnabled
        || m_cfg.spatialMotion == SpatialMotion::Static) return;

    m_spatialPhase = std::fmod(
        m_spatialPhase + frameDt * m_cfg.spatialMotionSpeed, 360.f);
    applySpatialDirection();
}

void MenuMusicEffects::setAuditionBypassed(bool bypassed) {
    if (m_auditionBypassed == bypassed) return;
    m_auditionBypassed = bypassed;
    update();
}

float MenuMusicEffects::currentSpatialAngle() const {
    float angle = m_cfg.spatialAngle;
    if (m_cfg.spatialMotion == SpatialMotion::Orbit) {
        angle += m_spatialPhase;
    } else if (m_cfg.spatialMotion == SpatialMotion::Sway) {
        constexpr float kDegToRad = 0.017453292519943295f;
        angle += std::sin(m_spatialPhase * kDegToRad) * 70.f;
    }
    return std::remainder(angle, 360.f);
}

float MenuMusicEffects::outputPeak() const {
    if (!m_meterDsp || (!isSpatialActive() && !m_auditionBypassed)) return 0.f;

    FMOD_DSP_METERING_INFO output{};
    if (m_meterDsp->getMeteringInfo(nullptr, &output) != FMOD_OK) return 0.f;

    float peak = 0.f;
    for (short i = 0; i < output.numchannels; ++i) {
        peak = std::max(peak, output.peaklevel[i]);
    }
    return std::clamp(peak, 0.f, 1.f);
}

bool MenuMusicEffects::isSpatialActive() const {
    return !m_auditionBypassed && m_menuTrackActive && m_attachedGroup
        && m_cfg.enabled && moduleOn() && spatialModuleOn() && m_cfg.spatialEnabled;
}

void MenuMusicEffects::deactivate() {
    m_menuTrackActive = false;
    detachDsps();
}

void MenuMusicEffects::detachDsps() {
    if (m_attachedGroup) {
        m_attachedGroup->setPitch(1.f);
        m_attachedGroup->setPan(0.f);
        if (m_eqDsp) m_attachedGroup->removeDSP(m_eqDsp);
        if (m_lowpassDsp) m_attachedGroup->removeDSP(m_lowpassDsp);
        if (m_highpassDsp) m_attachedGroup->removeDSP(m_highpassDsp);
        if (m_spatialPanDsp) m_attachedGroup->removeDSP(m_spatialPanDsp);
        if (m_spatialReverbDsp) m_attachedGroup->removeDSP(m_spatialReverbDsp);
        if (m_reverbDsp) m_attachedGroup->removeDSP(m_reverbDsp);
        if (m_echoDsp) m_attachedGroup->removeDSP(m_echoDsp);
        if (m_gainDsp) m_attachedGroup->removeDSP(m_gainDsp);
        if (m_meterDsp) m_attachedGroup->removeDSP(m_meterDsp);
        m_attachedGroup = nullptr;
    }

    auto release = [](FMOD::DSP*& dsp) {
        if (!dsp) return;
        dsp->release();
        dsp = nullptr;
    };
    release(m_eqDsp);
    release(m_lowpassDsp);
    release(m_highpassDsp);
    release(m_spatialPanDsp);
    release(m_spatialReverbDsp);
    release(m_reverbDsp);
    release(m_echoDsp);
    release(m_gainDsp);
    release(m_meterDsp);
}

void MenuMusicEffects::shutdown() {
    m_shuttingDown = true;
    m_menuTrackActive = false;
    m_auditionBypassed = false;
    detachDsps();
}

} // namespace paimon::menumusic
