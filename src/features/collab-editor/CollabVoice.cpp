#include "CollabVoice.hpp"

#include "CollabManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <fmod.hpp>

#include <chrono>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

constexpr int kSampleRate = 12000;           // Hz, mono
constexpr int kFrameSamples = kSampleRate / 4; // 250 ms per frame
constexpr int kGateHoldFrames = 3;           // keep transmitting ~750 ms after voice
constexpr size_t kFifoMaxSamples = kSampleRate * 2;   // 2 s cap per speaker
constexpr size_t kFifoCatchupSamples = kSampleRate / 2; // trim back to 500 ms

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// --- G.711 mu-law ----------------------------------------------------------

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

// --- base64 ----------------------------------------------------------------

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64Encode(std::vector<uint8_t> const& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
        i += 3;
    }
    size_t rem = in.size() - i;
    if (rem == 1) {
        uint32_t v = in[i] << 16;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t v = (in[i] << 16) | (in[i + 1] << 8);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::vector<uint8_t> b64Decode(std::string const& in) {
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        for (auto& v : table) v = -1;
        for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(kB64[i])] = static_cast<int8_t>(i);
        init = true;
    }
    std::vector<uint8_t> out;
    out.reserve((in.size() / 4) * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int8_t v = table[static_cast<uint8_t>(c)];
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

FMOD::System* fmodSystem() {
    auto* engine = FMODAudioEngine::sharedEngine();
    return engine ? engine->m_system : nullptr;
}

float voiceVolume() {
    double v = Mod::get()->getSettingValue<double>("collab-voice-volume");
    return static_cast<float>(v <= 0.0 ? 1.0 : v);
}

float vadThreshold() {
    double v = Mod::get()->getSettingValue<double>("collab-voice-sensitivity");
    if (v <= 0.0) v = 0.05;
    return static_cast<float>(v);
}

} // namespace

// --- Playback speaker -------------------------------------------------------

struct CollabVoice::Speaker {
    FMOD::Sound* stream = nullptr;
    FMOD::Channel* channel = nullptr;
    std::string name;
    std::mutex mutex;
    std::deque<int16_t> fifo;
    double lastFrameAt = 0.0;

    ~Speaker() {
        if (channel) channel->stop();
        if (stream) stream->release();
    }
};

namespace {

// FMOD mixer-thread callback: feed the peer's FIFO into the stream, silence on
// underrun. userdata is the owning Speaker (kept alive until Sound::release).
FMOD_RESULT F_CALLBACK voicePcmRead(FMOD_SOUND* sound, void* data, unsigned int datalen) {
    void* userdata = nullptr;
    reinterpret_cast<FMOD::Sound*>(sound)->getUserData(&userdata);
    auto* out = static_cast<int16_t*>(data);
    unsigned int samples = datalen / sizeof(int16_t);
    auto* speaker = static_cast<CollabVoice::Speaker*>(userdata);
    if (!speaker) {
        std::memset(data, 0, datalen);
        return FMOD_OK;
    }
    std::lock_guard lock(speaker->mutex);
    unsigned int n = 0;
    for (; n < samples && !speaker->fifo.empty(); ++n) {
        out[n] = speaker->fifo.front();
        speaker->fifo.pop_front();
    }
    for (; n < samples; ++n) out[n] = 0;
    return FMOD_OK;
}

} // namespace

CollabVoice& CollabVoice::get() {
    static CollabVoice instance;
    return instance;
}

// --- Capture -----------------------------------------------------------------

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

        // Energy VAD: peak of the frame vs. sensitivity, with hold so word
        // tails aren't clipped.
        int peak = 0;
        for (int16_t s : frame) peak = std::max(peak, std::abs(static_cast<int>(s)));
        bool voiced = peak > static_cast<int>(vadThreshold() * 32767.f);
        if (voiced) {
            m_gateOpenTicks = kGateHoldFrames;
        } else if (m_gateOpenTicks > 0) {
            --m_gateOpenTicks;
        }
        if (m_gateOpenTicks > 0) emitFrame(frame);
    }

    // Cap accumulated backlog if something stalls (shouldn't grow past a frame).
    if (m_capture.size() > static_cast<size_t>(kFrameSamples * 4)) m_capture.clear();
}

void CollabVoice::emitFrame(std::vector<int16_t> const& samples) {
    std::vector<uint8_t> encoded;
    encoded.reserve(samples.size());
    for (int16_t s : samples) encoded.push_back(muLawEncode(s));
    CollabManager::get().sendVoiceFrame(++m_txSeq, b64Encode(encoded));
}

// --- Playback ----------------------------------------------------------------

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

    FMOD_CREATESOUNDEXINFO exinfo = {};
    exinfo.cbsize = sizeof(exinfo);
    exinfo.numchannels = 1;
    exinfo.format = FMOD_SOUND_FORMAT_PCM16;
    exinfo.defaultfrequency = kSampleRate;
    exinfo.length = kSampleRate * sizeof(int16_t);
    exinfo.decodebuffersize = kSampleRate / 10; // 100 ms pulls keep latency low
    exinfo.pcmreadcallback = voicePcmRead;
    exinfo.userdata = speaker.get();

    FMOD_MODE mode = FMOD_OPENUSER | FMOD_LOOP_NORMAL | FMOD_CREATESTREAM;
    if (system->createSound(nullptr, mode, &exinfo, &speaker->stream) != FMOD_OK) {
        return nullptr;
    }
    if (system->playSound(speaker->stream, nullptr, false, &speaker->channel) != FMOD_OK) {
        speaker->stream->release();
        speaker->stream = nullptr;
        return nullptr;
    }
    speaker->channel->setVolume(voiceVolume());

    auto* raw = speaker.get();
    m_speakers[clientId] = std::move(speaker);
    return raw;
}

void CollabVoice::onRemoteFrame(int from, std::string const& name, std::string const& b64) {
    if (!Mod::get()->getSettingValue<bool>("collab-voice")) return;
    auto* speaker = speakerFor(from, name);
    if (!speaker) return;

    auto bytes = b64Decode(b64);
    if (bytes.empty()) return;

    std::lock_guard lock(speaker->mutex);
    for (uint8_t b : bytes) speaker->fifo.push_back(muLawDecode(b));
    // Latency control: if the FIFO built up (slow poll bursts), skip ahead.
    if (speaker->fifo.size() > kFifoMaxSamples) {
        while (speaker->fifo.size() > kFifoCatchupSamples) speaker->fifo.pop_front();
    }
    speaker->lastFrameAt = nowSeconds();
    if (speaker->channel) speaker->channel->setVolume(voiceVolume());
}

std::vector<std::string> CollabVoice::speakingPeers() const {
    std::vector<std::string> out;
    double t = nowSeconds();
    for (auto const& [id, speaker] : m_speakers) {
        if (t - speaker->lastFrameAt < 0.6) out.push_back(speaker->name);
    }
    return out;
}

void CollabVoice::dropPeer(int clientId) {
    m_speakers.erase(clientId);
}

void CollabVoice::stopAll() {
    stopRecording();
    m_micEnabled = false;
    m_speakers.clear();
}

} // namespace paimon::collab
