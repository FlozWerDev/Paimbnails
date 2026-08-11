#pragma once

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include "QualityConfig.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace paimon {

inline constexpr int kMainLevelMinID = 1;
inline constexpr int kMainLevelMaxID = 22;

inline bool isMainLevelID(int levelID) {
    return levelID >= kMainLevelMinID && levelID <= kMainLevelMaxID;
}

// Accepts only "<id>.png"/"<id>.gif" with id in [1, 22].
inline bool isMainLevelCacheFile(std::filesystem::path const& filename) {
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(filename.extension()));
    if (ext != ".png" && ext != ".gif") return false;

    auto stem = geode::utils::string::pathToString(filename.stem());
    auto idResult = geode::utils::numFromString<int>(stem);
    if (!idResult) return false;
    return isMainLevelID(idResult.unwrap());
}

// Clear <cacheDir> while keeping protected subdirs and main-level thumbnails (1-22).
inline std::pair<int, int> clearCachePreservingMainLevels(
    std::filesystem::path const& cacheDir,
    std::initializer_list<std::string_view> preservedSubdirs = {}
) {
    int preserved = 0;
    int removed = 0;

    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return {preserved, removed};
    }

    std::filesystem::directory_iterator it(cacheDir, ec);
    if (ec) {
        geode::log::warn(
            "[Paimbnails] clearCachePreservingMainLevels: failed to open dir: {}",
            ec.message()
        );
        return {preserved, removed};
    }

    for (auto const& entry : it) {
        std::error_code dummy;
        auto const path = entry.path();
        auto filename = geode::utils::string::pathToString(path.filename());

        if (entry.is_directory(dummy)) {
            bool keep = false;
            for (auto const& kept : preservedSubdirs) {
                if (filename == kept) {
                    keep = true;
                    break;
                }
            }
            if (keep) {
                preserved++;
                continue;
            }
        }

        if (entry.is_regular_file(dummy) && isMainLevelCacheFile(path.filename())) {
            preserved++;
            continue;
        }

        std::error_code rmEc;
        std::filesystem::remove_all(path, rmEc);
        if (rmEc) {
            geode::log::warn(
                "[Paimbnails] clearCachePreservingMainLevels: failed to remove {}: {}",
                geode::utils::string::pathToString(path), rmEc.message()
            );
        } else {
            removed++;
        }
    }

    return {preserved, removed};
}

inline std::atomic<bool> g_mainLevelsPrefetched{false};

inline bool tryClaimMainLevelsPrefetch() {
    bool expected = false;
    return g_mainLevelsPrefetched.compare_exchange_strong(expected, true);
}

inline constexpr int64_t kMainLevelsCacheTTLSeconds = 30LL * 24 * 60 * 60;
inline constexpr char const* kMainLevelsCachedAtKey = "main-levels-cached-at";

inline constexpr bool isMainLevelsCacheAgeFresh(int64_t cachedAt, int64_t now) {
    return cachedAt > 0 && now >= cachedAt
        && now - cachedAt < kMainLevelsCacheTTLSeconds;
}

static_assert(isMainLevelsCacheAgeFresh(1, kMainLevelsCacheTTLSeconds));
static_assert(!isMainLevelsCacheAgeFresh(1, kMainLevelsCacheTTLSeconds + 1));

inline int64_t mainLevelsNowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline bool allMainLevelThumbnailsOnDisk() {
    std::error_code ec;
    for (int id = kMainLevelMinID; id <= kMainLevelMaxID; ++id) {
        bool hasPng = std::filesystem::exists(paimon::quality::thumbCachePath(id, false), ec);
        bool hasGif = std::filesystem::exists(paimon::quality::thumbCachePath(id, true), ec);
        if (!hasPng && !hasGif) return false;
    }
    return true;
}

inline int64_t mainLevelsCachedAtEpoch() {
    return geode::Mod::get()->getSavedValue<int64_t>(kMainLevelsCachedAtKey, 0);
}

inline void markMainLevelsCached() {
    geode::Mod::get()->setSavedValue<int64_t>(kMainLevelsCachedAtKey, mainLevelsNowEpoch());
}

inline bool areMainLevelsFreshlyCached() {
    int64_t cachedAt = mainLevelsCachedAtEpoch();
    if (!isMainLevelsCacheAgeFresh(cachedAt, mainLevelsNowEpoch())) return false;
    return allMainLevelThumbnailsOnDisk();
}

} // namespace paimon
