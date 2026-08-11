#pragma once

#include <cstdint>
#include <mutex>
#include <string>

// Shared, platform-independent helpers for the per-platform audio extractors
// (Media Foundation on Windows, MediaNDK on Android, AVFoundation on Apple).
// Each backend only implements the decode-to-PCM step and reuses the WAV cache
// and writer defined here so the cache layout stays identical across platforms.
namespace paimon::video::detail {

// Serialises concurrent extractions (VideoPlayers can be created off the main
// thread) and protects the OS media stacks from re-entrancy. Recursive because
// extractAudioToWav wraps extractAudioToPcm.
std::recursive_mutex& audioExtractorMutex();

// Temp WAV path for a given source video. Stable hash of the video path.
std::string makeWavPath(const std::string& videoPath);

// Canonical 44-byte PCM WAV header. Sizes are patched after the data is known.
#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]        = {'R', 'I', 'F', 'F'};
    uint32_t fileSize       = 0;
    char     wave[4]        = {'W', 'A', 'V', 'E'};
    char     fmt[4]         = {'f', 'm', 't', ' '};
    uint32_t fmtSize        = 16;
    uint16_t audioFormat    = 1;  // PCM
    uint16_t numChannels    = 0;
    uint32_t sampleRate     = 0;
    uint32_t byteRate       = 0;
    uint16_t blockAlign     = 0;
    uint16_t bitsPerSample  = 0;
    char     data[4]        = {'d', 'a', 't', 'a'};
    uint32_t dataSize       = 0;
};
#pragma pack(pop)

static_assert(sizeof(WavHeader) == 44, "WAV header must be 44 bytes");

// Write a complete interleaved-PCM WAV file in one shot. Returns true on
// success. On failure removes any partial file and returns false.
bool writeWavFile(const std::string& wavPath,
                  const uint8_t* pcm, size_t pcmBytes,
                  uint16_t numChannels, uint32_t sampleRate,
                  uint16_t bitsPerSample);

} // namespace paimon::video::detail
