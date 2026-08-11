#pragma once
// Compartir iconos: empaqueta un proyecto (project.json + images/) como un
// archivo .paimbicon (zip) e importa los de otras personas como slot nuevo.

#include <Geode/Geode.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace paimon::icon_maker {

class IconShare final {
public:
    // Writes saveDir/icon-maker/export/<id>.paimbicon and returns its path.
    static geode::Result<std::filesystem::path> exportProject(std::string const& slotId);

    // Creates a fresh slot from a .paimbicon file; returns the new slot id.
    static geode::Result<std::string> importProject(std::filesystem::path const& file);

    // Opens a native picker for .paimbicon files, then imports.
    // The callback receives the new slot id on success.
    static void pickAndImport(std::function<void(geode::Result<std::string>)> callback);

private:
    IconShare() = delete;
};

}  // namespace paimon::icon_maker
