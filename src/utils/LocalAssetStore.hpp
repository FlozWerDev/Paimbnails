#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace paimon::assets {

enum class Kind {
    Image,
    Video,
    Media,
};

struct ImportResult {
    bool success = false;
    bool changed = false;
    std::filesystem::path path;
    std::string error;
};

std::filesystem::path rootDir();
std::filesystem::path dirForBucket(std::string const& bucket);
bool isManagedPath(std::filesystem::path const& path);
std::filesystem::path normalizePath(std::filesystem::path const& path);
std::string normalizePathString(std::filesystem::path const& path);
bool exists(std::string const& path);
ImportResult importToBucket(std::filesystem::path const& source, std::string const& bucket, Kind kind = Kind::Media);
ImportResult importStoredPath(std::string const& storedPath, std::string const& bucket, Kind kind = Kind::Media);
std::string importToBucketString(std::filesystem::path const& source, std::string const& bucket, Kind kind = Kind::Media);
std::string importStoredPathString(std::string const& storedPath, std::string const& bucket, Kind kind = Kind::Media);
bool cleanupBucket(std::string const& bucket);

} // namespace paimon::assets
