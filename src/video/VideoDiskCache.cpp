#include "VideoDiskCache.hpp"
#include "AudioExtractor.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>

namespace paimon::video {
namespace fs = std::filesystem;

// Mirror of getAudioCacheDir() in AudioExtractor.cpp.
// Kept in sync: both use temp_directory_path() / "paimbnails_audio_cache".
static std::filesystem::path audioCacheDir() {
    return std::filesystem::temp_directory_path() / "paimbnails_audio_cache";
}

static int removeDirectoryFiles(std::filesystem::path const& dir) {
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
    // Delegate to AudioExtractor which knows the exact hash-based WAV path.
    cleanupAudioCache(videoPath);
    geode::log::debug("[VideoDiskCache] Deleted audio cache for: {}", videoPath);
}

int VideoDiskCache::deleteAllCaches() {
    int removed = 0;
    int failed = 0;

    // Audio cache (extracted WAV files).
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

    // Video temp/cache MP4 + first-frame previews in the mod runtime dir.
    removed += removeDirectoryFiles(geode::dirs::getModRuntimeDir() / "video_cache");

    // Canonical normalized cache in the mod save dir.
    removed += removeDirectoryFiles(geode::Mod::get()->getSaveDir() / "video_cache");

    geode::log::info("[VideoDiskCache] deleteAllCaches: removed={} failed={}",
                     removed, failed);
    return removed;
}

} // namespace paimon::video