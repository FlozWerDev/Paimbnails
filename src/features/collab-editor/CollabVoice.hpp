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

// Proximity-less room voice chat (Globed-style walkie-talkie) over the collab
// server's HTTP relay.
//
// Capture: FMOD recording API -> 12 kHz mono PCM16 ring buffer -> 250 ms
// frames gated by a simple energy VAD -> mu-law (8-bit) -> base64 -> POST
// /api/voice (~4.1 KB per frame, only while actually talking).
//
// Playback: one FMOD user-stream per speaking peer; a pcmread callback pulls
// decoded samples from that peer's jitter FIFO (zero-fill on underrun, so
// network hiccups produce silence instead of glitches).
class CollabVoice {
public:
    // Public so the FMOD pcmread callback (free function) can use it.
    struct Speaker;

    static CollabVoice& get();

    // Mic toggle (the editor overlay button). Enabling starts FMOD recording.
    void setMicEnabled(bool enabled);
    bool micEnabled() const { return m_micEnabled; }
    // True while the VAD gate is open (we are transmitting).
    bool transmitting() const { return m_gateOpenTicks > 0; }

    // Called every editor tick from CollabManager::tick().
    void update(float dt);

    // Incoming relayed frame from a peer (main thread).
    void onRemoteFrame(int from, std::string const& name, std::string const& b64);

    // Names of peers whose audio played within the last ~600 ms, for the
    // "who's talking" indicator. Includes "yo" marker handled by caller.
    std::vector<std::string> speakingPeers() const;

    // Drop a single peer's stream (peer left) or everything (disconnect).
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
    unsigned int m_readPos = 0; // in samples, within the record ring
    std::vector<int16_t> m_capture;

    // VAD gate: opens on energy above threshold, holds for a few frames so
    // words aren't clipped.
    int m_gateOpenTicks = 0;

    uint32_t m_txSeq = 0;

    std::unordered_map<int, std::unique_ptr<Speaker>> m_speakers;
};

} // namespace paimon::collab
