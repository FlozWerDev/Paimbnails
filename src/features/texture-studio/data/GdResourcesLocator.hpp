#pragma once

#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

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

private:
    GdResourcesLocator() = delete;
};

}  // namespace paimon::texture_studio
