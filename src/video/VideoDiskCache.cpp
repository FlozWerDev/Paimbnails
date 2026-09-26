#include "VideoDiskCache.hpp"
#include "AudioExtractor.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>

namespace paimon::video {
namespace fs = std::filesystem;

// Keep in sync with getAudioCacheDir() in AudioExtractor.cpp.
static fs::path audioCacheDir() {
    return fs::temp_directory_path() / "paimbnails_audio_cache";
}

static int removeDirectoryFiles(fs::path const& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return 0;
    }

    int removed = 0;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        fs::remove(entry.path(), ec);
        if (!ec) {
            ++removed;
        } else {
            geode::log::warn("[VideoDiskCache] Failed to remove {}: {}",
                             geode::utils::string::pathToString(entry.path()), ec.message());
            ec.clear();
        }
    }
    return removed;
}

void VideoDiskCache::deleteCache(const std::string& videoPath) {
    cleanupAudioCache(videoPath);
    geode::log::debug("[VideoDiskCache] Deleted audio cache for: {}", videoPath);
}

int VideoDiskCache::deleteAllCaches() {
    int removed = 0;
    int failed = 0;

    {
        auto dir = audioCacheDir();
        std::error_code ec;
        if (!fs::exists(dir, ec) || ec) {
            geode::log::info("[VideoDiskCache] Audio cache directory does not exist: {}",
                             geode::utils::string::pathToString(dir));
        } else {
            for (auto const& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec) || ec) continue;
                fs::remove(entry.path(), ec);
                if (ec) {
                    geode::log::warn("[VideoDiskCache] Failed to remove {}: {}",
                                     geode::utils::string::pathToString(entry.path()), ec.message());
                    ++failed;
                    ec.clear();
                } else {
                    ++removed;
                }
            }
        }
    }

    removed += removeDirectoryFiles(geode::dirs::getModRuntimeDir() / "video_cache");

    removed += removeDirectoryFiles(geode::Mod::get()->getSaveDir() / "video_cache");

    geode::log::info("[VideoDiskCache] deleteAllCaches: removed={} failed={}",
                     removed, failed);
    return removed;
}

} // namespace paimon::video