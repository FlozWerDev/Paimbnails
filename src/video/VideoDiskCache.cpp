#include "VideoDiskCache.hpp"
#include "AudioExtractor.hpp"
#include <Geode/loader/Log.hpp>
#include <filesystem>

namespace paimon::video {

// Mirror of getAudioCacheDir() in AudioExtractor.cpp.
// Kept in sync: both use temp_directory_path() / "paimbnails_audio_cache".
static std::filesystem::path audioCacheDir() {
    return std::filesystem::temp_directory_path() / "paimbnails_audio_cache";
}

void VideoDiskCache::deleteCache(const std::string& videoPath) {
    // Delegate to AudioExtractor which knows the exact hash-based WAV path.
    cleanupAudioCache(videoPath);
    geode::log::debug("[VideoDiskCache] Deleted audio cache for: {}", videoPath);
}

int VideoDiskCache::deleteAllCaches() {
    auto dir = audioCacheDir();
    std::error_code ec;

    if (!std::filesystem::exists(dir, ec) || ec) {
        geode::log::info("[VideoDiskCache] Audio cache directory does not exist: {}",
                         dir.string());
        return 0;
    }

    int removed = 0;
    int failed  = 0;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        std::filesystem::remove(entry.path(), ec);
        if (ec) {
            geode::log::warn("[VideoDiskCache] Failed to remove {}: {}",
                             entry.path().string(), ec.message());
            ++failed;
            ec.clear();
        } else {
            ++removed;
        }
    }

    geode::log::info("[VideoDiskCache] deleteAllCaches: removed={} failed={} dir={}",
                     removed, failed, dir.string());
    return removed;
}

} // namespace paimon::video

