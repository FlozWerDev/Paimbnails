#include "VideoDiskCache.hpp"
#include <Geode/loader/Log.hpp>
#include <filesystem>

namespace paimon::video {

void VideoDiskCache::deleteCache(const std::string& videoPath) {
    // TODO: implement disk cache cleanup for a specific video
    geode::log::debug("VideoDiskCache::deleteCache: {}", videoPath);
}

int VideoDiskCache::deleteAllCaches() {
    // TODO: implement full disk cache cleanup
    geode::log::info("VideoDiskCache::deleteAllCaches");
    return 0;
}

} // namespace paimon::video
