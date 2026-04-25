#pragma once

#include <string>
#include <filesystem>

namespace paimon::video {

// Extract the audio track from a video file to a temporary WAV file.
// Returns the path to the WAV on success, or empty string on failure.
// The WAV is cached — calling again with the same video returns the existing file.
std::string extractAudioToWav(const std::string& videoPath);

// Get the cached WAV path for a video (without extracting).
// Returns empty string if no cached WAV exists.
std::string getCachedWavPath(const std::string& videoPath);

// Clean up the WAV cache for a specific video.
void cleanupAudioCache(const std::string& videoPath);

} // namespace paimon::video
