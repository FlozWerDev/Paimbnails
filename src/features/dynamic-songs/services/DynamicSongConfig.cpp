#include "DynamicSongConfig.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::dynsong {

namespace {

constexpr char const* kKeyStartMode   = "dynsong-start-mode";
constexpr char const* kKeyRandomMin   = "dynsong-random-min-pct";
constexpr char const* kKeyRandomMax   = "dynsong-random-max-pct";
constexpr char const* kKeyRotation    = "dynsong-rotation-mode";
constexpr char const* kKeyVolume      = "dynsong-volume-pct";
constexpr char const* kKeyFade        = "dynsong-fade-seconds";
constexpr char const* kKeyLevelSelect = "dynsong-in-level-select";
// Kept from the first version of the feature so nobody loses their choice.
constexpr char const* kKeyStream      = "dynamic-song-stream-preview";

constexpr char const* kKeySubEnabled  = "dynsong-submerge-enabled";
constexpr char const* kKeySubPreset   = "dynsong-submerge-preset";
constexpr char const* kKeySubCutoff   = "dynsong-submerge-cutoff-hz";
constexpr char const* kKeySubHighpass = "dynsong-submerge-highpass-hz";
constexpr char const* kKeySubDuck     = "dynsong-submerge-duck-db";
constexpr char const* kKeySubReverb   = "dynsong-submerge-reverb-mix";
constexpr char const* kKeySubPitch    = "dynsong-submerge-pitch";
constexpr char const* kKeySubDive     = "dynsong-submerge-dive-seconds";
constexpr char const* kKeySubSurface  = "dynsong-submerge-surface-seconds";
constexpr char const* kKeySubOnExit   = "dynsong-submerge-on-level-exit";
constexpr char const* kKeySubHold     = "dynsong-submerge-hold-seconds";

DynamicSongConfig g_cfg;
bool g_loaded = false;

void normalize(DynamicSongConfig& cfg) {
    cfg.randomMinPct = std::clamp(cfg.randomMinPct, 0, 90);
    cfg.randomMaxPct = std::clamp(cfg.randomMaxPct, 5, 100);
    // A window that closed on itself would seek to a single fixed point.
    if (cfg.randomMaxPct <= cfg.randomMinPct) {
        cfg.randomMaxPct = std::min(100, cfg.randomMinPct + 5);
        if (cfg.randomMaxPct <= cfg.randomMinPct) cfg.randomMinPct = cfg.randomMaxPct - 5;
    }

    cfg.volumePct   = std::clamp(cfg.volumePct, 20, 120);
    cfg.fadeSeconds = std::clamp(cfg.fadeSeconds, 0.05f, 3.0f);

    auto& sub = cfg.submerge;
    sub.cutoffHz       = std::clamp(sub.cutoffHz, 120.f, 6000.f);
    sub.highpassHz     = std::clamp(sub.highpassHz, 20.f, 1600.f);
    sub.duckDb         = std::clamp(sub.duckDb, -24.f, 0.f);
    sub.reverbMix      = std::clamp(sub.reverbMix, 0.f, 100.f);
    sub.pitch          = std::clamp(sub.pitch, 0.5f, 1.5f);
    sub.diveSeconds    = std::clamp(sub.diveSeconds, 0.05f, 4.0f);
    sub.surfaceSeconds = std::clamp(sub.surfaceSeconds, 0.05f, 6.0f);
    sub.holdSeconds    = std::clamp(sub.holdSeconds, 0.1f, 5.0f);
}

} // namespace

char const* submergePresetId(SubmergePreset preset) {
    switch (preset) {
        case SubmergePreset::Custom:     return "custom";
        case SubmergePreset::Underwater: return "underwater";
        case SubmergePreset::Muffled:    return "muffled";
        case SubmergePreset::Deep:       return "deep";
        case SubmergePreset::Radio:      return "radio";
        case SubmergePreset::Count:      break;
    }
    return "underwater";
}

SubmergePreset submergePresetFromId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(SubmergePreset::Count); ++i) {
        auto preset = static_cast<SubmergePreset>(i);
        if (id == submergePresetId(preset)) return preset;
    }
    return SubmergePreset::Underwater;
}

char const* startModeId(StartMode mode) {
    switch (mode) {
        case StartMode::Random:    return "random";
        case StartMode::Beginning: return "beginning";
        case StartMode::Resume:    return "resume";
        case StartMode::Count:     break;
    }
    return "random";
}

StartMode startModeFromId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(StartMode::Count); ++i) {
        auto mode = static_cast<StartMode>(i);
        if (id == startModeId(mode)) return mode;
    }
    return StartMode::Random;
}

char const* rotationModeId(RotationMode mode) {
    switch (mode) {
        case RotationMode::Rotate: return "rotate";
        case RotationMode::Random: return "random";
        case RotationMode::First:  return "first";
        case RotationMode::Count:  break;
    }
    return "rotate";
}

RotationMode rotationModeFromId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(RotationMode::Count); ++i) {
        auto mode = static_cast<RotationMode>(i);
        if (id == rotationModeId(mode)) return mode;
    }
    return RotationMode::Rotate;
}

SubmergeConfig submergePresetConfig(SubmergePreset preset) {
    SubmergeConfig cfg;
    cfg.preset = preset;

    switch (preset) {
        case SubmergePreset::Underwater:
            cfg.cutoffHz = 520.f;
            cfg.highpassHz = 20.f;
            cfg.duckDb = -5.f;
            cfg.reverbMix = 22.f;
            cfg.pitch = 0.94f;
            break;
        case SubmergePreset::Muffled:
            // Behind a door rather than under water: no pitch bend, barely wet.
            cfg.cutoffHz = 900.f;
            cfg.highpassHz = 20.f;
            cfg.duckDb = -4.f;
            cfg.reverbMix = 6.f;
            cfg.pitch = 1.f;
            break;
        case SubmergePreset::Deep:
            cfg.cutoffHz = 280.f;
            cfg.highpassHz = 20.f;
            cfg.duckDb = -9.f;
            cfg.reverbMix = 40.f;
            cfg.pitch = 0.87f;
            break;
        case SubmergePreset::Radio:
            // Band-limited both ends, which reads as a small speaker.
            cfg.cutoffHz = 2600.f;
            cfg.highpassHz = 500.f;
            cfg.duckDb = -3.f;
            cfg.reverbMix = 4.f;
            cfg.pitch = 1.f;
            break;
        case SubmergePreset::Custom:
        case SubmergePreset::Count:
            break;
    }
    return cfg;
}

void loadConfig() {
    auto* mod = Mod::get();
    DynamicSongConfig cfg;

    cfg.startMode = startModeFromId(
        mod->getSavedValue<std::string>(kKeyStartMode, startModeId(cfg.startMode)));
    cfg.randomMinPct = mod->getSavedValue<int>(kKeyRandomMin, cfg.randomMinPct);
    cfg.randomMaxPct = mod->getSavedValue<int>(kKeyRandomMax, cfg.randomMaxPct);
    cfg.rotationMode = rotationModeFromId(
        mod->getSavedValue<std::string>(kKeyRotation, rotationModeId(cfg.rotationMode)));
    cfg.volumePct     = mod->getSavedValue<int>(kKeyVolume, cfg.volumePct);
    cfg.fadeSeconds   = mod->getSavedValue<float>(kKeyFade, cfg.fadeSeconds);
    cfg.inLevelSelect = mod->getSavedValue<bool>(kKeyLevelSelect, cfg.inLevelSelect);
    cfg.streamPreview = mod->getSavedValue<bool>(kKeyStream, cfg.streamPreview);

    auto& sub = cfg.submerge;
    sub.enabled = mod->getSavedValue<bool>(kKeySubEnabled, sub.enabled);
    sub.preset  = submergePresetFromId(
        mod->getSavedValue<std::string>(kKeySubPreset, submergePresetId(sub.preset)));

    // A named preset owns its tone knobs: reading the stored ones back would
    // resurrect whatever a Custom session left behind.
    SubmergeConfig const tone = (sub.preset == SubmergePreset::Custom)
        ? SubmergeConfig{}
        : submergePresetConfig(sub.preset);

    sub.cutoffHz   = (sub.preset == SubmergePreset::Custom)
        ? mod->getSavedValue<float>(kKeySubCutoff, tone.cutoffHz) : tone.cutoffHz;
    sub.highpassHz = (sub.preset == SubmergePreset::Custom)
        ? mod->getSavedValue<float>(kKeySubHighpass, tone.highpassHz) : tone.highpassHz;
    sub.duckDb     = (sub.preset == SubmergePreset::Custom)
        ? mod->getSavedValue<float>(kKeySubDuck, tone.duckDb) : tone.duckDb;
    sub.reverbMix  = (sub.preset == SubmergePreset::Custom)
        ? mod->getSavedValue<float>(kKeySubReverb, tone.reverbMix) : tone.reverbMix;
    sub.pitch      = (sub.preset == SubmergePreset::Custom)
        ? mod->getSavedValue<float>(kKeySubPitch, tone.pitch) : tone.pitch;

    // Timing is the user's either way: a preset is a tone, not a tempo.
    sub.diveSeconds    = mod->getSavedValue<float>(kKeySubDive, sub.diveSeconds);
    sub.surfaceSeconds = mod->getSavedValue<float>(kKeySubSurface, sub.surfaceSeconds);
    sub.onLevelExit    = mod->getSavedValue<bool>(kKeySubOnExit, sub.onLevelExit);
    sub.holdSeconds    = mod->getSavedValue<float>(kKeySubHold, sub.holdSeconds);

    normalize(cfg);
    g_cfg = cfg;
    g_loaded = true;
}

void saveConfig(DynamicSongConfig cfg) {
    normalize(cfg);

    auto* mod = Mod::get();
    mod->setSavedValue(kKeyStartMode, std::string(startModeId(cfg.startMode)));
    mod->setSavedValue(kKeyRandomMin, cfg.randomMinPct);
    mod->setSavedValue(kKeyRandomMax, cfg.randomMaxPct);
    mod->setSavedValue(kKeyRotation, std::string(rotationModeId(cfg.rotationMode)));
    mod->setSavedValue(kKeyVolume, cfg.volumePct);
    mod->setSavedValue(kKeyFade, cfg.fadeSeconds);
    mod->setSavedValue(kKeyLevelSelect, cfg.inLevelSelect);
    mod->setSavedValue(kKeyStream, cfg.streamPreview);

    auto const& sub = cfg.submerge;
    mod->setSavedValue(kKeySubEnabled, sub.enabled);
    mod->setSavedValue(kKeySubPreset, std::string(submergePresetId(sub.preset)));
    mod->setSavedValue(kKeySubCutoff, sub.cutoffHz);
    mod->setSavedValue(kKeySubHighpass, sub.highpassHz);
    mod->setSavedValue(kKeySubDuck, sub.duckDb);
    mod->setSavedValue(kKeySubReverb, sub.reverbMix);
    mod->setSavedValue(kKeySubPitch, sub.pitch);
    mod->setSavedValue(kKeySubDive, sub.diveSeconds);
    mod->setSavedValue(kKeySubSurface, sub.surfaceSeconds);
    mod->setSavedValue(kKeySubOnExit, sub.onLevelExit);
    mod->setSavedValue(kKeySubHold, sub.holdSeconds);

    g_cfg = cfg;
    g_loaded = true;
}

DynamicSongConfig const& config() {
    if (!g_loaded) loadConfig();
    return g_cfg;
}

} // namespace paimon::dynsong
