// Dynamic Volume uses a private FMOD meter/fader/limiter chain so it does not
// fight other features that drive the music channel volume.
#pragma once

#include "DynamicVolumeTypes.hpp"
#include "SafeDrop.hpp"

#include <fmod.hpp>
#include <string>

namespace paimon::dynvol {

struct LiveState {
    bool  active       = false;
    float songLufs     = kInvalidLufs;
    float referenceDb  = kInvalidLufs;
    float appliedGainDb = 0.0f;
    float floorDb      = 0.0f;
    float rampProgress = 1.0f;   // 0 ducked, 1 full
    bool  analyzing    = false;
    bool  safeDropActive = false;
    float safeDropGainDb = 0.0f;
};

class DynamicVolumeManager {
public:
    static DynamicVolumeManager& get();

    DynamicVolumeConfig getConfig() const { return m_cfg; }
    void saveConfig(DynamicVolumeConfig const& cfg);
    void loadConfig();

    void init();
    void shutdown();
    void setPerformancePaused(bool paused);

    void update(float dt);

    // Notify a new track; songKey may be empty when detected by polling.
    void notifySongChanged(std::string const& songKey);

    bool isSafeDropEnabled() const;
    void setSafeDropEnabled(bool enabled);

    LiveState liveState() const;

    // Return to 0 dB and forget the current song.
    void resetRuntimeState();

private:
    DynamicVolumeManager() = default;
    DynamicVolumeManager(DynamicVolumeManager const&) = delete;
    DynamicVolumeManager& operator=(DynamicVolumeManager const&) = delete;

    bool ensureDsps();
    void ensureOutputOrder();
    void setSafeDropActive(bool active);
    void detachDsps();
    void resetMeter();

    float readGroupGainDb() const;
    float readMomentaryLufs() const;
    float readWindowPeakLufs() const;
    SafeDropLevels readOutputLevels() const;

    void applyGainDb(float db);
    bool contextAllowed() const;
    // Detect an unannounced song change and notify it.
    bool pollForSongChange();

    DynamicVolumeConfig m_cfg{};
    bool m_loaded = false;
    bool m_shuttingDown = false;
    bool m_performancePaused = false;
    int  m_moduleCheckCooldown = 0;
    bool m_moduleOnCached = false;

    FMOD::DSP* m_gainDsp    = nullptr;
    FMOD::DSP* m_meterDsp   = nullptr;
    FMOD::DSP* m_limiterDsp = nullptr;
    FMOD::ChannelGroup* m_attachedGroup = nullptr;
    SafeDrop m_safeDrop;
    bool m_safeDropActive = false;

    std::string  m_songKey;
    FMOD::Sound* m_lastSound = nullptr;
    float m_songClock = 0.0f;     // seconds since current song started
    float m_pollClock = 0.0f;     // monotonic poll clock
    // Suppress duplicate polling after playMusic and level restarts.
    float m_suppressPollUntil = 0.0f;
    bool  m_analyzing = false;
    bool  m_hasSong   = false;

    float m_measuredLufs  = kInvalidLufs; // locked loudness of the current song
    float m_analysisPeak  = kInvalidLufs; // running peak during the analysis window
    float m_referenceLufs = kInvalidLufs; // what the previous song settled at
    float m_settledLufs   = kInvalidLufs; // current song, once analysis finished

    float m_floorDb   = 0.0f; // fully-ducked level for this song
    float m_targetDb  = 0.0f; // where the ramp says we should be right now
    float m_appliedDb = 0.0f; // what the fader actually has (slewed)
};

}

void initDynamicVolumeTicker();
void shutdownDynamicVolumeTicker();
