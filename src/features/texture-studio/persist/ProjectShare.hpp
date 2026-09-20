#pragma once

// Shared json holds settings only; sheets re-detected locally on import.

#include "TextureProject.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>

namespace paimon::texture_studio {

class ProjectShare final {
public:
    // Writes project json; sheets re-resolved on import.
    static geode::Result<> exportTo(std::filesystem::path const& dst,
                                    TextureProject const& project);

    // Imports json as new slot; SlotStore resolves id collisions.
    static geode::Result<std::string> importFrom(std::filesystem::path const& src);

private:
    ProjectShare() = delete;
};

}  // namespace paimon::texture_studio
