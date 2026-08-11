#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace FMOD {
class Sound;
class Channel;
class System;
}

namespace paimon::collab {

// A speaking peer (or the local user) and its loudness level.
struct SpeakingInfo {
    int clientId = 0; // 0 = local.
    std::string name;
    float level = 0.f; // 0..1.
};

// HTTP-relay voice: 12 kHz mono PCM16 -> 250 ms VAD frames -> mu-law/base64.
// Playback uses one FMOD user stream per peer and zero-fills jitter underruns.
class CollabVoice {
public:
    // Public for the FMOD pcmread callback.
    struct Speaker;

    static CollabVoice& get();

    // Enabling starts FMOD recording.
    void setMicEnabled(bool enabled);
    bool micEnabled() const { return m_micEnabled; }
    // True while the VAD gate is open.
    bool transmitting() const { return m_gateOpenTicks > 0; }

    void update(float dt);

    // Incoming frame; called on the main thread.
    void onRemoteFrame(int from, std::string const& name, std::string const& b64);

    // Peers heard in the last ~600 ms; local user is excluded.
    std::vector<SpeakingInfo> speakingNow() const;
    float localLevel() const { return m_gateOpenTicks > 0 ? m_localLevel : 0.f; }

    // Drop one peer or all streams.
    void dropPeer(int clientId);
    void stopAll();

private:
    CollabVoice() = default;

    bool startRecording();
    void stopRecording();
    void pumpRecording();
    void emitFrame(std::vector<int16_t> const& samples);

    Speaker* speakerFor(int clientId, std::string const& name);

    bool m_micEnabled = false;
    bool m_recording = false;

    FMOD::Sound* m_recordSound = nullptr;
    unsigned int m_readPos = 0; // Samples within the record ring.
    std::vector<int16_t> m_capture;

    // Opens above the energy threshold and holds for a few frames.
    int m_gateOpenTicks = 0;
    float m_localLevel = 0.f; // 0..1 from the last frame.

    uint32_t m_txSeq = 0;

    std::unordered_map<int, std::unique_ptr<Speaker>> m_speakers;
};

}
