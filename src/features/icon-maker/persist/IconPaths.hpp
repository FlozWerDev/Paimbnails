#pragma once

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace paimon::icon_maker {

class IconPaths final {
public:
    static std::filesystem::path rootDir();
    static std::filesystem::path indexFile();
    static std::filesystem::path slotsDir();
    static std::filesystem::path slotDir(std::string_view slotId);
    static std::filesystem::path projectFile(std::string_view slotId);

    // User-imported piece shapes and fill images live here (copied on import
    // so projects stay re-openable even if the source file moves).
    static std::filesystem::path imagesDir(std::string_view slotId);
    static std::filesystem::path imageFile(std::string_view slotId, std::string_view fileName);

    // Compiled spritesheets: <exportName>-uhd.png/.plist, -hd, plain (sd).
    static std::filesystem::path outputDir(std::string_view slotId);

    static std::filesystem::path thumbFile(std::string_view slotId);

    static std::string sanitizeFilename(std::string_view name);

    // Safe to call multiple times.
    static geode::Result<> ensureSlotDirs(std::string_view slotId);

private:
    IconPaths() = delete;
};

}  // namespace paimon::icon_maker
