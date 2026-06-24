#pragma once

#include <string>

namespace paimon::video {

class VideoDiskCache {
public:
    static void deleteCache(const std::string& videoPath);
    static int deleteAllCaches();
};

} // namespace paimon::video
