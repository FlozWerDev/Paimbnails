#pragma once

#include <fmod.hpp>
#include <string_view>

namespace paimon::menumusic {

enum class MusicEffectsPreset {
    Custom,
    Original,
    SlowReverb,
    Dreamy,
    BassBoost,
    Nightcore,
    Underwater,
    ConcertHall,
    Lofi,
    Count,
};

enum class SpatialPreset {
    Custom,
    Off,
    Studio,
    Cinema,
    Arena,
    Orbit,
    Dreamwave,
    Count,
};

enum class SpatialMotion {
    Static,
    Orbit,
    Sway,
    Count,
};

struct MusicEffectsConfig {
    bool enabled = false;
    MusicEffectsPreset preset = MusicEffectsPreset::Original;

    float speed = 1.f;
    float gainDb = 0.f;
    float pan = 0.f;

    float bassDb = 0.f;
    float midDb = 0.f;
    float trebleDb = 0.f;
    float lowpassHz = 22000.f;
    float highpassHz = 20.f;

    float reverbMix = 0.f;
    float reverbDecay = 1.5f;
    float reverbRoom = 50.f;
    float reverbHighCut = 20000.f;

    float echoMix = 0.f;
    float echoDelay = 500.f;
    float echoFeedback = 30.f;

    bool spatialEnabled = false;
    SpatialPreset spatialPreset = SpatialPreset::Off;
    SpatialMotion spatialMotion = SpatialMotion::Static;
    float spatialWidth = 60.f;
    float spatialAngle = 0.f;
    float spatialRoom = 0.f;
    float spatialMotionSpeed = 12.f;
};

char const* musicEffectsPresetId(MusicEffectsPreset preset);
MusicEffectsPreset musicEffectsPresetFromId(std::string_view id);
MusicEffectsConfig musicEffectsPresetConfig(MusicEffectsPreset preset);
char const* spatialPresetId(SpatialPreset preset);
SpatialPreset spatialPresetFromId(std::string_view id);
char const* spatialMotionId(SpatialMotion motion);
SpatialMotion spatialMotionFromId(std::string_view id);

class MenuMusicEffects {
public:
    static MenuMusicEffects& get();

    MusicEffectsConfig const& config() const { return m_cfg; }
    void loadConfig();
    void saveConfig(MusicEffectsConfig config);
    void applyPreset(MusicEffectsPreset preset);
    void applySpatialPreset(SpatialPreset preset);

    void activateForCurrentTrack();
    void onMusicStarted(std::string_view path);
    void update();
    void tick(float dt);
    void setAuditionBypassed(bool bypassed);
    float currentSpatialAngle() const;
    float outputPeak() const;
    bool isSpatialActive() const;
    bool isAuditionBypassed() const { return m_auditionBypassed; }
    void deactivate();
    void shutdown();

private:
    MenuMusicEffects() = default;
    MenuMusicEffects(MenuMusicEffects const&) = delete;
    MenuMusicEffects& operator=(MenuMusicEffects const&) = delete;

    bool ensureDsps();
    void applyConfig();
    void applySpatialConfig();
    void applySpatialDirection();
    void bypassDsps();
    void detachDsps();

    MusicEffectsConfig m_cfg;
    bool m_loaded = false;
    bool m_shuttingDown = false;
    bool m_menuTrackActive = false;
    bool m_auditionBypassed = false;
    float m_spatialPhase = 0.f;
    float m_modulePollTime = 0.25f;

    FMOD::DSP* m_eqDsp = nullptr;
    FMOD::DSP* m_lowpassDsp = nullptr;
    FMOD::DSP* m_highpassDsp = nullptr;
    FMOD::DSP* m_reverbDsp = nullptr;
    FMOD::DSP* m_echoDsp = nullptr;
    FMOD::DSP* m_spatialPanDsp = nullptr;
    FMOD::DSP* m_spatialReverbDsp = nullptr;
    FMOD::DSP* m_gainDsp = nullptr;
    FMOD::DSP* m_meterDsp = nullptr;
    FMOD::ChannelGroup* m_attachedGroup = nullptr;
};

} // namespace paimon::menumusic
