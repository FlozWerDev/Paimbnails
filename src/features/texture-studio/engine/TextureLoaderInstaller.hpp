#pragma once
//
// TextureLoaderInstaller.hpp - Bridges our exported pack zip and the
// `geode.texture-loader` mod's packs directory. The latter is an optional
// dependency: we tolerate it being missing and surface a friendly message
// in that case.
//
// We do NOT auto-reload the game — Texture Loader requires a manual
// reload to pick up the new pack, and forcing one mid-session is too
// disruptive. We just copy the file and the user clicks "Apply" inside
// Texture Loader's own UI when ready.
//

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace paimon::texture_studio {

class TextureLoaderInstaller final {
public:
    // Returns true when Texture Loader is loaded right now (not just
    // installed). Use this to gate the "Apply" button visibility.
    static bool isInstalled();

    // Resolve the absolute path to Texture Loader's packs/ directory.
    // The directory is created when this is called.
    static std::filesystem::path packsDir();

    // Copy the given pack zip into Texture Loader's packs folder under
    // the given packId. Overwrites any previous version. Returns the
    // installed path on success.
    static geode::Result<std::filesystem::path> install(
        std::filesystem::path const& sourceZip,
        std::string const& packId);

private:
    TextureLoaderInstaller() = delete;
};

}  // namespace paimon::texture_studio
