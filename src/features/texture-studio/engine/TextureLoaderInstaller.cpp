#include "TextureLoaderInstaller.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/utils/file.hpp>

#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

bool TextureLoaderInstaller::isInstalled() {
    return Loader::get()->isModLoaded("geode.texture-loader");
}

std::filesystem::path TextureLoaderInstaller::packsDir() {
    // Texture Loader stores packs at:
    //   <save dir>/geode/config/geode.texture-loader/packs/
    // We get there via Loader::getInstalledMod (works even if disabled)
    // and fallback to dirs::getModConfigDir if that's missing.
    if (auto* mod = Loader::get()->getInstalledMod("geode.texture-loader")) {
        return mod->getConfigDir() / "packs";
    }
    return dirs::getModConfigDir() / "geode.texture-loader" / "packs";
}

geode::Result<std::filesystem::path> TextureLoaderInstaller::install(
    std::filesystem::path const& sourceZip,
    std::string const& packId) {

    if (!isInstalled()) {
        return Err("Texture Loader is not installed.");
    }
    std::error_code ec;
    if (!std::filesystem::exists(sourceZip, ec)) {
        return Err("Source zip does not exist: {}", sourceZip.string());
    }

    auto target = packsDir();
    std::filesystem::create_directories(target, ec);
    if (ec) {
        return Err("Cannot create Texture Loader packs dir: {}", ec.message());
    }

    auto destPath = target / (packId + ".zip");

    // Replace existing pack with the new one. copy_file with
    // overwrite_existing avoids a stale-file race.
    std::filesystem::copy_file(sourceZip, destPath,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return Err("Copy failed: {}", ec.message());
    }
    return Ok(destPath);
}

}  // namespace paimon::texture_studio
