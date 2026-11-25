#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

// wav-cache helpers shared by all audio backends.
namespace paimon::video::detail {

// shared audio cache dir (creates it). Also used by VideoDiskCache wipes.
std::filesystem::path getAudioCacheDir();

// recursive: extractaudiotowav wraps extractaudiotopcm.
std::recursive_mutex& audioExtractorMutex();

// stable hash of the video path.
std::string makeWavPath(const std::string& videoPath);

// quota sweep for the vid_*.wav cache: keep the newest 20 files totalling at
// most 512MB by mtime, deleting the oldest first. Never throws. Called after
// each successful write and once lazily at startup; deleteAllCaches() in
// VideoDiskCache stays as the manual full wipe.
void pruneAudioCache();

// canonical 44-byte pcm wav header. sizes are patched after the data is known.
#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]        = {'R', 'I', 'F', 'F'};
    uint32_t fileSize       = 0;
    char     wave[4]        = {'W', 'A', 'V', 'E'};
    char     fmt[4]         = {'f', 'm', 't', ' '};
    uint32_t fmtSize        = 16;
    uint16_t audioFormat    = 1;  // pcm
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

// UTF-8 path on every platform. Rejects incomplete PCM frames and WAV size/rate
// overflow before opening the file; removes any partial file on write failure.
bool writeWavFile(const std::string& wavPath,
                  const uint8_t* pcm, size_t pcmBytes,
                  uint16_t numChannels, uint32_t sampleRate,
                  uint16_t bitsPerSample);

} // namespace paimon::video::detail
