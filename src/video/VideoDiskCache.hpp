#pragma once

#include <string>

namespace paimon::video {

class VideoDiskCache {
public:
    static void deleteCache(const std::string& videoPath);
    // quota sweep for the vid_*.wav audio cache: keep the newest 20 files
    // totalling at most 512MB by mtime. Runs automatically after each audio
    // write and once lazily at startup; deleteAllCaches() stays as the
    // manual full wipe.
    static void pruneAudioCache();
    static int deleteAllCaches();
};

} // namespace paimon::video
