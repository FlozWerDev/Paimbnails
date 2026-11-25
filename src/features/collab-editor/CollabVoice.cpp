#include "../../utils/Base64.hpp"
#include "CollabVoice.hpp"

#include "CollabManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <fmod.hpp>

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

constexpr int kSampleRate = 12000;             // mono hz.
constexpr int kFrameSamples = kSampleRate / 4;  // 250 ms.
constexpr int kGateHoldFrames = 3;              // ~750 ms hold.
constexpr size_t kFifoMaxSamples = kSampleRate * 2;     // 2 s cap.
constexpr size_t kFifoCatchupSamples = kSampleRate / 2; // 500 ms catch-up.

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

uint8_t muLawEncode(int16_t sample) {
    constexpr int kBias = 0x84, kClip = 32635;
    int sign = (sample >> 8) & 0x80;
    int s = sign ? -static_cast<int>(sample) : sample;
    if (s > kClip) s = kClip;
    s += kBias;
    int exponent = 7;
    for (int mask = 0x4000; (s & mask) == 0 && exponent > 0; mask >>= 1) --exponent;
    int mantissa = (s >> (exponent + 3)) & 0x0F;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

int16_t muLawDecode(uint8_t code) {
    code = ~code;
    int sign = code & 0x80;
    int exponent = (code >> 4) & 0x07;
    int mantissa = code & 0x0F;
    int s = (((mantissa << 3) + 0x84) << exponent) - 0x84;
    return static_cast<int16_t>(sign ? -s : s);
}

FMOD::System* fmodSystem() {
    auto* engine = FMODAudioEngine::sharedEngine();
    return engine ? engine->m_system : nullptr;
}

float voiceVolume() {
    double v = Mod::get()->getSettingValue<double>("collab-voice-volume");
    return std::isfinite(v) ? static_cast<float>(std::clamp(v, 0.0, 1.0)) : 1.f;
}

float vadThreshold() {
    double v = Mod::get()->getSettingValue<double>("collab-voice-sensitivity");
    if (v <= 0.0) v = 0.05;
    return static_cast<float>(v);
}

// perceptual 0..1 loudness; sqrt keeps normal speech visible without clipping.
float levelFromPeak(int peak) {
    float linear = std::min(1.f, static_cast<float>(peak) / 26000.f);
    return std::sqrt(linear);
}

}

struct CollabVoice::Speaker {
    // fifo behind a shared tap so an in-flight mixer callback never touches a freed speaker.
    struct Tap {
        std::mutex mutex;
        std::deque<int16_t> fifo;
    };

    FMOD::Sound* stream = nullptr;
    FMOD::Channel* channel = nullptr;
    std::string name;
    uint64_t tapId = 0;
    std::shared_ptr<Tap> tap;
    double lastFrameAt = 0.0;
    float level = 0.f; // 0..1 from the last frame.

    ~Speaker();
};

namespace {

// generation-keyed taps: the mixer callback resolves its id under lock, so dropPeer/stopAll
// can unsubscribe (erase) before stopping and releasing the fmod stream.
std::mutex g_tapMutex;
std::unordered_map<uint64_t, std::weak_ptr<CollabVoice::Speaker::Tap>> g_taps;
std::atomic_uint64_t g_tapGen{1};

// fmod mixer callback: drain the peer fifo and silence underruns.
FMOD_RESULT F_CALLBACK voicePcmRead(FMOD_SOUND* sound, void* data, unsigned int datalen) {
    void* userdata = nullptr;
    reinterpret_cast<FMOD::Sound*>(sound)->getUserData(&userdata);
    auto* out = static_cast<int16_t*>(data);
    unsigned int samples = datalen / sizeof(int16_t);
    std::shared_ptr<CollabVoice::Speaker::Tap> tap;
    if (userdata) {
        uint64_t id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(userdata));
        std::lock_guard lock(g_tapMutex);
        if (auto it = g_taps.find(id); it != g_taps.end()) tap = it->second.lock();
    }
    if (!tap) {
        std::memset(data, 0, datalen);
        return FMOD_OK;
    }
    std::lock_guard lock(tap->mutex);
    unsigned int n = 0;
    for (; n < samples && !tap->fifo.empty(); ++n) {
        out[n] = tap->fifo.front();
        tap->fifo.pop_front();
    }
    for (; n < samples; ++n) out[n] = 0;
    return FMOD_OK;
}

void detachSpeaker(CollabVoice::Speaker& speaker) {
    // unsubscribe first: later mixer callbacks resolve nothing and stay silent,
    // while one already running keeps the tap alive through its local copy.
    if (speaker.tapId != 0) {
        std::lock_guard lock(g_tapMutex);
        g_taps.erase(speaker.tapId);
        speaker.tapId = 0;
    }
    if (speaker.channel) {
        speaker.channel->stop();
        speaker.channel = nullptr;
    }
    if (speaker.stream) {
        speaker.stream->release();
        speaker.stream = nullptr;
    }
    speaker.tap.reset();
}

}

CollabVoice::Speaker::~Speaker() {
    detachSpeaker(*this);
}

CollabVoice& CollabVoice::get() {
    static CollabVoice instance;
    return instance;
}

void CollabVoice::setMicEnabled(bool enabled) {
    if (m_micEnabled == enabled) return;
    m_micEnabled = enabled;
    if (enabled) {
        if (!startRecording()) {
            m_micEnabled = false;
            Notification::create("No se pudo abrir el microfono", NotificationIcon::Error)->show();
        }
    } else {
        stopRecording();
    }
}

bool CollabVoice::startRecording() {
    if (m_recording) return true;
    auto* system = fmodSystem();
    if (!system) return false;

    int numDrivers = 0, numConnected = 0;
    if (system->getRecordNumDrivers(&numDrivers, &numConnected) != FMOD_OK || numConnected <= 0) {
        log::warn("[CollabVoice] no recording devices (drivers={} connected={})", numDrivers, numConnected);
        return false;
    }

    FMOD_CREATESOUNDEXINFO exinfo = {};
    exinfo.cbsize = sizeof(exinfo);
    exinfo.numchannels = 1;
    exinfo.format = FMOD_SOUND_FORMAT_PCM16;
    exinfo.defaultfrequency = kSampleRate;
    exinfo.length = kSampleRate * sizeof(int16_t); // 1 s ring

    if (system->createSound(nullptr, FMOD_OPENUSER | FMOD_LOOP_NORMAL, &exinfo, &m_recordSound) != FMOD_OK) {
        m_recordSound = nullptr;
        return false;
    }
    if (system->recordStart(0, m_recordSound, true) != FMOD_OK) {
        m_recordSound->release();
        m_recordSound = nullptr;
        return false;
    }
    m_readPos = 0;
    m_capture.clear();
    m_recording = true;
    log::info("[CollabVoice] recording started");
    return true;
}

void CollabVoice::stopRecording() {
    if (!m_recording) return;
    if (auto* system = fmodSystem()) system->recordStop(0);
    if (m_recordSound) {
        m_recordSound->release();
        m_recordSound = nullptr;
    }
    m_capture.clear();
    m_gateOpenTicks = 0;
    m_localLevel = 0.f;
    m_recording = false;
    log::info("[CollabVoice] recording stopped");
}

void CollabVoice::pumpRecording() {
    auto* system = fmodSystem();
    if (!system || !m_recordSound) return;

    unsigned int recordPos = 0;
    if (system->getRecordPosition(0, &recordPos) != FMOD_OK) return;
    if (recordPos == m_readPos) return;

    constexpr unsigned int kRingSamples = kSampleRate;
    unsigned int available = (recordPos >= m_readPos)
        ? recordPos - m_readPos
        : kRingSamples - m_readPos + recordPos;

    void *ptr1 = nullptr, *ptr2 = nullptr;
    unsigned int len1 = 0, len2 = 0;
    if (m_recordSound->lock(m_readPos * sizeof(int16_t), available * sizeof(int16_t), &ptr1, &ptr2, &len1, &len2) != FMOD_OK) {
        return;
    }
    if (ptr1 && len1) {
        auto* s = static_cast<int16_t const*>(ptr1);
        m_capture.insert(m_capture.end(), s, s + len1 / sizeof(int16_t));
    }
    if (ptr2 && len2) {
        auto* s = static_cast<int16_t const*>(ptr2);
        m_capture.insert(m_capture.end(), s, s + len2 / sizeof(int16_t));
    }
    m_recordSound->unlock(ptr1, ptr2, len1, len2);
    m_readPos = (m_readPos + available) % kRingSamples;
}

void CollabVoice::update(float) {
    if (!m_recording) return;
    if (!CollabManager::get().connected()) return;

    pumpRecording();

    while (m_capture.size() >= static_cast<size_t>(kFrameSamples)) {
        std::vector<int16_t> frame(m_capture.begin(), m_capture.begin() + kFrameSamples);
        m_capture.erase(m_capture.begin(), m_capture.begin() + kFrameSamples);

        // energy vad with a short hold so word tails are not clipped.
        int peak = 0;
        for (int16_t s : frame) peak = std::max(peak, std::abs(static_cast<int>(s)));
        bool voiced = peak > static_cast<int>(vadThreshold() * 32767.f);
        if (voiced) {
            m_gateOpenTicks = kGateHoldFrames;
        } else if (m_gateOpenTicks > 0) {
            --m_gateOpenTicks;
        }
        m_localLevel = voiced ? levelFromPeak(peak) : m_localLevel * 0.5f;
        if (m_gateOpenTicks > 0) emitFrame(frame);
    }

    // drop excessive backlog after a stall.
    if (m_capture.size() > static_cast<size_t>(kFrameSamples * 4)) m_capture.clear();
}

void CollabVoice::emitFrame(std::vector<int16_t> const& samples) {
    std::vector<uint8_t> encoded;
    encoded.reserve(samples.size());
    for (int16_t s : samples) encoded.push_back(muLawEncode(s));
    CollabManager::get().sendVoiceFrame(++m_txSeq, paimon::base64Encode(encoded));
}

CollabVoice::Speaker* CollabVoice::speakerFor(int clientId, std::string const& name) {
    auto it = m_speakers.find(clientId);
    if (it != m_speakers.end()) {
        it->second->name = name;
        return it->second.get();
    }

    auto* system = fmodSystem();
    if (!system) return nullptr;

    auto speaker = std::make_unique<Speaker>();
    speaker->name = name;
    speaker->tap = std::make_shared<Speaker::Tap>();
    speaker->tapId = g_tapGen.fetch_add(1, std::memory_order_relaxed);
    if (speaker->tapId == 0) speaker->tapId = g_tapGen.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(g_tapMutex);
        g_taps.emplace(speaker->tapId, speaker->tap);
    }

    FMOD_CREATESOUNDEXINFO exinfo = {};
    exinfo.cbsize = sizeof(exinfo);
    exinfo.numchannels = 1;
    exinfo.format = FMOD_SOUND_FORMAT_PCM16;
    exinfo.defaultfrequency = kSampleRate;
    exinfo.length = kSampleRate * sizeof(int16_t);
    exinfo.decodebuffersize = kSampleRate / 10; // 100 ms pulls.
    exinfo.pcmreadcallback = voicePcmRead;
    exinfo.userdata = reinterpret_cast<void*>(static_cast<uintptr_t>(speaker->tapId));

    FMOD_MODE mode = FMOD_OPENUSER | FMOD_LOOP_NORMAL | FMOD_CREATESTREAM;
    if (system->createSound(nullptr, mode, &exinfo, &speaker->stream) != FMOD_OK) {
        detachSpeaker(*speaker);
        return nullptr;
    }
    if (system->playSound(speaker->stream, nullptr, false, &speaker->channel) != FMOD_OK) {
        detachSpeaker(*speaker);
        return nullptr;
    }
    speaker->channel->setVolume(voiceVolume());

    auto* raw = speaker.get();
    m_speakers[clientId] = std::move(speaker);
    return raw;
}

void CollabVoice::onRemoteFrame(int from, std::string const& name, std::string const& b64) {
    if (!Mod::get()->getSettingValue<bool>("collab-voice") || from <= 0 ||
        b64.size() > ((kFifoMaxSamples + 2) / 3) * 4) return;

    std::vector<uint8_t> bytes;
    if (!paimon::base64Decode(b64, bytes)) return;
    if (bytes.empty() || bytes.size() > kFifoMaxSamples) return;
    auto* speaker = speakerFor(from, name);
    if (!speaker || !speaker->tap) return;

    int peak = 0;
    {
        auto tap = speaker->tap;
        std::lock_guard lock(tap->mutex);
        for (uint8_t b : bytes) {
            int16_t s = muLawDecode(b);
            peak = std::max(peak, std::abs(static_cast<int>(s)));
            tap->fifo.push_back(s);
        }
        // trim accumulated fifo latency after slow poll bursts.
        if (tap->fifo.size() > kFifoMaxSamples) {
            while (tap->fifo.size() > kFifoCatchupSamples) tap->fifo.pop_front();
        }
    }
    speaker->level = levelFromPeak(peak);
    speaker->lastFrameAt = nowSeconds();
    if (speaker->channel) speaker->channel->setVolume(voiceVolume());
}

std::vector<SpeakingInfo> CollabVoice::speakingNow() const {
    std::vector<SpeakingInfo> out;
    double t = nowSeconds();
    for (auto const& [id, speaker] : m_speakers) {
        double age = t - speaker->lastFrameAt;
        if (age >= 0.6) continue;
    // fade stale levels so speaking bars fall smoothly.
        float fade = age > 0.3 ? static_cast<float>((0.6 - age) / 0.3) : 1.f;
        out.push_back({id, speaker->name, speaker->level * fade});
    }
    return out;
}

void CollabVoice::dropPeer(int clientId) {
    auto it = m_speakers.find(clientId);
    if (it == m_speakers.end()) return;
    // cancel the mixer subscription before releasing the speaker.
    detachSpeaker(*it->second);
    m_speakers.erase(it);
}

void CollabVoice::stopAll() {
    stopRecording();
    m_micEnabled = false;
    // unsubscribe every tap before releasing any stream.
    for (auto& [id, speaker] : m_speakers) detachSpeaker(*speaker);
    m_speakers.clear();
}

}
