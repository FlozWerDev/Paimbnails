#pragma once
//
// GdResourcesLocator.hpp - Discovers .plist + .png pairs that ship with
// Geometry Dash (or that are loaded into the textures cache by another mod).
//
// We rely on `geode::dirs::getResourcesDir()` for the cross-platform path —
// Geode already abstracts away the differences between Steam Windows /
// Mac.app/Contents/Resources / Android APK assets / iOS bundle Resources.
// Our job here is purely to enumerate which files inside that directory
// look like a sprite sheet pair.
//
// A "sheet pair" is a .plist whose `metadata.textureFileName` resolves to a
// .png that exists alongside it. We pick the highest available quality
// (-uhd > -hd > unsuffixed) so the user always edits the source resolution.
//

#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// One detected sheet (just metadata; we don't open it here).
struct DetectedSheet {
    std::string baseName;                  // "GJ_GameSheet01" (no quality suffix)
    std::filesystem::path plistPath;       // absolute
    std::filesystem::path pngPath;         // absolute
    std::string qualitySuffix;             // "-uhd", "-hd", or empty
    std::int64_t fileSize = 0;             // PNG size in bytes (rough cost hint)
    int frameCount = -1;                   // -1 if not yet sniffed
};

class GdResourcesLocator final {
public:
    // Search the GD Resources directory for sheet pairs. Each base name
    // appears at most once in the result (highest quality wins).
    //
    // The search is non-recursive — GD's Resources folder is flat in 2.2.
    static geode::Result<std::vector<DetectedSheet>> detectVanillaSheets();

    // Search any folder for sheet pairs. Used for "load from custom folder".
    static geode::Result<std::vector<DetectedSheet>> detectInDirectory(
        std::filesystem::path const& dir);

    // Convenience: the absolute path of GD's Resources folder.
    static std::filesystem::path resourcesDir();

private:
    GdResourcesLocator() = delete;
};

}  // namespace paimon::texture_studio
