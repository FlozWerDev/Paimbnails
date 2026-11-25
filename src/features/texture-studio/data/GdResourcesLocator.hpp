#pragma once

#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace paimon::texture_studio {

struct DetectedSheet {
    std::string baseName;
    std::filesystem::path plistPath;
    std::filesystem::path pngPath;
    std::string qualitySuffix;
    std::int64_t fileSize = 0;
    int frameCount = -1;
};

class GdResourcesLocator final {
public:
    static geode::Result<std::vector<DetectedSheet>> detectVanillaSheets();

    static geode::Result<std::vector<DetectedSheet>> detectInDirectory(
        std::filesystem::path const& dir);

    static std::filesystem::path resourcesDir();

    // Reads a bundled asset (including APK assets/ on Android) into memory.
    static bool readBundledAsset(std::string const& relativeName, std::vector<std::uint8_t>& out);
    static bool bundledAssetExists(std::string const& relativeName);

private:
    GdResourcesLocator() = delete;
};

}  // namespace paimon::texture_studio
