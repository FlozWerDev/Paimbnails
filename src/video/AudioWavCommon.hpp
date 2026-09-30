#pragma once

#include <cstdint>
#include <mutex>
#include <string>

// wav-cache helpers shared by all audio backends.
namespace paimon::video::detail {

// recursive: extractaudiotowav wraps extractaudiotopcm.
std::recursive_mutex& audioExtractorMutex();

// stable hash of the video path.
std::string makeWavPath(const std::string& videoPath);

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

// removes any partial file on failure.
bool writeWavFile(const std::string& wavPath,
                  const uint8_t* pcm, size_t pcmBytes,
                  uint16_t numChannels, uint32_t sampleRate,
                  uint16_t bitsPerSample);

} // namespace paimon::video::detail
