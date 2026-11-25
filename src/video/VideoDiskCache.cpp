#include "../utils/FilesystemPath.hpp"
#include "VideoDiskCache.hpp"
#include "AudioExtractor.hpp"
#include "AudioWavCommon.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>

namespace paimon::video {
namespace fs = std::filesystem;

static int removeDirectoryFiles(fs::path const& dir, int* failed = nullptr) {
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
            if (failed) ++*failed;
            geode::log::warn("[VideoDiskCache] Failed to remove {}: {}",
                             paimon::paths::toUtf8(entry.path()), ec.message());
            ec.clear();
        }
    }
    return removed;
}

void VideoDiskCache::deleteCache(const std::string& videoPath) {
    cleanupAudioCache(videoPath);
    geode::log::debug("[VideoDiskCache] Deleted audio cache for: {}", videoPath);
}

void VideoDiskCache::pruneAudioCache() {
    detail::pruneAudioCache();
}

int VideoDiskCache::deleteAllCaches() {
    int removed = 0;
    int failed = 0;

    {
        auto dir = detail::getAudioCacheDir();
        std::error_code ec;
        if (!fs::exists(dir, ec) || ec) {
            geode::log::info("[VideoDiskCache] Audio cache directory does not exist: {}",
                             paimon::paths::toUtf8(dir));
        } else {
            removed += removeDirectoryFiles(dir, &failed);
        }
    }

    removed += removeDirectoryFiles(paimon::paths::toStdPath(geode::dirs::getModRuntimeDir()) / "video_cache");

    removed += removeDirectoryFiles(paimon::paths::toStdPath(geode::Mod::get()->getSaveDir()) / "video_cache");

    geode::log::info("[VideoDiskCache] deleteAllCaches: removed={} failed={}",
                     removed, failed);
    return removed;
}

} // namespace paimon::video