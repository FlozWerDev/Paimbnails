#pragma once

#include <string>

namespace paimon::video {

class VideoDiskCache {
public:
    // Delete the cache for a specific video path
    static void deleteCache(const std::string& videoPath);

    // Delete all video caches; returns count of removed entries
    static int deleteAllCaches();
};

} // namespace paimon::video
