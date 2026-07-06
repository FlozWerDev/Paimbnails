#pragma once

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace paimon::texture_studio {

class TextureLoaderInstaller final {
public:
    // True only when Texture Loader is loaded right now (not just installed).
    static bool isInstalled();

    static std::filesystem::path packsDir();

    static geode::Result<std::filesystem::path> install(
        std::filesystem::path const& sourceZip,
        std::string const& packId);

private:
    TextureLoaderInstaller() = delete;
};

}  // namespace paimon::texture_studio
