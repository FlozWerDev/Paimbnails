#pragma once

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace paimon::texture_studio {

class SlotPaths final {
public:
    static std::filesystem::path rootDir();

    static std::filesystem::path slotsIndexFile();

    static std::filesystem::path slotsDir();

    static std::filesystem::path slotDir(std::string_view slotId);

    static std::filesystem::path projectFile(std::string_view slotId);

    static std::filesystem::path overridesDir(std::string_view slotId);

    // The sprite name is sanitized (slashes / colons → underscores).
    static std::filesystem::path overrideFile(std::string_view slotId,
                                              std::string_view spriteName);

    static std::filesystem::path spriteImageFile(std::string_view slotId,
                                                 std::string_view spriteName);

    static std::filesystem::path autoCacheFile(std::string_view slotId);

    static std::filesystem::path outputZipFile(std::string_view slotId);

    static std::string sanitizeFilename(std::string_view name);

    // Safe to call multiple times.
    static geode::Result<> ensureSlotDirs(std::string_view slotId);

private:
    SlotPaths() = delete;
};

}  // namespace paimon::texture_studio
