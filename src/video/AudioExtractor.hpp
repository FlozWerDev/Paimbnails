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

// Decode the first audio track to interleaved PCM in memory. Implemented per
// platform (Media Foundation / MediaNDK / AVFoundation).
AudioPcm extractAudioToPcm(const std::string& videoPath);

// Same decode, persisted as a cached WAV. Only for consumers that need a file
// path (profile music); playback goes through VideoAudioTrack instead.
std::string extractAudioToWav(const std::string& videoPath);

// Cached WAV path for a video, or empty when it has not been extracted.
std::string getCachedWavPath(const std::string& videoPath);

void cleanupAudioCache(const std::string& videoPath);

} // namespace paimon::video
