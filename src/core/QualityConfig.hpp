#pragma once

#include "Settings.hpp"
#include <string>
#include <cstdint>
#include <filesystem>

namespace paimon::quality {

inline std::filesystem::path cacheDir() {
    return geode::Mod::get()->getSaveDir() / settings::quality::cacheSubdir();
}

inline std::string thumbFilename(int levelID, bool isGif) {
    return std::to_string(levelID) + (isGif ? ".gif" : ".png");
}

inline std::string profileFilename(int accountID, bool isGif) {
    return "profile_" + std::to_string(accountID) + (isGif ? ".gif" : ".rgb");
}

inline std::filesystem::path thumbCachePath(int levelID, bool isGif) {
    return cacheDir() / thumbFilename(levelID, isGif);
}

inline std::filesystem::path profileCachePath(int accountID, bool isGif) {
    return cacheDir() / profileFilename(accountID, isGif);
}

inline void migrateLegacyCache() {
    auto saveDir  = geode::Mod::get()->getSaveDir();
    auto qualDir  = cacheDir();
    std::error_code ec;

    if (std::filesystem::exists(qualDir, ec)) return;

    auto legacyDir = saveDir / "cache";
    if (legacyDir != qualDir && std::filesystem::exists(legacyDir, ec) && std::filesystem::is_directory(legacyDir, ec)) {
        std::filesystem::rename(legacyDir, qualDir, ec);
    }
}

} // namespace paimon::quality
