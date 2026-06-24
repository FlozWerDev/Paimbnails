#pragma once

#include <Geode/loader/Log.hpp>
#include <vector>
#include <string>
#include <filesystem>

namespace paimon::video {

class VideoNormalizer {
public:
    static geode::Result<std::vector<uint8_t>, std::string> normalizeData(
        std::vector<uint8_t> const& data, std::string const& cacheKey)
    {
        return geode::Ok(data);
    }

    static void shutdownAsyncWork() {}

    static void cleanupOrphanedCache() {
        geode::log::info("VideoNormalizer::cleanupOrphanedCache: stub");
    }
};

} // namespace paimon::video
