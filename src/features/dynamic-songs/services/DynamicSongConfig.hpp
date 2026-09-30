#pragma once

// shared dynamic-song storage. config()/saveconfig() make ui changes live.

#include <string>
#include <string_view>

namespace paimon::dynsong {

enum class StartMode {
    Random,  // random point inside the configured window.
    Beginning,
    Resume,
    Count,
};

enum class RotationMode {
    Rotate,
    Random,
    First,
    Count,
};

enum class SubmergePreset {
    Custom,
    Underwater,
    Muffled,
    Deep,
    Radio,
    Count,
};

// fully submerged filter values; the manager interpolates toward them.
struct SubmergeConfig {
    bool enabled = true;
    SubmergePreset preset = SubmergePreset::Underwater;

    float cutoffHz = 520.f;
    float highpassHz = 20.f;   // 20 = off.
    float duckDb = -5.f;       // gain while submerged.
    float reverbMix = 22.f;    // 0..100.
    float pitch = 0.94f;       // 1.0 = unchanged.

    float diveSeconds = 0.55f;     // play to submerged.
    float surfaceSeconds = 1.10f;  // submerged to clear.

    bool onLevelExit = true;
    float holdSeconds = 0.7f;  // grace before a cancelled play surfaces.
};

struct DynamicSongConfig {
    StartMode startMode = StartMode::Random;
    int randomMinPct = 15;
    int randomMaxPct = 85;

    RotationMode rotationMode = RotationMode::Rotate;

    int volumePct = 100;       // relative to gd's music slider.
    float fadeSeconds = 0.35f;

    bool inLevelSelect = true;
    bool streamPreview = true;

    SubmergeConfig submerge{};
};

// cached config, loaded on first use.
DynamicSongConfig const& config();
void loadConfig();
void saveConfig(DynamicSongConfig cfg);

SubmergeConfig submergePresetConfig(SubmergePreset preset);

char const* submergePresetId(SubmergePreset preset);
SubmergePreset submergePresetFromId(std::string_view id);
char const* startModeId(StartMode mode);
StartMode startModeFromId(std::string_view id);
char const* rotationModeId(RotationMode mode);
RotationMode rotationModeFromId(std::string_view id);

}
