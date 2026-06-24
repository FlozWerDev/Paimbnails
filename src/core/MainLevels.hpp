#pragma once

// Official main levels (1-22) are prefetched at startup and preserved across cache clears.

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

// Range of official GD levels (1 = Stereo Madness, 22 = The Tower / sub-zero etc).
inline constexpr int kMainLevelMinID = 1;
inline constexpr int kMainLevelMaxID = 22;

// True if <levelID> is an official main level.
inline bool isMainLevelID(int levelID) {
    return levelID >= kMainLevelMinID && levelID <= kMainLevelMaxID;
}

// True if <filename> is an official main-level thumbnail. Accepts only
// "<id>.png" or "<id>.gif" with id in [1, 22]; the cache only stores main
// levels under those names, so the check is exact.
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

        // 1) Keep protected subdirs (e.g. cache/gifs/).
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

        // 2) Keep main-level thumbnails (1-22.png/.gif).
        if (entry.is_regular_file(dummy) && isMainLevelCacheFile(path.filename())) {
            preserved++;
            continue;
        }

        // 3) Remove the rest.
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

// Guard to avoid redundant main-level prefetch requests.
inline std::atomic<bool> g_mainLevelsPrefetched{false};

// Returns true only for the first caller.
inline bool tryClaimMainLevelsPrefetch() {
    bool expected = false;
    return g_mainLevelsPrefetched.compare_exchange_strong(expected, true);
}

// 14-day cache window for the main-level manifest. If all 22 thumbnails are on
// disk and were validated within this window, no network request is made.
inline constexpr int64_t kMainLevelsCacheTTLSeconds = 14LL * 24 * 60 * 60;
inline constexpr char const* kMainLevelsCachedAtKey = "main-levels-cached-at";

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
    if (cachedAt <= 0) return false;                 // never validated
    int64_t age = mainLevelsNowEpoch() - cachedAt;
    if (age < 0 || age > kMainLevelsCacheTTLSeconds) return false; // expired / clock moved
    return allMainLevelThumbnailsOnDisk();            // all present on disk
}

} // namespace paimon
