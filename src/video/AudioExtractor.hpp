#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::video {

struct AudioPcm {
    std::vector<uint8_t> data;   // interleaved
    int channels      = 0;
    int sampleRate    = 0;
    int bitsPerSample = 0;

    bool valid() const {
        return !data.empty() && channels > 0 && sampleRate > 0 && bitsPerSample > 0;
    }
};

// first audio track to interleaved pcm; per-platform backend.
AudioPcm extractAudioToPcm(const std::string& videoPath);

// cached wav for file-path consumers only; playback uses videoaudiotrack.
std::string extractAudioToWav(const std::string& videoPath);

std::string getCachedWavPath(const std::string& videoPath);

void cleanupAudioCache(const std::string& videoPath);

} // namespace paimon::video
