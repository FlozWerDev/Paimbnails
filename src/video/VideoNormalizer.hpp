#pragma once

#include <Geode/loader/Log.hpp>
#include <vector>
#include <string>
#include <filesystem>

namespace paimon::video {

class VideoNormalizer {
public:
    // Normalize video data to canonical H.264+AAC MP4.
    // Returns the normalized data on success, or an error string on failure.
    static geode::Result<std::vector<uint8_t>, std::string> normalizeData(
        std::vector<uint8_t> const& data, std::string const& cacheKey)
    {
        // TODO: implement FFmpeg-based normalization
        // For now, pass through the original data
        return geode::Ok(data);
    }

    // Cancel all pending async normalization work (called during shutdown)
    static void shutdownAsyncWork() {
        // TODO: cancel async work
    }

    // Remove orphaned cache files older than 7 days
    static void cleanupOrphanedCache() {
        // TODO: implement orphaned cache cleanup
        geode::log::info("VideoNormalizer::cleanupOrphanedCache: stub");
    }
};

} // namespace paimon::video
