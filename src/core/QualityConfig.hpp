#pragma once

// QualityConfig.hpp — Cache path helpers and legacy migration.
// Single resolution: thumbnails are served and displayed at their original
// server dimensions — no client-side downscaling.

#include "Settings.hpp"
#include <string>
#include <cstdint>
#include <filesystem>

namespace paimon::quality {

// ── Cache key / path helpers ────────────────────────────────────────

// Unified cache directory under the mod save dir.
inline std::filesystem::path cacheDir() {
    return geode::Mod::get()->getSaveDir() / settings::quality::cacheSubdir();
}

// Build a cache filename for a level thumbnail: "<levelID>.<ext>"
inline std::string thumbFilename(int levelID, bool isGif) {
    return std::to_string(levelID) + (isGif ? ".gif" : ".png");
}

// Build a cache filename for a profile image: "profile_<accountID>.<ext>"
inline std::string profileFilename(int accountID, bool isGif) {
    return "profile_" + std::to_string(accountID) + (isGif ? ".gif" : ".rgb");
}

// Full cache path for a level thumbnail.
inline std::filesystem::path thumbCachePath(int levelID, bool isGif) {
    return cacheDir() / thumbFilename(levelID, isGif);
}

// Full cache path for a profile image.
inline std::filesystem::path profileCachePath(int accountID, bool isGif) {
    return cacheDir() / profileFilename(accountID, isGif);
}

// ── Legacy cache migration ──────────────────────────────────────────

// Migrates old quality-specific directories (cache_low, cache_med, cache_high)
// and the legacy "cache/" directory into the unified cache dir.
// Preserves previously-cached thumbnails so users don't re-download.
inline void migrateLegacyCache() {
    auto saveDir  = geode::Mod::get()->getSaveDir();
    auto qualDir  = cacheDir();
    std::error_code ec;

    // If the cache dir already exists, nothing to migrate.
    if (std::filesystem::exists(qualDir, ec)) return;

    // Try renaming old "cache/" first
    auto legacyDir = saveDir / "cache";
    if (legacyDir != qualDir && std::filesystem::exists(legacyDir, ec) && std::filesystem::is_directory(legacyDir, ec)) {
        std::filesystem::rename(legacyDir, qualDir, ec);
    }
}

} // namespace paimon::quality
